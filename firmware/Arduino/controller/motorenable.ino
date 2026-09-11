// Motor-enable / kill switch sense, and the lamp inside that switch.
//
// This switch is the ONLY way navigation is engaged or disengaged. It feeds the
// motor, so wiring it to navigation as well means one action does the whole
// thing - the software state can never sit there claiming to steer a motor that
// has no power, and there is exactly one place to look to answer "is it on".
//
// Nothing else may set it: the `n` command is gone from both controller command
// surfaces (subscribe.ino's ~APCMD dispatch and telnet.ino), and the display's
// enable/disable button is gone with it. ~APDAT still carries nav_enabled, so
// the displays and the plugin show the state - they just no longer author it.
//
// Circuit (circuit/Controller, U7 "Motor Enable"):
//
//        +5V ---- U7.P1 (SW)          U7.P4 (LED+) ---- +5V
//                    \  operator switch    |
//   Motor5V ---- U7.P2 (SW)          U7.P3 (LED-) ---- Q1 collector (2N2222)
//        |                                              Q1 emitter -- GND
//        +-- MOTOR_ENABLE_R1 10k --+-- A6 (ADC6)         Q1 base -- 1k -- D6
//        |                         +-- C1 100nF -- GND
//        |    MOTOR_ENABLE_R2 10k -+
//        |            |
//        |           GND
//        +-- motor header pins 3/4, POWER1 pins 17/18 (the actual motor feed)
//
// So: closing the switch puts +5 V on Motor5V, the 10k/10k divider halves it,
// and A6 reads ~2.5 V. Open, R2 pulls A6 to 0 V - the divider is what makes
// "switch open" a defined 0 rather than a floating pin. On the LED side Q1 is
// a low-side switch, so D6 HIGH saturates Q1 and lights the lamp.
//
// A6 is GPIO13 on the Nano ESP32, which is an *ADC2* channel, and ADC2 is
// shared with Wi-Fi. On the ESP32-S3 a hardware arbiter gives Wi-Fi priority;
// a read that loses the arbitration comes back invalid, and the Arduino
// wrapper turns that into a plain 0. A single sample is therefore not
// trustworthy - hence the median below, and the debounce on top of it. A
// spurious 0 is the fail-safe direction (it reads as "switch off"), so the
// filtering is about avoiding nuisance disengages, not about safety.

#define MOTOR_ENABLE_SENSE_PIN A6  // divider off Motor5V: ~2.5 V closed, 0 V open
#define MOTOR_ENABLE_LED_PIN D6    // -> 1k -> 2N2222 base; HIGH = lamp on

// Hysteresis band around the ~2.5 V "on" level and the 0 V "off" level. Wide
// on purpose: nothing lives between those two states, so the gap only has to
// swallow ADC error and the RC's edge, and a reading that lands inside it
// leaves the debounced state alone.
#define MOTOR_ENABLE_ON_MV 1500
#define MOTOR_ENABLE_OFF_MV 900

#define MOTOR_ENABLE_SAMPLES 3     // median of this many, per check - see the ADC2 note above
#define MOTOR_ENABLE_DEBOUNCE_MS 100  // a new level must hold this long before it counts

static bool motorEnableLevel = false;   // debounced switch position
static bool motorEnableCandidate = false;
static unsigned long motorEnableCandidateSince = 0;
static bool motorEnableLedOn = false;

// Written from control_task (check_motor_enable), read from command_task
// (the telnet 'p' print) - volatile for cross-task visibility, same pattern as
// lastRudderReceiveTime in rudder.ino.
static volatile uint32_t motorEnableLastMv = 0;

// Median of a few samples. Not an average: the failure mode here is an
// occasional wildly wrong reading (see the ADC2 note), and a median discards
// that outright where a mean would drag the result toward it.
static uint32_t read_motor_enable_mv() {
  uint32_t s[MOTOR_ENABLE_SAMPLES];
  for (int i = 0; i < MOTOR_ENABLE_SAMPLES; i++) {
    s[i] = analogReadMilliVolts(MOTOR_ENABLE_SENSE_PIN);
  }
  for (int i = 1; i < MOTOR_ENABLE_SAMPLES; i++) {
    uint32_t v = s[i];
    int j = i - 1;
    while (j >= 0 && s[j] > v) {
      s[j + 1] = s[j];
      j--;
    }
    s[j + 1] = v;
  }
  return s[MOTOR_ENABLE_SAMPLES / 2];
}

