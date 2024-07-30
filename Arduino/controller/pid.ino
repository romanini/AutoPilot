#include <Wire.h>
#include <PID_v1.h>

// PID tuning parameters
double Kp = 2.0;  // Proportional gain
double Ki = 5.0;  // Integral gain
double Kd = 1.0;  // Derivative gain

// PID variables
double setpoint;  // Desired compass heading
double input;     // Current compass heading
double output;    // PID controller output

// Create PID controller
PID myPID(&input, &output, &setpoint, Kp, Ki, Kd, DIRECT);

// Motor control pins
const int motorPin1 = 3;  // Motor control pin 1
const int motorPin2 = 5;  // Motor control pin 2
const int enablePin = 9;  // PWM enable pin

// Function to initialize motor control pins
void setupMotorPins() {
  pinMode(motorPin1, OUTPUT);
  pinMode(motorPin2, OUTPUT);
  pinMode(enablePin, OUTPUT);
}

// Function to control motor speed and direction
void controlMotor(double speed) {
  if (speed > 0) {
    // Turn right
    digitalWrite(motorPin1, HIGH);
    digitalWrite(motorPin2, LOW);
    analogWrite(enablePin, abs(speed));
  } else if (speed < 0) {
    // Turn left
    digitalWrite(motorPin1, LOW);
    digitalWrite(motorPin2, HIGH);
    analogWrite(enablePin, abs(speed));
  } else {
    // Stop motor
    digitalWrite(motorPin1, LOW);
    digitalWrite(motorPin2, LOW);
  }
}

void setup_pid() {
  // Initialize motor control pins
  setupMotorPins();

  // Initialize serial communication for debugging
  Serial.begin(9600);

  // Set initial compass heading and setpoint
  input = 0.0;
  setpoint = 0.0;

  // Initialize PID controller
  myPID.SetMode(AUTOMATIC);
  myPID.SetOutputLimits(-255, 255);  // Motor speed range
}

void check_pid() {
  if (autoPilot.getMode() > 0) {
    if (autoPilot.getMode() == 1) {
      // TODO do we want to use short average or long average?
      input = autoPilot.getHeadingShortAverage();
    } else {
      input = autoPilot.getCourse();
    }
    setpoint = autoPilot.getBearing();
    // Compute PID output
    myPID.Compute();

    // Control motor based on PID output
    controlMotor(output);

    // Debugging information
    DEBUG_PRINT("Heading: ");
    DEBUG_PRINT(input);
    DEBUG_PRINT(" | Setpoint: ");
    DEBUG_PRINT(setpoint);
    DEBUG_PRINT(" | Output: ");
    DEBUG_PRINTLN(output);
  }
}
