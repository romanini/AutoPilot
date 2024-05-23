#include <Adafruit_PCF8575.h>

/* Example for 8 input buttons that are connected from the GPIO expander pins to ground.
 * Note the buttons must be connected with the other side of the switch to GROUND. There is
 * a built in pull-up 'resistor' on each input, but no pull-down resistor capability.
 */

#define ADJUSTMENT_AMOUNT 1.0

#define PORT_ADJUST_BUTTON 0
#define COMPASS_MODE_BUTTON 1
#define NAVIGATE_MODE_BUTTON 2
#define STARBORD_ADJUST_BUTTON 3

#define NUMBER_OF_BUTTONS 4

#define PORT_ADJUST_LED 15
#define ENABLED_LED 14
#define COMPASS_MODE_LED 13
#define STARBORD_ADJUST_LED 12
#define NAVIGATE_MODE_LED 11

#define PCF8575_ADDRESS 0x20

Adafruit_PCF8575 pcf = Adafruit_PCF8575();

bool button_down[NUMBER_OF_BUTTONS];
bool setup_complete = false;

void setup_button() {
  if (!pcf.begin(PCF8575_ADDRESS, &Wire)) {
    Serial.println("Couldn't find PCF8575");
    return;
  }
  pcf.pinMode(PORT_ADJUST_BUTTON, INPUT_PULLUP);
  button_down[PORT_ADJUST_BUTTON] = false;
  pcf.pinMode(COMPASS_MODE_BUTTON, INPUT_PULLUP);
  button_down[COMPASS_MODE_BUTTON] = false;
  pcf.pinMode(NAVIGATE_MODE_BUTTON, INPUT_PULLUP);
  button_down[NAVIGATE_MODE_BUTTON] = false;
  pcf.pinMode(STARBORD_ADJUST_BUTTON, INPUT_PULLUP);
  button_down[STARBORD_ADJUST_BUTTON] = false;

  pcf.pinMode(PORT_ADJUST_LED, OUTPUT);
  pcf.digitalWrite(PORT_ADJUST_LED, HIGH);
  pcf.pinMode(ENABLED_LED, OUTPUT);
  pcf.digitalWrite(ENABLED_LED, HIGH);
  pcf.pinMode(COMPASS_MODE_LED, OUTPUT);
  pcf.digitalWrite(COMPASS_MODE_LED, HIGH);
  pcf.pinMode(STARBORD_ADJUST_LED, OUTPUT);
  pcf.digitalWrite(STARBORD_ADJUST_LED, HIGH);
  pcf.pinMode(NAVIGATE_MODE_LED, OUTPUT);
  pcf.digitalWrite(NAVIGATE_MODE_LED, HIGH);

  setup_complete = true;
  Serial.println("Button and LED setup!");
}

void check_button() {
  if (setup_complete) {
    switch (autoPilot.getMode()) {
      case 0:
        pcf.digitalWrite(NAVIGATE_MODE_LED, HIGH);
        pcf.digitalWrite(COMPASS_MODE_LED, HIGH);
        break;
      case 1:
        pcf.digitalWrite(NAVIGATE_MODE_LED, HIGH);
        pcf.digitalWrite(COMPASS_MODE_LED, LOW);
        break;
      case 2:
        pcf.digitalWrite(NAVIGATE_MODE_LED, LOW);
        pcf.digitalWrite(COMPASS_MODE_LED, HIGH);
        break;
    }
    for (uint8_t p = 0; p < NUMBER_OF_BUTTONS; p++) {
      bool pinValue = pcf.digitalRead(p);
      if (!pinValue && !button_down[p]) {
        button_down[p] = true;
        switch (p) {
          case PORT_ADJUST_BUTTON:
            pcf.digitalWrite(PORT_ADJUST_LED, LOW);
            adjustHeading(ADJUSTMENT_AMOUNT * -1.0);
            DEBUG_PRINTLN("Port Adjust");
            break;
          case STARBORD_ADJUST_BUTTON:
            pcf.digitalWrite(STARBORD_ADJUST_LED, LOW);
            adjustHeading(ADJUSTMENT_AMOUNT);
            DEBUG_PRINTLN("Starbord Adjust");
            break;
          case NAVIGATE_MODE_BUTTON:
            if (autoPilot.getMode() != 2) {
              setMode(2);
            } else {
              setMode(0);
            }
            DEBUG_PRINTLN("Navigate Mode");
            break;
          case COMPASS_MODE_BUTTON:
            if (autoPilot.getMode() != 1) {
              setMode(1);
            }
            else {
              setMode(0);
            }
            DEBUG_PRINTLN("Compass Mode");
            break;
        }
      } else if (pinValue && button_down[p]) {
        button_down[p] = false;
        switch (p) {
          case PORT_ADJUST_BUTTON:
            pcf.digitalWrite(PORT_ADJUST_LED, HIGH);
            break;
          case STARBORD_ADJUST_BUTTON:
            pcf.digitalWrite(STARBORD_ADJUST_LED, HIGH);
            break;
        }
      }
    }
  }
}
