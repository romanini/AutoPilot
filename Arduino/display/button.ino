
/* 
 * Note the buttons must be connected with the other side of the switch to GROUND. There is
 * a built in pull-up 'resistor' on each input, but no pull-down resistor capability.
 */

#define ADJUSTMENT_AMOUNT_SHORT 1.0
#define ADJUSTMENT_AMOUNT_LONG 10.0
#define ADJUSTMENT_AMOUNT_TACK 90.0
#define TACK_REQUEST_TIMEOUT 30000

// Hold MODE for this long while navigation is disabled to arm relay
// auto-tune (autotune.ino); release before AUTOTUNE_READY_TIMEOUT to start it
// with the enable/disable button, same pattern as TACK above.
#define AUTOTUNE_ARM_HOLD_TIME 5000
#define AUTOTUNE_READY_TIMEOUT 30000

#define PORT_ADJUST_BUTTON_PIN D3
#define STARBORD_ADJUST_BUTTON_PIN D2
#define NAVIGATION_DISABLE_BUTTON_PIN D4
#define MODE_BUTTON_PIN D5
#define TACK_BUTTON_PIN D6

#define BEEP_PIN D7
#define BEEP_INTERVAL 25
#define BEEP_HOLD_INTERVAL 50
#define BEEP_TACK_INTERVAL 500

#define BUTTON_HOLD_TIME 1000

const int button_pins[] = { PORT_ADJUST_BUTTON_PIN, NAVIGATION_DISABLE_BUTTON_PIN, MODE_BUTTON_PIN, STARBORD_ADJUST_BUTTON_PIN, TACK_BUTTON_PIN };
const int num_buttons = sizeof(button_pins) / sizeof(button_pins[0]);

unsigned long beep_on_time = 0;
unsigned long beep_off_time = 0;
bool beep_state = LOW;

unsigned long button_press_times[num_buttons];
unsigned long button_release_times[num_buttons];
bool button_pressed_states[num_buttons];

bool beep_short_triggered[num_buttons];
bool beep_long_triggered[num_buttons];
bool autotune_armed_triggered[num_buttons];  // has the 5s arm-hold beep already fired for this press?

void setup_button() {
  for (int pin = 0; pin < num_buttons; pin++) {
    // setup the button
    pinMode(button_pins[pin], INPUT_PULLUP);
    button_pressed_states[pin] = false;
    beep_short_triggered[pin] = false;
    beep_long_triggered[pin] = false;
    autotune_armed_triggered[pin] = false;
  }

  pinMode(BEEP_PIN, OUTPUT);
  digitalWrite(BEEP_PIN, LOW);

  DEBUG_PRINTLN("Button setup!");
}

void set_beep(unsigned long duration) {
  beep_on_time = millis();
  beep_off_time = beep_on_time + duration;
  beep_state = HIGH;
  digitalWrite(BEEP_PIN, beep_state);
  DEBUG_PRINT("Set Beep on for ");
  DEBUG_PRINTLN(duration);
}

void update_beep() {
  unsigned long currentTime = millis();
  if (beep_state == HIGH && currentTime >= beep_off_time) {
    beep_state = LOW;
    digitalWrite(BEEP_PIN, beep_state);
    DEBUG_PRINTLN("Beep Off");
  }
}

void update_tack() {
  unsigned long currentTime = millis();
  unsigned long tackRequested = autoPilot.getTackRequested();
  if (tackRequested > 0 && currentTime >= tackRequested + TACK_REQUEST_TIMEOUT) {
    autoPilot.cancelTackRequested();
    DEBUG_PRINTLN("Tack Reset");
  }
}

// Local (display-side) copy of the "ready" arm timeout - the controller runs
// its own independent watchdog (autotune_check_ready_timeout in autotune.ino)
// in case this packet never arrives, but we don't wait to hear back before
// reverting the display's own optimistic state, same as every other command.
void update_autotune_ready() {
  unsigned long currentTime = millis();
  unsigned long readyAt = autoPilot.getAutoTuneReadyAt();
  if (autoPilot.getAutoTuneState() == 1 && readyAt > 0 && currentTime >= readyAt + AUTOTUNE_READY_TIMEOUT) {
    autoPilot.cancelAutoTune();
    DEBUG_PRINTLN("Auto-tune ready timed out, reverting");
  }
}

