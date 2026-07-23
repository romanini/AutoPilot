// Relay (Åström-Hägglund) auto-tune for the heading PID (pid.ino).
//
// Operator flow: arm ("ready") while navigation is disabled - via a display's
// MODE-button hold or telnet's "pat" - then start it (a display's
// enable/disable button while armed, or another "t2" command). While running,
// the relay replaces pid_loop()/motor_control_loop()'s normal steering: it
// bangs the rudder to a fixed deflection each way around the heading at the
// moment it started, measures the resulting oscillation's period and
// amplitude, and from those computes new Kp/Ki (Tyreus-Luyben - gentler than
// classic Ziegler-Nichols, since an aggressive tune means a lurchy helm on a
// boat). Applied immediately and saved to flash via set_pid_gains() (pid.ino).
//
// autoTuneState (0=idle, 1=ready, 2=running) lives on AutoPilot since it's
// shared telemetry (published in ~APDAT) and needs validated state
// transitions from multiple tasks. The oscillation-measurement bookkeeping
// below is private to this file, same as pid.ino/motor.ino keep their own
// algorithm state as module statics.

#define AUTOTUNE_RELAY_AMPLITUDE_DEG 10.0  // fixed rudder deflection while relaying
#define AUTOTUNE_READY_TIMEOUT_MS 30000UL  // controller-side "ready" watchdog (mirrors the display's local copy)
#define AUTOTUNE_MAX_DURATION_MS 90000UL   // hard cap on a running tune - abort if it hasn't finished by then
#define AUTOTUNE_SETTLE_MS 3000UL          // ignore crossings/extrema during this initial settling transient
#define AUTOTUNE_ABORT_ERROR_DEG 60.0       // heading strayed this far from the tune setpoint -> abort, something's wrong
#define AUTOTUNE_TARGET_HALF_CYCLES 6       // stop once this many zero-crossings have been measured (~3 full periods)
#define AUTOTUNE_KP_MIN 0.1
#define AUTOTUNE_KP_MAX 5.0
#define AUTOTUNE_KI_MIN 0.0
#define AUTOTUNE_KI_MAX 0.5

static float at_setpoint;
static unsigned long at_start_time;
static unsigned long at_last_crossing_time;
static int at_half_cycle_count;
static float at_extreme_error;       // running peak |error| since the last crossing
static float at_sum_half_period_ms;
static float at_sum_amplitude_deg;
static int at_prev_error_sign;       // -1, 0 (not yet known), or 1

void autotune_finish(bool success, const char *reason) {
  move_motor(0);
  autoPilot.cancelAutoTune();
  DEBUG_PRINT("Autotune finished (");
  DEBUG_PRINT(success ? "ok" : "aborted");
  DEBUG_PRINT("): ");
  DEBUG_PRINTLN(reason);
}

void autotune_compute_and_apply() {
  float avg_half_period_ms = at_sum_half_period_ms / at_half_cycle_count;
  float avg_amplitude_deg = at_sum_amplitude_deg / at_half_cycle_count;  // "a" in the relay formula
  float Pu = (avg_half_period_ms * 2.0) / 1000.0;                       // full ultimate period, seconds

  if (avg_amplitude_deg < 0.5 || Pu <= 0.0) {
    autotune_finish(false, "oscillation too small to measure reliably");
    return;
  }

  float Ku = (4.0 * AUTOTUNE_RELAY_AMPLITUDE_DEG) / (PI * avg_amplitude_deg);

  // Tyreus-Luyben.
  float new_kp = Ku / 3.2;
  float Ti = 2.2 * Pu;
  float new_ki = new_kp / Ti;

  if (new_kp < AUTOTUNE_KP_MIN) new_kp = AUTOTUNE_KP_MIN;
  if (new_kp > AUTOTUNE_KP_MAX) new_kp = AUTOTUNE_KP_MAX;
  if (new_ki < AUTOTUNE_KI_MIN) new_ki = AUTOTUNE_KI_MIN;
  if (new_ki > AUTOTUNE_KI_MAX) new_ki = AUTOTUNE_KI_MAX;

  set_pid_gains(new_kp, new_ki);

  DEBUG_PRINT("Autotune: Ku=");
  DEBUG_PRINT2(Ku, 3);
  DEBUG_PRINT(" Pu=");
  DEBUG_PRINT2(Pu, 3);
  DEBUG_PRINT(" -> Kp=");
  DEBUG_PRINT2(new_kp, 3);
  DEBUG_PRINT(" Ki=");
  DEBUG_PRINTLN2(new_ki, 4);

  autotune_finish(true, "complete");
}

