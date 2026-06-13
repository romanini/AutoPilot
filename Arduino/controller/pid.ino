#include <Wire.h>

// Clamp on the integral term so it can't wind up without bound while fighting a
// current or a mechanical stop. Tunable: this caps the integral's steering
// authority relative to the proportional term (P = Kp * e, |e| <= 180).
#define PID_INTEGRAL_MAX 10.0

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
  DEBUG_PRINTLN("PID all setup.");
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
