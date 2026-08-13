// Wind vane: an AS5600 magnetic rotary encoder on I2C reading the angle of the
// vane shaft. Structurally the same as rudder/angle.ino - same chip, same
// mutex reasoning, same "zero it once it's installed" calibration - with the
// zero at 0 degrees (dead ahead) rather than 180.
//
// Wiring, same constraints as the rudder board: the AS5600 module is powered
// from the Nano's 3V3 pin, NOT 5V/VIN. The module's onboard I2C pull-ups tie
// SDA/SCL to whatever powers it and the ESP32's GPIOs are 3.3V-only. SDA/SCL
// go to the Nano ESP32's dedicated SDA/SCL pins; DIR to GND.

#include <Preferences.h>
#include <Adafruit_AS5600.h>

// The "voffset" key is written by calibrate_bow() and nudge_bow(). It is in raw
// AS5600 counts (0-4095), not degrees, so re-applying it never accumulates
// float rounding error. The namespace it lives in is shared with the speed
// calibration in anemometer.ino - see WIND_PREFS_NAMESPACE in Wind.h.
#define BOW_COUNTS 0  // 0 degrees - vane pointing dead ahead

// Largest single trim accepted from ~APCMD,d<degrees>$. The full half-circle is
// allowed on purpose: 180 is exactly what you would send for a head that turned
// out to be mounted back to front. As with the speed calibration the bound is
// written as !(in range) at the check below, so a NaN out of a malformed
// datagram is refused rather than written to flash.
#define VANE_NUDGE_ABS_MAX 180.0f

// Set to 1 if the vane reads backwards, i.e. the angle decreases as the vane
// swings to starboard. Which way an AS5600 counts depends on which face of the
// magnet it is looking at, so this depends on how the sensor board ends up
// mounted in the head rather than on anything you can decide up front - the
// original firmware carries the same flip for its Ventus variant, where the
// AS5600 sits underneath. Applied to raw counts before the calibration offset,
// so re-zeroing after flipping this is not required (though it does no harm).
#define VANE_INVERT 0

// AS5600 counts per full turn (12-bit). One count is 360/4096 = 0.087 degrees,
// which is the direction resolution the original reports for the Yachta head.
#define VANE_COUNTS 4096

static Adafruit_AS5600 as5600;
static uint16_t offsetCounts = 0;

// Guards the AS5600 and offsetCounts, both of which are touched from two tasks:
// sensor_task reads the angle every CALCULATE_INTERVAL_MS while command_task
// re-zeros or trims on a ~APCMD,v$ / ~APCMD,d$ (see wind.ino). A mutex is
// genuinely required rather than just careful ordering: one getRawAngle() is
// *two* Wire transactions (register-address write, then data read), so a
// transaction injected between them from the other core clobbers the AS5600's
// internal address pointer and the read comes back holding some other
// register's value.
// Recursive, matching the pattern in the controller/display AutoPilot classes.
static SemaphoreHandle_t vaneMutex = NULL;

// Pending requests, set from the AsyncUDP callback (subscribe.ino) and consumed
// by command_task. The callback deliberately does not do the work inline: that
// would put an I2C read and an NVS flash write on the network stack's own task.
//
// This is a *different* lock from vaneMutex above and guards a different thing:
// vaneMutex is the sensor and the live offset, this is only the command
// handoff. It is a spinlock rather than a bare volatile flag because the nudge
// carries a value alongside its flag, and a half-visible update would apply a
// trim of whatever happened to be in the variable last. Nothing that blocks is
// ever called while holding it.
static portMUX_TYPE vaneRequestMux = portMUX_INITIALIZER_UNLOCKED;
static bool zeroRequested = false;
static bool nudgeRequested = false;
static float pendingNudgeDegrees = 0.0f;

static void vane_lock() {
  if (vaneMutex != NULL) {
    xSemaphoreTakeRecursive(vaneMutex, portMAX_DELAY);
  }
}

static void vane_unlock() {
  if (vaneMutex != NULL) {
    xSemaphoreGiveRecursive(vaneMutex);
  }
}

// Raw encoder position with the mounting flip applied but not the calibration
// offset. Caller must hold the lock - this does I2C.
static uint16_t vane_raw_counts() {
  uint16_t counts = as5600.getRawAngle();
#if VANE_INVERT
  counts = (VANE_COUNTS - counts) % VANE_COUNTS;
#endif
  return counts;
}

