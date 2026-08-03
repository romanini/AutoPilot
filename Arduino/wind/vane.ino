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

// Namespace/key are only ever written by calibrate_bow() - nothing else
// touches flash. Offset is in raw AS5600 counts (0-4095), not degrees, so
// re-applying it never accumulates float rounding error.
#define WIND_PREFS_NAMESPACE "wind"
#define BOW_COUNTS 0  // 0 degrees - vane pointing dead ahead

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
// re-zeros on a ~APCMD,v$ (see wind.ino). A mutex is genuinely required rather
// than just careful ordering: one getRawAngle() is *two* Wire transactions
// (register-address write, then data read), so a transaction injected between
// them from the other core clobbers the AS5600's internal address pointer and
// the read comes back holding some other register's value.
// Recursive, matching the pattern in the controller/display AutoPilot classes.
static SemaphoreHandle_t vaneMutex = NULL;

// Set from the AsyncUDP callback (subscribe.ino), consumed by command_task.
// The callback deliberately does not calibrate inline: that would put an I2C
// read and an NVS flash write on the network stack's own task.
static volatile bool calibrationRequested = false;

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
  calibrationRequested = true;
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

// Polled from command_task. Clears the flag *before* doing the work so a
// request that lands during a calibration is serviced on the next tick rather
// than being swallowed.
void check_calibration_request() {
  if (!calibrationRequested) {
    return;
  }
  calibrationRequested = false;
  calibrate_bow();
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
