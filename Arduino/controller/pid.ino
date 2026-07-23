#include <Wire.h>
#include <Preferences.h>

// Clamp on the integral term so it can't wind up without bound while fighting a
// current or a mechanical stop. Tunable: this caps the integral's steering
// authority relative to the proportional term (P = Kp * e, |e| <= 180).
#define PID_INTEGRAL_MAX 10.0

// Namespace/keys are only ever written by set_pid_gains() (called from
// autotune.ino after a completed relay auto-tune) - nothing else touches flash.
#define PID_PREFS_NAMESPACE "pid"

float Pi;
float e_prev;
float Kp, Ki, Kd;
int loopCount = 0;

void setup_pid() {
  Pi = 0.0;
  e_prev = 0.0;
  Kp = 1.0;
  Ki = 0.05;
  Kd = 0.0;

  // Pull in gains saved by a previous relay auto-tune, if any; otherwise keep
  // the defaults above.
  Preferences prefs;
  prefs.begin(PID_PREFS_NAMESPACE, true);  // read-only
  Kp = prefs.getFloat("kp", Kp);
  Ki = prefs.getFloat("ki", Ki);
  prefs.end();

  DEBUG_PRINTLN("PID all setup.");
}

// Apply new P/I gains immediately and persist them to flash so they survive a
// reboot. Called by autotune.ino when a relay auto-tune completes. Kd is left
// alone - the D term is currently disabled in pid_loop() regardless of its
// value (see the comment above `change_angle` below), so there's nothing
// meaningful to tune there yet.
void set_pid_gains(float new_kp, float new_ki) {
  Kp = new_kp;
  Ki = new_ki;
  Preferences prefs;
  prefs.begin(PID_PREFS_NAMESPACE, false);  // read-write
  prefs.putFloat("kp", Kp);
  prefs.putFloat("ki", Ki);
  prefs.end();
}

// Clear the integral accumulator and error history. Called when navigation is
// (re-)engaged so steering starts fresh instead of acting on stale windup.
void reset_pid() {
  Pi = 0.0;
  e_prev = 0.0;
}

float pid_loop(float target, float current, float time) {

  float e = target - current;
  if (e > 180.0) e = e - 360.0;
  if (e < -180.0) e = e + 360.0;
  float P = Kp * e;
  float l_ki, l_kd;
  l_ki = Ki;
  l_kd = Kd;
  if (abs(e) > 10.0) {
    l_ki = 0.0;
    l_kd = 0.0;
  }
  Pi = Pi + l_ki * e * time;
  if (Pi > PID_INTEGRAL_MAX) {
    Pi = PID_INTEGRAL_MAX;
  } else if (Pi < -PID_INTEGRAL_MAX) {
    Pi = -PID_INTEGRAL_MAX;
  }
  // Discrete derivative term: Kd * d(error)/dt. Previously this was
  // (l_kd * e - e_prev) / time, which mixed a gain-scaled current error with an
  // unscaled previous error - mathematically wrong. Guard against time == 0 (two
  // loop iterations within the same millisecond) so enabling D can't divide by 0.
  float D = (time > 0.0f) ? (l_kd * (e - e_prev) / time) : 0.0f;
  //float change_angle = P + Pi + D;
  float change_angle = P+Pi;
  e_prev = e;

  if (loopCount >= 100) {
    loopCount = 0;
    // char buffer[100];
    // sprintf(buffer, "PID: target: %.2f, current: %.2f, changeAngle=: %.2f, time: %.2f Pi: %.2f P %.2f\n", target, current, change_angle, time, Pi,P);
    // DEBUG_PRINT(buffer);
  } else {
    loopCount++;
  }
  return -change_angle;
}
