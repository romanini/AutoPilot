#include <Adafruit_PCF8575.h>

/* Example for 8 input buttons that are connected from the GPIO expander pins to ground.
 * Note the buttons must be connected with the other side of the switch to GROUND. There is
 * a built in pull-up 'resistor' on each input, but no pull-down resistor capability.
 */

#define ADJUSTMENT_AMOUNT_SHORT 1.0
#define ADJUSTMENT_AMOUNT_LONG 10.0
#define ADJUSTMENT_AMOUNT_TACK 90.0

#define PORT_ADJUST_BUTTON_PIN 0
#define COMPASS_MODE_BUTTON_PIN 1
#define NAVIGATE_MODE_BUTTON_PIN 2
#define STARBORD_ADJUST_BUTTON_PIN 3

#define BEEP_PIN 10
#define BEEP_INTERVAL 25
#define BEEP_HOLD_INTERVAL 100
#define BEEP_LONG_INTERVAL 1000

#define PORT_ADJUST_LED_PIN 15
#define RECEIVE_LED_PIN 14
#define COMPASS_MODE_LED_PIN 13
#define STARBORD_ADJUST_LED_PIN 12
#define NAVIGATE_MODE_LED_PIN 11

#define PCF8575_ADDRESS 0x20

#define BUTTON_HOLD_TIME 1000
#define BUTTON_LONG_HOLD_TIME 5000

#define LED_CLICK_TIME 50
#define LED_HOLD_TIME 200
#define LED_LONG_HOLD_TIME 500

#define RECEIVE_FLASH_INTERVAL 25
#define RECEIVE_FLASH_COUNT 5

const int button_pins[] = { PORT_ADJUST_BUTTON_PIN, COMPASS_MODE_BUTTON_PIN, NAVIGATE_MODE_BUTTON_PIN, STARBORD_ADJUST_BUTTON_PIN };
const int led_pins[] = { PORT_ADJUST_LED_PIN, COMPASS_MODE_LED_PIN, NAVIGATE_MODE_LED_PIN, STARBORD_ADJUST_LED_PIN };  // these must be in the same order as the button Pins
const int num_buttons = sizeof(button_pins) / sizeof(button_pins[0]);

Adafruit_PCF8575 pcf = Adafruit_PCF8575();

uint32_t receive_led_flash_mills = millis();
bool receive_led_state = LOW;
int receive_flash_count = 0;

unsigned long beep_on_time = 0;
unsigned long beep_off_time = 0;
bool beep_state = LOW;

unsigned long button_press_times[num_buttons];
unsigned long button_release_times[num_buttons];
bool button_pressed_states[num_buttons];

bool beep_short_triggered[num_buttons];
bool beep_long_triggered[num_buttons];
bool beep_very_long_triggered[num_buttons];

void setup_button() {
  if (!pcf.begin(PCF8575_ADDRESS, &Wire)) {
    Serial.println("Couldn't find PCF8575");
    return;
  }
  for (int pin = 0; pin < num_buttons; pin++) {
    // setup the button
    pcf.pinMode(button_pins[pin], INPUT_PULLUP);
    button_pressed_states[pin] = false;
    beep_short_triggered[pin] = false;
    beep_long_triggered[pin] = false;
    beep_very_long_triggered[pin] = false;

    // setup the LED
    pcf.pinMode(led_pins[pin], OUTPUT);
    pcf.digitalWrite(led_pins[pin], HIGH);
  }

  pcf.pinMode(BEEP_PIN, OUTPUT);
  pcf.digitalWrite(BEEP_PIN, HIGH);

  pcf.pinMode(RECEIVE_LED_PIN, OUTPUT);
  pcf.digitalWrite(RECEIVE_LED_PIN, HIGH);

  Serial.println("Button and LED setup!");
}

void flash_receive_led() {
  receive_flash_count += 1;
  if (receive_flash_count == RECEIVE_FLASH_COUNT) {
    receive_flash_count = 0;
    receive_led_flash_mills = millis();
    receive_led_state = LOW;
    pcf.digitalWrite(RECEIVE_LED_PIN, receive_led_state);
  }
}

void set_beep(unsigned long duration) {
  beep_on_time = millis();
  beep_off_time = beep_on_time + duration;
  beep_state = LOW;
  pcf.digitalWrite(BEEP_PIN, beep_state);
  DEBUG_PRINT("Set Beep on for ");
  DEBUG_PRINTLN(duration);
}

void update_beep() {
  unsigned long currentTime = millis();
  if (beep_state == LOW && currentTime >= beep_off_time) {
    beep_state = HIGH;
    pcf.digitalWrite(BEEP_PIN, beep_state);
    DEBUG_PRINTLN("Beep Off");
  }
}
void button_pressed(int pin) {
  button_press_times[pin] = millis();
  button_pressed_states[pin] = true;
  // light the LED
  pcf.digitalWrite(led_pins[pin], LOW);
}