void button_pressed(int pin) {
  // if we are not connected then buttons are useless
  if (autoPilot.isConnected()) {
    switch (button_pins[pin]) {
      case PORT_ADJUST_BUTTON_PIN:
      case STARBORD_ADJUST_BUTTON_PIN:
      case TACK_BUTTON_PIN:
        // no action if we are in mode = 0 or disabled navigation
        if (autoPilot.getMode() == 0 || !autoPilot.isNavigationEnabled()) {
          // the button press didn't happen
          return;
        }
        break;
      case MODE_BUTTON_PIN:
        // Normally mode-toggle needs navigation on. The one exception: holding
        // MODE while disabled (and idle) arms relay auto-tune, so let that
        // press register too - but not if a tune is already armed/running.
        if (!autoPilot.isNavigationEnabled() && autoPilot.getAutoTuneState() != 0) {
          // the button press didn't happen
          return;
        }
        break;
    }
    button_press_times[pin] = millis();
    button_pressed_states[pin] = true;
    DEBUG_PRINT("Button Released ");
    DEBUG_PRINTLN(pin);
  }
}

void button_release(int pin) {
  unsigned long current_time = millis();
  button_release_times[pin] = current_time;
  button_pressed_states[pin] = false;
  beep_short_triggered[pin] = false;
  beep_long_triggered[pin] = false;
  autotune_armed_triggered[pin] = false;

  unsigned long press_duration = current_time - button_press_times[pin];

  DEBUG_PRINT("Button ");
  DEBUG_PRINT(pin);
  DEBUG_PRINT(" released after ");
  DEBUG_PRINT(press_duration);
  DEBUG_PRINTLN(" ms");

  float adjustment = 0.0;
  if (button_pins[pin] == PORT_ADJUST_BUTTON_PIN || button_pins[pin] == STARBORD_ADJUST_BUTTON_PIN) {
    // PORT and STARBORD adjust can do click, or hold for different adjust values and they can be used in conjunction with TACK button
    if (autoPilot.isTackRequested()) {
      DEBUG_PRINT("Button ");
      DEBUG_PRINT(pin);
      DEBUG_PRINTLN(" Clicked - Tacking");
      set_beep(BEEP_TACK_INTERVAL);
      adjustment = ADJUSTMENT_AMOUNT_TACK;
    } else {
      unsigned long pressDuration = button_release_times[pin] - button_press_times[pin];
      if (pressDuration < BUTTON_HOLD_TIME) {
        DEBUG_PRINT("Button ");
        DEBUG_PRINT(pin);
        DEBUG_PRINTLN(" Clicked");
        set_beep(BEEP_INTERVAL);
        adjustment = ADJUSTMENT_AMOUNT_SHORT;
      } else {
        DEBUG_PRINT("Button ");
        DEBUG_PRINT(pin);
        DEBUG_PRINTLN(" Held");
        //set_beep(BEEP_HOLD_INTERVAL);
        adjustment = ADJUSTMENT_AMOUNT_LONG;
      }
    }
  } else {
    DEBUG_PRINT("Button ");
    DEBUG_PRINT(pin);
    DEBUG_PRINTLN(" Clicked");
    set_beep(BEEP_INTERVAL);
  }

  switch (button_pins[pin]) {
    case PORT_ADJUST_BUTTON_PIN:
      if (autoPilot.getMode() > 0) {
        if (autoPilot.getMode() == 2) {
          set_mode(1);
          // delay(1000);
        }
        adjustment *= -1.0;
        adjust_heading(adjustment);
        DEBUG_PRINT("Port Adjust ");
        DEBUG_PRINTLN(adjustment);
        if (autoPilot.isTackRequested()) {
          DEBUG_PRINTLN("Clearing Tack");
          autoPilot.cancelTackRequested();
        }
      }
      break;
    case STARBORD_ADJUST_BUTTON_PIN:
      if (autoPilot.getMode() > 0) {
        if (autoPilot.getMode() == 2) {
          set_mode(1);
          // delay(1000);
        }
        adjust_heading(adjustment);
        DEBUG_PRINT("Starbord Adjust ");
        DEBUG_PRINTLN(adjustment);
        if (autoPilot.isTackRequested()) {
          autoPilot.cancelTackRequested();
        }
      }
      break;
    case NAVIGATION_DISABLE_BUTTON_PIN: {
      int atState = autoPilot.getAutoTuneState();
      if (atState == 2) {
        // Auto-tuning: this button is the abort switch.
        DEBUG_PRINTLN("Aborting auto-tune");
        send_autotune(0);
      } else if (atState == 1) {
        // Ready/armed: this button fires the tune.
        DEBUG_PRINTLN("Starting auto-tune");
        send_autotune(2);
      } else if (autoPilot.isNavigationEnabled()) {
        set_navigation(0);
        DEBUG_PRINTLN("Disabling Navigation");
      } else {
        set_navigation(1);
        DEBUG_PRINTLN("Enabling Navigation");
      }
      if (autoPilot.isTackRequested()) {
        autoPilot.cancelTackRequested();
      }
      DEBUG_PRINTLN("Navigation on/off Button Pressed");
      break;
    }
    case MODE_BUTTON_PIN:
      if (!autoPilot.isNavigationEnabled()) {
        // Disabled: the only thing a MODE press can do here is arm auto-tune,
        // and only if it was held the full AUTOTUNE_ARM_HOLD_TIME (the beep in
        // check_button_press_diuration told the operator to release now).
        if (press_duration >= AUTOTUNE_ARM_HOLD_TIME && autoPilot.getAutoTuneState() == 0) {
          DEBUG_PRINTLN("Arming auto-tune");
          send_autotune(1);
        }
      } else {
        if (autoPilot.getMode() == 1) {
          if (autoPilot.isWaypointSet()) {
            set_mode(2);
            DEBUG_PRINTLN("GPS Mode");
          }
        } else if (autoPilot.getMode() == 2) {
          set_mode(1);
          DEBUG_PRINTLN("Compass Mode");
        }
        if (autoPilot.isTackRequested()) {
          autoPilot.cancelTackRequested();
        }
      }
      DEBUG_PRINTLN("Mode Button Pressed");
      break;
    case TACK_BUTTON_PIN: {
      unsigned long tr = autoPilot.getTackRequested();
      bool itr = autoPilot.isTackRequested();
      DEBUG_PRINT("Tack Request time ");
      DEBUG_PRINTLN(tr);
      DEBUG_PRINT("isTackRequested() ");
      DEBUG_PRINTLN(itr);
      if (autoPilot.isTackRequested()) {
        autoPilot.cancelTackRequested();
      } else {
        // if we are navigating towards a waypoint but we request a tack switch to compass mode
        // so that we can adjust the heading by 90 degrees
        if (autoPilot.getMode() == 2) {
          set_mode(1);
          //delay(1000);
        }
        unsigned long t = millis();
        autoPilot.setTackRequested(t);
        DEBUG_PRINT("Tack Requested ");
        DEBUG_PRINTLN(t);
      }
      DEBUG_PRINTLN("Tack Button Pressed");
      break;
    }
  }
}

