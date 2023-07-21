#include <Wire.h>
#include <WiFiNINA.h>
#include "AutoPilot.h"

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
  setup_display();
  setup_motor();
  setup_compass();
  setup_button();
  setup_gps();
  Serial.println("Setup complete");
}

void loop() {
  uint32_t start_time = millis();
  check_button();
  check_command();
  check_compass();
  check_gps();
  check_motor();
  display();
  // calculage_average_loop_time(millis() - start_time);
  // if (millis() - last_display_loop_average > DISAPLY_AVERAGE_RATE) {
  //   last_display_loop_average = millis();
  //   Serial.print("Loop average runtime: ");
  //   Serial.print(average_loop_time);
  //   Serial.print(" / ");
  //   Serial.println(average_size);
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
