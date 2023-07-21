#include <Adafruit_PCF8575.h>

/* Example for 8 input buttons that are connected from the GPIO expander pins to ground.
 * Note the buttons must be connected with the other side of the switch to GROUND. There is
 * a built in pull-up 'resistor' on each input, but no pull-down resistor capability.
 */

#define ADJUSTMENT_AMOUNT 1.0

#define PORT_ADJUST_BUTTON 0
#define STARBORD_ADJUST_BUTTON 1
#define NAVIGATE_MODE_BUTTON 2
#define COMPASS_MODE_BUTTON 3
#define NUMBER_OF_BUTTONS 4
#define PCF8575_ADDRESS 0x20

Adafruit_PCF8575 pcf = Adafruit_PCF8575();

bool button_down[4];
bool buttons_setup = false;

void setup_button() {
  if (!pcf.begin(PCF8575_ADDRESS, &Wire)) {
    Serial.println("Couldn't find PCF8575");
    return;
  }
  for (uint8_t p = 0; p < NUMBER_OF_BUTTONS; p++) {
    pcf.pinMode(p, INPUT_PULLUP);
    button_down[p] = false;
  }
  buttons_setup = true;
}

void check_button() {
  if (buttons_setup) {
    for (uint8_t p = 0; p < NUMBER_OF_BUTTONS; p++) {
      bool pinValue = pcf.digitalRead(p);
      if (!pinValue && !button_down[p]) {
        button_down[p] = true;
        switch (p) {
          case PORT_ADJUST_BUTTON:
            autoPilot.adjustHeadingDesired(ADJUSTMENT_AMOUNT * -1.0);
            Serial.println("Port Adjust");
            break;
          case STARBORD_ADJUST_BUTTON:
            autoPilot.adjustHeadingDesired(ADJUSTMENT_AMOUNT);
            Serial.println("Starbord Adjust");
            break;
          case NAVIGATE_MODE_BUTTON:
            autoPilot.setMode(2);
            Serial.println("Navigate Mode");
            break;
          case COMPASS_MODE_BUTTON:
            autoPilot.setMode(1);
            Serial.println("Compass Mode");
            break;
        }
      } else if (pinValue && button_down[p]) {
        button_down[p] = false;
      }
    }
  }
}
