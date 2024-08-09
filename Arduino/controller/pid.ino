#include <Wire.h>

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
  Serial.println("PID all setup.");
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
  float D = (l_kd * e - e_prev) / time;
  //float change_angle = P + Pi + D;
  float change_angle = P+Pi;
  e_prev = e;

  if (loopCount >= 100) {
    loopCount = 0;
    char buffer[100];
    sprintf(buffer, "PID: target: %.2f, current: %.2f, changeAngle=: %.2f, time: %.2f Pi: %.2f P %.2f\n", target, current, change_angle, time, Pi,P);
    DEBUG_PRINT(buffer);
  } else {
    loopCount++;
  }
  return -change_angle;
}
