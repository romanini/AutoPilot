#include <Wire.h>
#include <WiFi.h>
#include "AutoPilot.h"

#define DEBUG_ENABLED 1
#if DEBUG_ENABLED
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINT2(x, y) Serial.print(x, y)
#define DEBUG_PRINTLN(x) Serial.println(x)
#define DEBUG_PRINTLN2(x, y) Serial.println(x, y)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINT2(x, y)
#define DEBUG_PRINTLN(x)
#define DEBUG_PRINTLN2(x, y)
#endif

#if defined(ARDUINO_ARCH_ESP32)  // Check if the board is based on the ESP32 architecture (like Arduino Nano ESP32)
// Define the cores
#define CORE_0 0
#define CORE_1 1
#define MULTI_CORE true

#if MULTI_CORE
void control_task(void *pvParameters);
void command_task(void *pvParameters);
#endif
#endif

AutoPilot autoPilot = AutoPilot(&Serial);

void setup() {
  //Initialize serial
#if DEBUG_ENABLED
  int attempts = 0;
  while (!Serial && attempts < 200) { 
    attempts++;
    delay(10); 
  }

  Serial.begin(38400);
#endif

  Wire.begin();
  setup_wifi();
  setup_publish();
  setup_command();
  setup_motor();
  setup_compass();
  setup_gps();
  setup_led();
  setup_motor();
  setup_pid();
  publish_RESET();

  xTaskCreatePinnedToCore(control_task, "Task Control", 10000, NULL, 1, NULL, CORE_0);
  xTaskCreatePinnedToCore(command_task, "Task Command", 10000, NULL, 2, NULL, CORE_1);
  Serial.println("Multi-core setup");

  Serial.println("Setup complete");
#if DEBUG_ENABLED
  Serial.println("Debug enabled");
#else
  Serial.println("Debug disabled");
#endif
}

void loop() {

}

void control_task(void *pvParameters) {
  int last_mills = millis();
  int cur_mills;
  float setpoint;
  float input;
  for (;;) {  // A Task shall never return or exit.
    cur_mills = millis();
    check_compass();

    if (autoPilot.isNavigationEndabled()) {
      if (autoPilot.getMode() == 1) {
        input = autoPilot.getHeading();
      } else {
        input = autoPilot.getCourse();
      }
      setpoint = autoPilot.getBearing();
      // Compute PID output

      float diff_time = cur_mills - last_mills;
      diff_time *= 0.001;
      last_mills = cur_mills;
      float steer_angle = pid_loop(setpoint, input, diff_time);
      motor_control_loop(steer_angle);
    } else {
      float steer_angle = autoPilot.getSteerAngle();
      motor_control_loop(steer_angle);
    }
    //check_pid();
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void command_task(void *pvParameters) {
  for (;;) {  // A Task shall never return or exit.
    check_command();
    check_gps();
    check_led();
    publish_APDAT();
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}