// Reads the calibration offset saved by a previous calibrate_bow() call, if
// any. Defaults to 0 (uncalibrated - reports the encoder angle verbatim) on
// first run. Called from setup_vane() before any task exists, so it needs no
// lock.
static void load_calibration() {
  Preferences prefs;
  prefs.begin(WIND_PREFS_NAMESPACE, true);  // read-only
  offsetCounts = prefs.getUShort("voffset", 0);
  prefs.end();
  DEBUG_PRINT("Loaded vane calibration offset: ");
  DEBUG_PRINTLN(offsetCounts);
}

void setup_vane() {
  // Created first: both tasks lock through it, and they are only started at
  // the end of setup().
  vaneMutex = xSemaphoreCreateRecursiveMutex();
  if (vaneMutex == NULL) {
    DEBUG_PRINTLN("Failed to create vane mutex - rebooting");
    if (Serial) Serial.flush();
    delay(1000);
    ESP.restart();
  }

  // Try to initialize, retrying a transient failure before giving up.
  int attempts = 0;
  bool as5600Initialized = false;
  while (!as5600Initialized && attempts < 50) {
    as5600Initialized = as5600.begin();
    if (!as5600Initialized) {
      attempts++;
      delay(50);
    }
  }
  if (!as5600Initialized) {
    // Without the vane there is no wind direction, which is most of the point
    // of this board. Rather than hang here - which at the top of a mast means
    // a brick until someone kills the masthead circuit, and which would also
    // stop the Wi-Fi link, the command listener and the FreeRTOS tasks from
    // ever starting - reboot and try again. A transient I2C glitch (cold, damp,
    // marginal supply as the whole boat powers up) clears on the retry. Same
    // reasoning as setup_angle() in rudder/angle.ino and setup_compass() in
    // controller/compass.ino.
    DEBUG_PRINTLN("Failed to find AS5600 - rebooting to retry");
    if (Serial) Serial.flush();
    delay(1000);  // let the message flush and avoid a tight reboot loop
    ESP.restart();
  } else {
    DEBUG_PRINTLN("AS5600 found!");
  }

  load_calibration();
}

// Flags a re-zero for command_task to pick up. Safe to call from the AsyncUDP
// task; repeated requests simply coalesce, which is the right behaviour for an
// idempotent "the vane is pointing dead ahead right now" command.
void request_calibration() {
  portENTER_CRITICAL(&vaneRequestMux);
  zeroRequested = true;
  portEXIT_CRITICAL(&vaneRequestMux);
}

// Flags a trim for command_task to pick up. Unlike the re-zero this one does
// NOT coalesce - two requests are two trims - because it is relative. See the
// note on why relative in nudge_bow() below. Returns false if the value is
// refused.
bool request_vane_nudge(float degrees) {
  if (!(degrees >= -VANE_NUDGE_ABS_MAX && degrees <= VANE_NUDGE_ABS_MAX)) {
    DEBUG_PRINT("Rejected implausible vane nudge: ");
    DEBUG_PRINTLN2(degrees, 2);
    return false;
  }

  portENTER_CRITICAL(&vaneRequestMux);
  // Accumulate rather than overwrite: if two nudges arrive inside one
  // command_task tick, applying only the second would silently drop the first.
  // Adding them is what a relative trim means, and it keeps the outcome the
  // same whether the two land in one tick or two.
  pendingNudgeDegrees += degrees;
  nudgeRequested = true;
  portEXIT_CRITICAL(&vaneRequestMux);
  return true;
}

// Takes a fresh raw reading and computes the offset that makes *this* vane
// position read as 0 degrees, then persists it so it survives a reboot.
//
// This has to be a runtime command rather than a build-time constant for the
// same reason the rudder's centring does: you cannot know the encoder's offset
// relative to "vane pointing at the bow" until the head is assembled and
// bolted to the masthead bracket. Point the vane down the centreline (or motor
// head-to-wind and use the sailmaker's trick of eyeballing it against the
// backstay) and send ~APCMD,v$.
//
// There is no ack packet - same as every other ~APCMD in this project, the
// sender confirms the change by watching the next ~APWND value rather than a
// reply.
static void calibrate_bow() {
  // The lock is held across the flash write as well as the I2C read. It would
  // be enough to hold it only for the read + offsetCounts update, but keeping
  // it for the write means the in-RAM offset and the persisted one can never
  // disagree, without that invariant depending on calibration only ever being
  // driven from a single task. The cost is that sensor_task blocks for the few
  // ms of the NVS write and drops a sample or two - irrelevant for a rare,
  // deliberate operation done at anchor.
  vane_lock();

  uint16_t raw = vane_raw_counts();
  offsetCounts = (BOW_COUNTS + VANE_COUNTS - raw) % VANE_COUNTS;

  Preferences prefs;
  prefs.begin(WIND_PREFS_NAMESPACE, false);  // read-write
  prefs.putUShort("voffset", offsetCounts);
  prefs.end();

  uint16_t newOffset = offsetCounts;
  vane_unlock();

  DEBUG_PRINT("Vane calibrated: raw=");
  DEBUG_PRINT(raw);
  DEBUG_PRINT(" new offset=");
  DEBUG_PRINTLN(newOffset);
}

