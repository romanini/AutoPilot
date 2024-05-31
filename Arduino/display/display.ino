#include "AutoPilot.h"
#include <WiFiNINA.h>
#include <MemoryFree.h>

//#define DEBUG_ENABLED 1
#ifdef DEBUG_ENABLED
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINT2(x, y) Serial.print(x, y)
#define DEBUG_PRINTLN(x) Serial.println(x)
#define DEBUG_PRINTLN2(x, y) Serial.println(x, y)
#define DEBUG_PRINT_CPP(x) serial->print(x);
#define DEBUG_PRINT2_CPP(x, y) Serial->print(x, y)
#define DEBUG_PRINTLN_CPP(x) Serial->println(x)
#define DEBUG_PRINTLN2_CPP(x, y) Serial->println(x, y)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINT2(x, y)
#define DEBUG_PRINTLN(x)
#define DEBUG_PRINT2(x, y)
#define DEBUG_PRINT_CPP(x)
#define DEBUG_PRINT2_CPP(x, y)
#define DEBUG_PRINTLN_CPP(x)
#define DEBUG_PRINT2_CPP(x, y)
#endif

#define DISPLAY_ADDRESS 8
#define DATA_SIZE 300

AutoPilot autoPilot = AutoPilot(&Serial);

char serialzied_data[DATA_SIZE];
int receive_count = 0;

void setup() {
  Serial.begin(38400);

  setup_screen();
  setup_wifi();
  setup_subscribe();
  setup_command();
  setup_button();
  Serial.println("Setup Complete");
#ifdef DEBUG_ENABLED
  Serial.println("Debug Enabled");
#else
  Serial.println("Debug Disabled");
#endif
  DEBUG_PRINTLN("Debug is enabled");
  DEBUG_PRINT("Program starts with ");
  DEBUG_PRINT(freeMemory());
  DEBUG_PRINTLN(" bytes of free memory.");
}


void loop() {
  // put your main code here, to run repeatedly:
  check_button();
  check_voltage();
  check_subscription();
  check_command();
  display();

  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 5000) {  // every 5 seconds
    lastPrint = millis();
    DEBUG_PRINT("Free memory: ");
    DEBUG_PRINTLN(freeMemory());
  }
}