// Called every control_task tick while autoTuneState == 2 (running), in place
// of pid_loop()/motor_control_loop().
void autotune_loop(float diff_time) {
  unsigned long now = millis();
  if (now - at_start_time > AUTOTUNE_MAX_DURATION_MS) {
    autotune_finish(false, "timed out waiting for enough oscillation cycles");
    return;
  }

  float heading = autoPilot.getHeading();
  float error = at_setpoint - heading;
  if (error > 180.0) error -= 360.0;
  if (error < -180.0) error += 360.0;

  if (abs(error) > AUTOTUNE_ABORT_ERROR_DEG) {
    autotune_finish(false, "heading strayed too far from the tune setpoint");
    return;
  }

  bool settled = (now - at_start_time) > AUTOTUNE_SETTLE_MS;

  float abs_error = abs(error);
  if (settled && abs_error > at_extreme_error) {
    at_extreme_error = abs_error;
  }

  int error_sign = (error >= 0) ? 1 : -1;
  if (settled && at_prev_error_sign != 0 && error_sign != at_prev_error_sign) {
    if (at_last_crossing_time > 0) {
      unsigned long half_period = now - at_last_crossing_time;
      at_sum_half_period_ms += half_period;
      at_sum_amplitude_deg += at_extreme_error;
      at_half_cycle_count++;
    }
    at_last_crossing_time = now;
    at_extreme_error = 0.0;

    if (at_half_cycle_count >= AUTOTUNE_TARGET_HALF_CYCLES) {
      autotune_compute_and_apply();
      return;
    }
  }
  if (settled) {
    at_prev_error_sign = error_sign;
  }

  // Relay: bang to a fixed deflection toward reducing the error. Same sign
  // convention as pid_loop's -Kp*e (positive error -> negative steer command).
  float relay_steer = (error >= 0) ? -AUTOTUNE_RELAY_AMPLITUDE_DEG : AUTOTUNE_RELAY_AMPLITUDE_DEG;
  motor_control_loop(relay_steer);
}

// Called every control_task tick while autoTuneState == 1 (ready), to expire
// an arm nobody followed up on. Independent of (and a backstop for) the
// display's own local 30s copy of this timeout.
void autotune_check_ready_timeout() {
  if (millis() - autoPilot.getAutoTuneReadyAt() > AUTOTUNE_READY_TIMEOUT_MS) {
    autoPilot.cancelAutoTune();
    DEBUG_PRINTLN("Autotune: ready timed out, reverting");
  }
}

// Arm ("ready"). Only valid while navigation is disabled and nothing is
// already armed/running. Returns false (no-op) otherwise.
bool autotune_try_arm() {
  if (autoPilot.isNavigationEndabled()) {
    return false;
  }
  if (autoPilot.getAutoTuneState() != 0) {
    return false;
  }
  autoPilot.armAutoTune();
  DEBUG_PRINTLN("Autotune: armed (ready)");
  return true;
}

// Start running. Only valid from "ready". Returns false (no-op) otherwise.
bool autotune_try_start() {
  if (autoPilot.getAutoTuneState() != 1) {
    return false;
  }
  at_setpoint = autoPilot.getHeading();
  at_start_time = millis();
  at_last_crossing_time = 0;
  at_half_cycle_count = 0;
  at_extreme_error = 0.0;
  at_sum_half_period_ms = 0.0;
  at_sum_amplitude_deg = 0.0;
  at_prev_error_sign = 0;
  autoPilot.startAutoTune();
  DEBUG_PRINTLN("Autotune: running");
  return true;
}

// Abort from ready or running. No-op if already idle.
void autotune_abort() {
  if (autoPilot.getAutoTuneState() == 0) {
    return;
  }
  autotune_finish(false, "aborted by operator");
}
