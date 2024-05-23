#include "calibrate.h"

#define MAX_MOTOR_PLUS 255
#define MAX_MOTOR_NEG 255
#define MOTOR_NEG_PIN 3
#define MOTOR_PLUS_PIN 6
#define DIRECTION_POSITIVE "starbord"
#define DIRECTION_NEGATIVE "port"
#define MIN_DEGREE_ADJUST 0.5
#define SMALL_ADJUST 3.0
#define SLOW_CHANGE 3.0
#define INV_RATE_CHANGE (1.0 / 10.0)
#define SMALL_ADJUST_RUN (SLOW_CHANGE * INV_RATE_CHANGE)
#define MILLIS_PER_DEGREE_RATE_CHANGE 100
#define MIN_MOTOR_OFF_TIME 1000
#define MILLIS_PER_DEGREE_CORRECTION 1000

int motor_stop_time_mills = 0;
int wheel = 0;

void setup_motor() {
  pinMode(MOTOR_PLUS_PIN, OUTPUT);
  pinMode(MOTOR_NEG_PIN, OUTPUT);
  analogWrite(MOTOR_PLUS_PIN, 0);
  analogWrite(MOTOR_NEG_PIN, 0);
  Serial.println("Motor all setup.");
}

void start_motor(int run_millis) {
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
  DEBUG_PRINT("Motor Started: ");
  DEBUG_PRINTLN(run_millis * direction);
}

void stop_motor() {
  analogWrite(MOTOR_PLUS_PIN, 0);
  analogWrite(MOTOR_NEG_PIN, 0);
  autoPilot.setMotor(0, 0);
  autoPilot.setMotorLastRunTime(millis());
}

/*
 * See if the motor is running and needs to stop
 */
void check_motor() {
  unsigned int cur_millis = millis();

  if (autoPilot.getMode() == 0) {
    stop_motor();
    return;
  } else if (autoPilot.getMode() == 1) {
    if (autoPilot.getStartMotor() != 0 && !autoPilot.getMotorStarted()) {
      autoPilot.setMotorStarted(true);
      DEBUG_PRINT("Starting Motor: ");
      DEBUG_PRINTLN(autoPilot.getStartMotor());

      start_motor(autoPilot.getStartMotor());
    }
  }

  // if we have a stop time we are/should be running the motor
  if (autoPilot.getMotorStopTime()) {
    // if the stop time is in the past, it's time to stop the motor
    if (autoPilot.getMotorStopTime() < cur_millis) {
      DEBUG_PRINTLN("Motor Stopping");
      stop_motor();
    }
    // we don't have a stop time so as long as we have not run the motor
    // within MIN_MOTOR_OFF_TIME
  } else if ((cur_millis - autoPilot.getMotorLastRunTime()) > MIN_MOTOR_OFF_TIME) {
    float correct = autoPilot.getBearingCorrection();
    int run_millis = 0;
    if (fabs(correct) > MIN_DEGREE_ADJUST) {
      float rate_change = autoPilot.getHeadingShortAverageChange() * -1.0;
      if (fabs(correct) < SMALL_ADJUST && fabs(rate_change) < SLOW_CHANGE) {
        DEBUG_PRINT("rate change ");
        DEBUG_PRINT(rate_change);
        DEBUG_PRINT(":   correction ");
        DEBUG_PRINTLN(correct);
        run_millis = floor(SMALL_ADJUST_RUN * correct);
        DEBUG_PRINT("run_millis : ");
        DEBUG_PRINTLN(run_millis);
      } else {
        float desired_rate_change = correct * INV_RATE_CHANGE * -1.0;
        float diff_rate_change = desired_rate_change - rate_change;
        DEBUG_PRINT("rate change ");
        DEBUG_PRINT(rate_change);
        DEBUG_PRINT(":   correction ");
        DEBUG_PRINT(correct);
        DEBUG_PRINT(" desired rate: ");
        DEBUG_PRINT(desired_rate_change);
        DEBUG_PRINT(" diff rate: ");
        DEBUG_PRINTLN(diff_rate_change);
        run_millis = floor(diff_rate_change * MILLIS_PER_DEGREE_RATE_CHANGE);
        run_millis *= -1;
        DEBUG_PRINT("run_millis : ");
        DEBUG_PRINTLN(run_millis);
      }

      // if (run_millis) {
      //   // start_motor(run_millis * -1.0);
      // }
    }
  }
  //autoPilot.printAutoPilot();
}