// Shifts the stored offset by a number of degrees, without reading the vane.
//
// This is the calibration you use once the head is up the mast and out of
// reach, where calibrate_bow() above is impossible. It also corrects errors
// calibrate_bow() *cannot*: it only ever fixes the vane's alignment to the
// sensor body, whereas the total error the boat experiences is that plus any
// rotation of the mast relative to the hull, plus the aerodynamic bias of
// sitting in the mast and mainsail's upwash. Neither of the latter two is
// visible from the masthead.
//
// You measure the total by tacking. A constant offset makes the two tacks
// disagree: sail close-hauled on starboard and average the reported angle off
// the bow, tack, do the same on port with identical trim, and the offset is
// *half* the difference - half because the error shifts both readings the same
// way in absolute terms while the two tacks measure from opposite sides. Then
// send the negation of it. Nothing about that procedure yields an absolute
// encoder offset, only a correction, which is why this command is relative:
// it maps one-to-one onto the number you actually measured.
//
// Working in whole counts (0.087 degrees) rather than storing degrees keeps
// this consistent with calibrate_bow() and with what "voffset" means. Each
// nudge rounds once, so a run of them can drift by up to 0.04 degrees apiece -
// far under the precision the tack test itself can deliver.
static void nudge_bow(float degrees) {
  int32_t deltaCounts = lroundf(degrees * (float)VANE_COUNTS / 360.0f);

  vane_lock();

  // C's % keeps the sign of the dividend, so a negative trim needs the extra
  // wrap to land back in 0..VANE_COUNTS-1.
  int32_t updated = ((int32_t)offsetCounts + deltaCounts) % VANE_COUNTS;
  if (updated < 0) {
    updated += VANE_COUNTS;
  }
  offsetCounts = (uint16_t)updated;

  Preferences prefs;
  prefs.begin(WIND_PREFS_NAMESPACE, false);  // read-write
  prefs.putUShort("voffset", offsetCounts);
  prefs.end();

  uint16_t newOffset = offsetCounts;
  vane_unlock();

  DEBUG_PRINT("Vane nudged by ");
  DEBUG_PRINT2(degrees, 2);
  DEBUG_PRINT(" deg (");
  DEBUG_PRINT(deltaCounts);
  DEBUG_PRINT(" counts), new offset=");
  DEBUG_PRINTLN(newOffset);
}

// Polled from command_task. Takes and clears both pending requests under the
// spinlock *before* doing any of the work, so a request that lands mid-write is
// serviced on the next tick rather than being swallowed - and so the blocking
// parts (I2C, flash) never run inside a critical section.
//
// If both are pending, the absolute zero is applied first and the relative trim
// on top of it, which is the only order that makes sense for the two of them.
void check_vane_calibration_request() {
  bool zero;
  bool nudge;
  float nudgeDegrees;

  portENTER_CRITICAL(&vaneRequestMux);
  zero = zeroRequested;
  nudge = nudgeRequested;
  nudgeDegrees = pendingNudgeDegrees;
  zeroRequested = false;
  nudgeRequested = false;
  pendingNudgeDegrees = 0.0f;
  portEXIT_CRITICAL(&vaneRequestMux);

  if (zero) {
    calibrate_bow();
  }
  if (nudge) {
    nudge_bow(nudgeDegrees);
  }
}

// Applies the calibration offset to a fresh raw reading. Called once per
// sample tick from sample_wind() (on sensor_task). The float conversion is
// done after unlocking to keep the hold time down to just the I2C traffic.
//
// magnitudeOut is the AS5600's measured field strength. Nothing steers on it;
// it is carried through to the debug dump because it is the one number that
// tells you whether the magnet gap in the head is right, which is worth having
// before the thing goes up the mast.
void read_vane(float* degreesOut, uint16_t* magnitudeOut, bool* okOut) {
  vane_lock();
  uint16_t raw = vane_raw_counts();
  uint16_t magnitude = as5600.getMagnitude();
  bool magnetOk = as5600.isMagnetDetected();
  uint16_t offset = offsetCounts;
  vane_unlock();

  uint16_t adjusted = (raw + offset) % VANE_COUNTS;
  *degreesOut = adjusted * 360.0 / VANE_COUNTS;
  *magnitudeOut = magnitude;
  *okOut = magnetOk;
}
