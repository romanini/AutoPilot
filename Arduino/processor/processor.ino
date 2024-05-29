#include <Wire.h>
#include <WiFiNINA.h>
#include "AutoPilot.h"

#define DEBUG_ENABLED true
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
  Serial.begin(38400);
  Wire.begin();
  setup_wifi();
  setup_publish();
  setup_command();
  setup_motor();
  setup_compass();
  setup_gps();
  publish_RESET();
  Serial.println("Setup complete");
}

void loop() {
  uint32_t start_time = millis();
  check_command();
  check_compass();
  check_gps();
  check_motor();
  publish_APDAT();
  // calculage_average_loop_time(millis() - start_time);
  // if (millis() - last_display_loop_average > DISAPLY_AVERAGE_RATE) {
  //   last_display_loop_average = millis();
  //   DEBUG_PRINT("Loop average runtime: ");
  //   DEBUG_PRINT(average_loop_time);
  //   DEBUG_PRINT(" / ");
  //   DEBUG_PRINTLN(average_size);
  // }
}

// void calculage_average_loop_time(int run_time) {
//   if (average_size > 0) {
//     average_loop_time = average_loop_time + ((run_time - average_loop_time) / average_size);
//   } else {
//     average_loop_time = run_time;
//   }
//   if (average_size < AVERAGE_MAX_SIZE) {
//     average_size += 1;
//   }
// }
