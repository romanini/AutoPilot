
#define MAX_MOTOR_PLUS 255
#define MAX_MOTOR_NEG 255
#define MOTOR_NEG_PIN D3
#define MOTOR_PLUS_PIN D5
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

// Usable rudder travel, expressed in steer-angle degrees, and the same limit in
// the integrator's "millis" units. Tune MAX_RUDDER_STEER_ANGLE to the actuator:
// it both caps how far an out-of-range command can drive the motor and bounds
// the virtual position so it can't wind up past the mechanical stop.
#define MAX_RUDDER_STEER_ANGLE 45.0
#define MAX_RUDDER_TRAVEL_MILLIS ((int)(MAX_RUDDER_STEER_ANGLE * MILLIS_PER_DEGREE_RATE_CHANGE))

unsigned long last_mills;  // millis() timestamp of the previous motor_control_loop call
int motor_mills;
int direction;
int current_mills;         // virtual rudder position accumulator (signed, clamped); not a timestamp
float current_steer;
float new_curr;
int callCount = 0;

void setup_motor() {
  pinMode(MOTOR_PLUS_PIN, OUTPUT);
  pinMode(MOTOR_NEG_PIN, OUTPUT);
  analogWrite(MOTOR_PLUS_PIN, 0);
  analogWrite(MOTOR_NEG_PIN, 0);
  current_steer = 0.0;
  last_mills = 0;
  direction = 0;
  motor_mills = 0;
  current_mills = 0;

  DEBUG_PRINTLN("Motor all setup.");
}

void move_motor(int direct) {

  analogWrite(MOTOR_PLUS_PIN, 0);
  analogWrite(MOTOR_NEG_PIN, 0);
  if (direct == 1) {
    analogWrite(MOTOR_PLUS_PIN, MAX_MOTOR_PLUS);
    direction = 1;
  } else if (direct == -1) {
    analogWrite(MOTOR_NEG_PIN, MAX_MOTOR_NEG);
    direction = -1;
  } else {
    direction = 0;
  }
  // if the direction is negative that means left/starbord but for
  // the stop time we need have it always in the future so multiply
  // run_millis by direction to always get a positive number
  // autoPilot.setMotor(millis() + (run_millis * direction), direction);
  // DEBUG_PRINT("Motor Started: ");
  // DEBUG_PRINTLN(run_millis * direction);
}

void stop_motor() {
  analogWrite(MOTOR_PLUS_PIN, 0);
  analogWrite(MOTOR_NEG_PIN, 0);


  //autoPilot.setMotor(0, 0);
  //autoPilot.setMotorLastRunTime(millis());
}

void motor_control_loop(float new_steer_angle) {
  unsigned long cur_mills = millis();
  if (last_mills == 0) {
    last_mills = cur_mills;
    return;
  }

  int new_mills = new_steer_angle * MILLIS_PER_DEGREE_RATE_CHANGE;
  // Bound the commanded position to the rudder's physical travel so an
  // out-of-range steer command doesn't drive the motor against a stop forever.
  if (new_mills > MAX_RUDDER_TRAVEL_MILLIS) {
    new_mills = MAX_RUDDER_TRAVEL_MILLIS;
  } else if (new_mills < -MAX_RUDDER_TRAVEL_MILLIS) {
    new_mills = -MAX_RUDDER_TRAVEL_MILLIS;
  }
  // Unsigned subtraction is rollover-safe across the millis() wrap; the per-loop
  // elapsed time is small, so casting the result to int for the direction multiply
  // is safe.
  int diff_m = (int)(cur_mills - last_mills);
  last_mills = cur_mills;
  current_mills += diff_m * direction;
  // Clamp the virtual rudder position to the same travel limit. Without this it
  // integrates past the mechanical stop and then lags on reversal while it
  // "unwinds" phantom travel that the rudder never actually had.
  if (current_mills > MAX_RUDDER_TRAVEL_MILLIS) {
    current_mills = MAX_RUDDER_TRAVEL_MILLIS;
  } else if (current_mills < -MAX_RUDDER_TRAVEL_MILLIS) {
    current_mills = -MAX_RUDDER_TRAVEL_MILLIS;
  }
  if (callCount >= 100) {
    callCount = 0;
    // char buffer[100];
    // sprintf(buffer, "cur %d, new_mils, %d \n", current_mills, new_mills);
    // DEBUG_PRINT(buffer);
  } else {
    callCount++;
  }
  if (abs(current_mills - new_mills) < 100) {
    move_motor(0);
    //DEBUG_PRINTLN("move_motort(0)");
  } else {
    if (new_mills < current_mills) {
      move_motor(-1);
      //DEBUG_PRINTLN("move_motor(-1)");
    }  
    if (new_mills > current_mills) {
      move_motor(1);
      //DEBUG_PRINTLN("move_motor(1)");
    }  
  }
}