void button_release(int pin) {
  unsigned long current_time = millis();
  button_release_times[pin] = current_time;
  button_pressed_states[pin] = false;
  beep_short_triggered[pin] = false;
  beep_long_triggered[pin] = false;
  beep_very_long_triggered[pin] = false;

  // turn off LED, yes HIGH is off because it is switched on negative.
  pcf.digitalWrite(led_pins[pin], HIGH);

  unsigned long press_duration = current_time - button_press_times[pin];

  DEBUG_PRINT("Button ");
  DEBUG_PRINT(pin);
  DEBUG_PRINT(" released after ");
  DEBUG_PRINT(press_duration);
  DEBUG_PRINTLN(" ms");

  if (press_duration < 25) {
    DEBUG_PRINT("Button ");
    DEBUG_PRINT(pin);
    DEBUG_PRINTLN(" Clicked");
    set_beep(BEEP_INTERVAL);
  }

  float adjustment = 0.0;
  if (pin == PORT_ADJUST_BUTTON_PIN || pin == STARBORD_ADJUST_BUTTON_PIN) {
    // PORT and STARBORD adjust can do click, hold and long hold for different adjust values
    unsigned long pressDuration = button_release_times[pin] - button_press_times[pin];
    if (pressDuration < BUTTON_HOLD_TIME) {
      adjustment = ADJUSTMENT_AMOUNT_SHORT;
    } else if (pressDuration < BUTTON_LONG_HOLD_TIME) {
      adjustment = ADJUSTMENT_AMOUNT_LONG;
    } else {
      adjustment = ADJUSTMENT_AMOUNT_TACK;
    }
  }

  switch (pin) {
    case PORT_ADJUST_BUTTON_PIN:
      adjustment *= -1.0;
      adjust_heading(adjustment);
      DEBUG_PRINT("Port Adjust ");
      DEBUG_PRINTLN(adjustment);
      break;
    case STARBORD_ADJUST_BUTTON_PIN:
      adjust_heading(adjustment);
      DEBUG_PRINT("Port Adjust ");
      DEBUG_PRINTLN(adjustment);
      break;
    case COMPASS_MODE_BUTTON_PIN:
      if (autoPilot.getMode() != 1) {
        set_mode(1);
        DEBUG_PRINTLN("Compass Mode");
      } else {
        set_mode(0);
        DEBUG_PRINTLN("Mode off");
      }
      break;
    case NAVIGATE_MODE_BUTTON_PIN:
      if (autoPilot.getMode() != 2) {
        set_mode(2);
        DEBUG_PRINTLN("Navigate Mode");
      } else {
        set_mode(0);
        DEBUG_PRINTLN("Mode off");
      }
      break;
  }
}

void update_receive_led() {
  if ((receive_led_state == LOW) && (millis() - receive_led_flash_mills > RECEIVE_FLASH_INTERVAL)) {
    receive_led_state = HIGH;
    pcf.digitalWrite(RECEIVE_LED_PIN, receive_led_state);
  }
}

void check_button_press_diuration(int pin) {
  unsigned long press_duration = millis() - button_press_times[pin];

  if (pin == PORT_ADJUST_BUTTON_PIN || pin == STARBORD_ADJUST_BUTTON_PIN) {
    // Note this looks a bit strange but beep_state == HIGH means it is NOT beeping becasue it is switched on negative pole.
    if (press_duration >= 5000 && press_duration < 6000 && !beep_very_long_triggered[pin]) {
      DEBUG_PRINT("Button ");
      DEBUG_PRINT(pin);
      DEBUG_PRINTLN(" Very Long Pressing");
      set_beep(BEEP_LONG_INTERVAL);
      beep_very_long_triggered[pin] = true;
    } else if (press_duration >= 1000 && press_duration < 2000 && !beep_long_triggered[pin]) {
      DEBUG_PRINT("Button ");
      DEBUG_PRINT(pin);
      DEBUG_PRINTLN(" Long Pressing");
      set_beep(BEEP_HOLD_INTERVAL);
      beep_long_triggered[pin] = true;
    } else if (press_duration >= 25 && press_duration < 100 && !beep_short_triggered[pin]) {
      DEBUG_PRINT("Button ");
      DEBUG_PRINT(pin);
      DEBUG_PRINTLN(" Pressed");
      set_beep(BEEP_INTERVAL);
      beep_short_triggered[pin] = true;
    }
  } else {
    if (press_duration >= 25 && !beep_short_triggered[pin]) {
      DEBUG_PRINT("Button ");
      DEBUG_PRINT(pin);
      DEBUG_PRINTLN(" Pressed");
      set_beep(BEEP_INTERVAL);
      beep_short_triggered[pin] = true;
      beep_long_triggered[pin] = true;
      beep_very_long_triggered[pin] = true;            
    }
  }
}

void check_button() {
  update_receive_led();
  update_beep();
  for (uint8_t pin = 0; pin < num_buttons; pin++) {
    int buttonState = pcf.digitalRead(button_pins[pin]);

    if (buttonState == LOW) {
      if (!button_pressed_states[pin]) {
        button_pressed(pin);
      } else {
        check_button_press_diuration(pin);
      }
    } else {
      if (button_pressed_states[pin]) {
        button_release(pin);
      }
    }
  }
}