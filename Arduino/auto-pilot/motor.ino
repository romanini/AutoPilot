#include <Wire.h>
#include "calibrate.h"

#define MAX_MOTOR_PLUS 255
#define MAX_MOTOR_NEG 255
#define MOTOR_NEG_PIN 2
#define MOTOR_PLUS_PIN 3
#define DIRECTION_POSITIVE "starbord"
#define DIRECTION_NEGATIVE "port"

int motor_stop_time_mills = 0;
int wheel = 0;

void setup_motor() {
  Wire.begin();
  pinMode(MOTOR_PLUS_PIN, OUTPUT);
  pinMode(MOTOR_NEG_PIN, OUTPUT);
  analogWrite(MOTOR_PLUS_PIN, 0);
  analogWrite(MOTOR_NEG_PIN, 0);
  Serial.println("Motor all setup.");
}

void start_motor(AutoPilot& autoPilot, int run_millis) {
  analogWrite(MOTOR_PLUS_PIN, 0);
  analogWrite(MOTOR_NEG_PIN, 0);
  int direction = 0;
  if (run_millis > 0) {
    analogWrite(MOTOR_PLUS_PIN, MAX_MOTOR_PLUS);
    direction = 1;
  } else if (run_millis < 0) {
    analogWrite(MOTOR_NEG_PIN, MAX_MOTOR_NEG);
    direction = -1;
  }
  // if the direction is negative that means left/starbord but for
  // the stop time we need have it always in the future so multiply
  // run_millis by direction to always get a positive number
  autoPilot.setMotor(millis() + (run_millis * direction), direction);
}

void stop_motor(AutoPilot& autoPilot) {
  analogWrite(MOTOR_PLUS_PIN, 0);
  analogWrite(MOTOR_NEG_PIN, 0);
  autoPilot.setMotor(0, 0);
  autoPilot.setMotorLastRunTime(millis());
}

/*
 * See if the motor is running and needs to stop
 */
void check_motor(AutoPilot& autoPilot) {
  unsigned int cur_millis = millis();
  // if we have a stop time we are/should be running the motor
  if (autoPilot.getMotorStopTime()) {
    // if the stop time is in the past, it's time to stop the motor
    if (autoPilot.getMotorStopTime() < cur_millis) {
      stop_motor(autoPilot);
    }
    // we don't have a stop time so as long as we have not run the motor
    // within MIN_MOTOR_OFF_TIME
  } else if ((cur_millis - autoPilot.getMotorLastRunTime()) > MIN_MOTOR_OFF_TIME) {
    if (autoPilot.getBearingCorrection() != 0) {
      int run_millis = floor(autoPilot.getBearingCorrection() * MILLIS_PER_DEGREE_CORRECTION);
      start_motor(autoPilot, run_millis);
    }
  }
}