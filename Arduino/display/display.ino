#include "AutoPilot.h"
#if defined(ARDUINO_ARCH_SAMD)  // Check if the board is based on the SAMD architecture (like Arduino Nano 33 IoT)
  #include <WiFiNINA.h>
#elif defined(ARDUINO_ARCH_ESP32)  // Check if the board is based on the ESP32 architecture (like Arduino Nano ESP32)
  #include <WiFi.h>
  #include <WiFiUdp.h>
#else
  #error "Unsupported board type. Please use Arduino Nano 33 IoT or Arduino Nano ESP32."
#endif

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
#define DEBUG_PRINT2(x, y)
#endif

#if defined(ARDUINO_ARCH_ESP32)  // Check if the board is based on the ESP32 architecture (like Arduino Nano ESP32)
// Define the cores
#define CORE_0 0
#define CORE_1 1
#define MULTI_CORE true

#if MULTI_CORE
void display_task(void *pvParameters);
void command_task(void *pvParameters);
#endif
#endif

#define SETUP_COMPLETE_BEEP_INTERVAL 25
#define DISPLAY_ADDRESS 8
#define DATA_SIZE 300

AutoPilot autoPilot = AutoPilot(&Serial);

char serialzied_data[DATA_SIZE];
int receive_count = 0;

void setup() {
// #if defined(ARDUINO_ARCH_ESP32)  // Check if the board is based on the ESP32 architecture (like Arduino Nano ESP32)
//   while (!Serial) { delay(10); }
// #endif
  Serial.begin(38400);
  Serial.println("Start");
  setup_screen();
  setup_wifi();
  setup_subscribe();
  setup_command();
  setup_button();

  analogSetAttenuation(ADC_11db); // full 3.3V range
  analogReadResolution(12); // explicitly set 12-bit on ESP32

  xTaskCreatePinnedToCore(display_task, "Task Display", 10000, NULL, 1, NULL, CORE_0);
  xTaskCreatePinnedToCore(command_task, "Task Command", 10000, NULL, 2, NULL, CORE_1);
  Serial.println("Multi-core setup");

  Serial.println("Setup Complete");
  set_beep(SETUP_COMPLETE_BEEP_INTERVAL);
#ifdef DEBUG_ENABLED
  Serial.println("Debug Enabled");
#else
  Serial.println("Debug Disabled");
#endif
}


void loop() {
  // put your main code here, to run repeatedly:
  // check_button();
  // check_voltage();
  // check_subscription();
  // check_command();
  // display();
}

void display_task(void *pvParameters) {
  for (;;) {  // A Task shall never return or exit.
    check_voltage();
    display();
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

void command_task(void *pvParameters) {
  for (;;) {  // A Task shall never return or exit.
    check_button();
    check_subscription();
    check_command();
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}