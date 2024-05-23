#include "AutoPilot.h"
#include <WiFiNINA.h>

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

#define DISPLAY_ADDRESS 8
#define DATA_SIZE 300

char serialzied_data[DATA_SIZE];
int receive_count = 0;

AutoPilot autoPilot = AutoPilot(&Serial);

void setup() {
  Serial.begin(38400);
  
  setup_wifi();
  setup_command();
  setup_subscribe();
  
  setup_button();
  setup_screen();

  DEBUG_PRINTLN("Debug is enabled");
}


void loop() {
  // put your main code here, to run repeatedly:
  check_button();
  check_subscription();
  check_voltage();
  display();
}