void check_button_press_diuration(int pin) {
  unsigned long press_duration = millis() - button_press_times[pin];

  if (button_pins[pin] == PORT_ADJUST_BUTTON_PIN || button_pins[pin] == STARBORD_ADJUST_BUTTON_PIN) {
    if (press_duration >= BUTTON_HOLD_TIME && !beep_long_triggered[pin]) {
      DEBUG_PRINT("Button ");
      DEBUG_PRINT(pin);
      DEBUG_PRINTLN(" Long Pressing");
      set_beep(BEEP_HOLD_INTERVAL);
      beep_long_triggered[pin] = true;
    } else if (press_duration >= 25 && press_duration < 100 && !beep_short_triggered[pin]) {
      // DEBUG_PRINT("Button ");
      // DEBUG_PRINT(pin);
      // DEBUG_PRINTLN(" Pressed <>");
      // set_beep(BEEP_INTERVAL);
      // beep_short_triggered[pin] = true;
    }
  } else if (button_pins[pin] == MODE_BUTTON_PIN && !autoPilot.isNavigationEnabled()) {
    // Holding MODE for AUTOTUNE_ARM_HOLD_TIME while disabled arms relay
    // auto-tune - beep once (same as the adjust buttons' hold-to-beep above)
    // to tell the operator to release now, which is what fires the action.
    if (press_duration >= AUTOTUNE_ARM_HOLD_TIME && !autotune_armed_triggered[pin]) {
      DEBUG_PRINTLN("MODE held while disabled - release to arm auto-tune");
      set_beep(BEEP_HOLD_INTERVAL);
      autotune_armed_triggered[pin] = true;
    }
  } else {
    if (press_duration >= 25 && !beep_short_triggered[pin]) {
      // DEBUG_PRINT("Button ");
      // DEBUG_PRINT(pin);
      // DEBUG_PRINTLN(" Pressed");
      // set_beep(BEEP_INTERVAL);
      // beep_short_triggered[pin] = true;
      // beep_long_triggered[pin] = true;
    }
  }
}

void check_button() {
  update_beep();
  update_tack();
  update_autotune_ready();
  for (uint8_t pin = 0; pin < num_buttons; pin++) {
    int buttonState = digitalRead(button_pins[pin]);

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