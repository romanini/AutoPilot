#include <Wire.h>
#if defined(ARDUINO_ARCH_SAMD)  // Check if the board is based on the SAMD architecture (like Arduino Nano 33 IoT)
  #include <WiFiNINA.h>
#elif defined(ARDUINO_ARCH_ESP32)  // Check if the board is based on the ESP32 architecture (like Arduino Nano ESP32)
  #include <WiFi.h>
  #include <WiFiUdp.h>
#else
  #error "Unsupported board type. Please use Arduino Nano 33 IoT or Arduino Nano ESP32."
#endif
#include "AutoPilot.h"

#define DEBUG_ENABLED 1
#ifdef DEBUG_ENABLED
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINT2(x,y) Serial.print(x,y)
#define DEBUG_PRINTLN(x) Serial.println(x)
#define DEBUG_PRINTLN2(x,y) Serial.println(x,y)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINT2(x,y)
#define DEBUG_PRINTLN(x)
#define DEBUG_PRINT2(x,y)
#endif

// #define DISAPLY_AVERAGE_RATE 5000
// #define AVERAGE_MAX_SIZE 1000
// int average_size = 0;
// int average_loop_time = 0;
// uint32_t last_display_loop_average = millis();

AutoPilot autoPilot = AutoPilot(&Serial);

void setup() {
  //Initialize serial 
  while (!Serial) { delay(10); }
  Serial.begin(38400);
  Wire.begin();
  setup_wifi();
  setup_publish();
  setup_command();
  setup_motor();
  setup_compass();
  setup_gps();
  setup_led();
  publish_RESET();
  Serial.println("Setup complete");
#ifdef DEBUG_ENABLED
  Serial.println("debug enabled");
#else
  Serial.println("dibug disabled");
#endif
}

void loop() {
  uint32_t start_time = millis();
  check_command();
  check_compass();
  check_gps();
  check_motor();
  check_led();
  publish_APDAT();
}


