#include <Wire.h>
#if defined(ARDUINO_ARCH_SAMD)  // Check if the board is based on the SAMD architecture (like Arduino Nano 33 IoT)
#include <WiFiNINA.h>
#elif defined(ARDUINO_ARCH_ESP32)  // Check if the board is based on the ESP32 architecture (like Arduino Nano ESP32)
#include <WiFi.h>
#else
#error "Unsupported board type. Please use Arduino Nano 33 IoT or Arduino Nano ESP32."
#endif
#include "AutoPilot.h"

#define DEBUG_ENABLED 0
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
#if defined(ARDUINO_ARCH_ESP32)  // Check if the board is based on the ESP32 architecture (like Arduino Nano ESP32)
  while (!Serial) { delay(10); }
#endif
  Serial.begin(38400);
  Wire.begin();
  setup_wifi();
  setup_publish();
  setup_command();
  setup_motor();
  setup_compass();
  setup_gps();
  setup_led();
  setup_motor();
  publish_RESET();

#if defined(ARDUINO_ARCH_ESP32) && MULTI_CORE  // Check if the board is based on the ESP32 architecture (like Arduino Nano ESP32)
  xTaskCreatePinnedToCore(control_task, "Task Control", 10000, NULL, 1, NULL, CORE_0);
  xTaskCreatePinnedToCore(command_task, "Task Command", 10000, NULL, 2, NULL, CORE_1);
  Serial.println("Multi-core setup");
#endif

  Serial.println("Setup complete");
#ifdef DEBUG_ENABLED
  Serial.println("Debug enabled");
#else
  Serial.println("Debug disabled");
#endif
}

void loop() {
#if defined(ARDUINO_ARCH_SAMD) || !MULTI_CORE  // Check if the board is based on the SAMD architecture (like Arduino Nano 33 IoT)
  uint32_t start_time = millis();
  check_command();
  check_compass();
  check_gps();
  check_motor();
  check_led();
  publish_APDAT();
#endif
}

#if defined(ARDUINO_ARCH_ESP32) && MULTI_CORE  // Check if the board is based on the ESP32 architecture (like Arduino Nano ESP32)
void control_task(void *pvParameters) {
  int last_mills=millis();
  int cur_mills;
  float setpoint;
  float input;
  for (;;) {  // A Task shall never return or exit.
    cur_mills=millis();
    check_compass();

    if (autoPilot.getMode() > 0) {
	if (autoPilot.getMode() == 1) 
	{
	    // TODO do we want to use short average or long average?
	    input = autoPilot.getHeadingShortAverage();
	 } else 
         {
	      input = autoPilot.getCourse();
	 }
	    setpoint = autoPilot.getBearing();
	    // Compute PID output

	    float diff_time=cur_mills-last_mills;
	    diff_time *=0.001;
	    last_mills = cur_mills;
	    float steer_angle=pid_loop(setpoint,input,diff_time);
	    motor_control_loop(steer_angle);
    }
    //check_pid();
    vTaskDelay(100 / portTICK_PERIOD_MS);
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

#endif