static void set_motor_enable_led(bool on) {
  if (on == motorEnableLedOn) {
    return;
  }
  motorEnableLedOn = on;
  digitalWrite(MOTOR_ENABLE_LED_PIN, on ? HIGH : LOW);
}

void setup_motor_enable() {
  pinMode(MOTOR_ENABLE_LED_PIN, OUTPUT);
  digitalWrite(MOTOR_ENABLE_LED_PIN, LOW);
  motorEnableLedOn = false;

  // 11 dB attenuation gives the full ~0-3.1 V input span. Any less clips well
  // below the 2.5 V the divider produces, which would read as a stuck "on".
  analogSetPinAttenuation(MOTOR_ENABLE_SENSE_PIN, ADC_11db);

  // Seed the debounced state from where the switch actually is, and leave
  // navigation alone here: check_motor_enable() below will bring it into
  // agreement on its first tick. Setting it from setup() would be doing the
  // same job twice, before the compass has produced a heading to hold.
  motorEnableLastMv = read_motor_enable_mv();
  motorEnableLevel = (motorEnableLastMv >= MOTOR_ENABLE_ON_MV);
  motorEnableCandidate = motorEnableLevel;
  motorEnableCandidateSince = millis();

  DEBUG_PRINT("Motor enable switch at startup: ");
  DEBUG_PRINT(motorEnableLevel ? "on" : "off");
  DEBUG_PRINT(" (");
  DEBUG_PRINT(motorEnableLastMv);
  DEBUG_PRINTLN(" mV)");
}

// Called from control_task every tick (10 ms). Navigation simply *is* the
// debounced switch position - enforced every tick, not just on the edges. The
// per-tick enforcement is what makes the switch authoritative rather than
// advisory: if anything ever did set navigation behind its back (a future
// command, a stray call), the next tick puts it back. Nothing else is supposed
// to, but the switch is the kill switch, so it wins by construction rather than
// by everyone else remembering to behave.
//
// setNavigationEnabled() is only called on a real change - it does work on the
// disabled->enabled transition (seeds heading_desired from the current heading,
// cancels a pending compass fallback) that must not be re-run 100 times a
// second, and control_task's own was_navigating edge handling is what freezes
// the wheel on the way down.
void check_motor_enable() {
  uint32_t mv = read_motor_enable_mv();
  motorEnableLastMv = mv;

  bool level = motorEnableLevel;
  if (mv >= MOTOR_ENABLE_ON_MV) {
    level = true;
  } else if (mv <= MOTOR_ENABLE_OFF_MV) {
    level = false;
  }  // inside the hysteresis band: keep the level we already had

  unsigned long now = millis();
  if (level != motorEnableCandidate) {
    motorEnableCandidate = level;
    motorEnableCandidateSince = now;
  } else if (level != motorEnableLevel && (now - motorEnableCandidateSince) >= MOTOR_ENABLE_DEBOUNCE_MS) {
    motorEnableLevel = level;
    DEBUG_PRINT("Motor enable switch -> ");
    DEBUG_PRINTLN(level ? "on" : "off");
  }

  if (autoPilot.isNavigationEndabled() != motorEnableLevel) {
    DEBUG_PRINT("Navigation -> ");
    DEBUG_PRINTLN(motorEnableLevel ? "enabled" : "disabled");
    autoPilot.setNavigationEnabled(motorEnableLevel);
  }

  // The lamp follows navigation, which under this scheme is the switch - but
  // read it back from the state rather than from motorEnableLevel, so the lamp
  // is showing what the autopilot actually believes and not just what we sent.
  set_motor_enable_led(autoPilot.isNavigationEndabled());
}

// Debounced switch position, for the telnet status line.
bool motor_enable_switch_on() {
  return motorEnableLevel;
}

// Last median reading in millivolts - the raw number to look at when the
// switch and the reported state disagree.
uint32_t motor_enable_sense_mv() {
  return motorEnableLastMv;
}
