#include <Preferences.h>
#include <Adafruit_AS5600.h>

// Namespace/key are only ever written by calibrate_center() - nothing else
// touches flash. Offset is in raw AS5600 counts (0-4095), not degrees, so
// re-applying it never accumulates float rounding error.
#define RUDDER_PREFS_NAMESPACE "rudder"
#define CENTER_COUNTS 2048          // 180 degrees - dead center of rudder travel

static Adafruit_AS5600 as5600;
static uint16_t offsetCounts = 0;

// Guards the AS5600 and offsetCounts, both of which are touched from two tasks:
// sensor_task reads the angle at 50 Hz while command_task re-zeros on a
// relayed ~APCMD,z$ (see rudder.ino). A mutex is genuinely required rather
// than just careful ordering: one getRawAngle() is *two* Wire transactions
// (register-address write, then data read), so a transaction injected between
// them from the other core clobbers the AS5600's internal address pointer and
// the read comes back holding some other register's value.
// Recursive, matching the pattern in the controller/display AutoPilot classes.
static SemaphoreHandle_t angleMutex = NULL;

// Set from the AsyncUDP callback (subscribe.ino), consumed by command_task.
// The callback deliberately does not calibrate inline: that would put an I2C
// read and an NVS flash write on the network stack's own task.
static volatile bool calibrationRequested = false;

static void angle_lock() {
  if (angleMutex != NULL) {
    xSemaphoreTakeRecursive(angleMutex, portMAX_DELAY);
  }
}

static void angle_unlock() {
  if (angleMutex != NULL) {
    xSemaphoreGiveRecursive(angleMutex);
  }
}

// Reads the calibration offset saved by a previous calibrate_center() call, if
// any. Defaults to 0 (uncalibrated - reports raw angle verbatim) on first run.
// Called from setup_angle() before any task exists, so it needs no lock.
static void load_calibration() {
  Preferences prefs;
  prefs.begin(RUDDER_PREFS_NAMESPACE, true);  // read-only
  offsetCounts = prefs.getUShort("offset", 0);
  prefs.end();
  DEBUG_PRINT("Loaded calibration offset: ");
  DEBUG_PRINTLN(offsetCounts);
}

void setup_angle() {
  // Created first: both tasks lock through it, and they are only started at
  // the end of setup().
  angleMutex = xSemaphoreCreateRecursiveMutex();
  if (angleMutex == NULL) {
    DEBUG_PRINTLN("Failed to create angle mutex - rebooting");
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
    // The AS5600 is the whole point of this board, so we can't continue without
    // it. Rather than hang here forever - which on a headless board sealed in an
    // enclosure at the rudder stock means a brick until someone power-cycles it,
    // and which would also stop the Wi-Fi link, the command listener and the
    // FreeRTOS tasks from ever starting - reboot and try again. A transient I2C
    // glitch (cold, damp, marginal supply as the whole boat powers up) clears on
    // the retry. Same reasoning as setup_compass() in controller/compass.ino.
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
// idempotent "make this position center" command.
void request_calibration() {
  calibrationRequested = true;
}

// Takes a fresh raw reading and computes the offset that makes *this* position
// read as 180 degrees (dead center of rudder travel), then persists it so it
// survives a reboot. There is no ack packet - same as every other ~APCMD in
// this project, the sender confirms the change by watching the next telemetry
// value rather than a reply.
static void calibrate_center() {
  // The lock is held across the flash write as well as the I2C read. It would
  // be enough to hold it only for the read + offsetCounts update, but keeping
  // it for the write means the in-RAM offset and the persisted one can never
  // disagree, without that invariant depending on calibration only ever being
  // driven from a single task. The cost is that sensor_task blocks for the few
  // ms of the NVS write and drops a sample or two - irrelevant for a rare,
  // deliberate, boat-stationary operation.
  angle_lock();

  uint16_t raw = as5600.getRawAngle();
  offsetCounts = (CENTER_COUNTS + 4096 - raw) % 4096;

  Preferences prefs;
  prefs.begin(RUDDER_PREFS_NAMESPACE, false);  // read-write
  prefs.putUShort("offset", offsetCounts);
  prefs.end();

  uint16_t newOffset = offsetCounts;
  angle_unlock();

  DEBUG_PRINT("Calibrated: raw=");
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
  calibrate_center();
}

// Applies the calibration offset to a fresh raw reading. Called once per
// publish tick from publish.ino (on sensor_task). The float conversion is done
// after unlocking to keep the hold time down to just the I2C traffic.
void read_rudder_angle(float* degreesOut, bool* magnetOkOut) {
  angle_lock();
  uint16_t raw = as5600.getRawAngle();
  bool magnetOk = as5600.isMagnetDetected();
  uint16_t offset = offsetCounts;
  angle_unlock();

  uint16_t adjusted = (raw + offset) % 4096;
  *degreesOut = adjusted * 360.0 / 4096.0;
  *magnetOkOut = magnetOk;
}
