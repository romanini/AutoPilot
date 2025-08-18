
#define MAX_MOTOR_PLUS 255
#define MAX_MOTOR_NEG 255
#define MOTOR_NEG_PIN D5
#define MOTOR_PLUS_PIN D3
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

int last_mills;
int motor_mills;
int direction;
int current_mills;
float current_steer;
float new_curr;
int callCount = 0;

void setup_motor() {
  pinMode(MOTOR_PLUS_PIN, OUTPUT);
  pinMode(MOTOR_NEG_PIN, OUTPUT);
  analogWrite(MOTOR_PLUS_PIN, HIGH);
  analogWrite(MOTOR_NEG_PIN, 0);
  current_steer = 0.0;
  last_mills = 0;
  direction = 0;
  motor_mills = 0;
  current_mills = 0;

  Serial.println("Motor all setup.");
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
  unsigned int cur_mills = millis();
  if (last_mills == 0) {
    last_mills = cur_mills;
    return;
  }

  int new_mills = new_steer_angle * MILLIS_PER_DEGREE_RATE_CHANGE;
  int diff_m = cur_mills - last_mills;
  last_mills = cur_mills;
  current_mills += diff_m * direction;
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
      DEBUG_PRINTLN("move_motor(-1)");
    }  
    if (new_mills > current_mills) {
      move_motor(1);
      DEBUG_PRINTLN("move_motor(1)");
    }  
  }
}
