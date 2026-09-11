/*
 * Cockpit wind display - a bulkhead-mounted head unit that shows the masthead
 * wind as a classic analogue wind dial plus numbers.
 *
 * It is a pure listener. It joins the controller's SoberPilot Wi-Fi as a
 * station, receives the same broadcast ~APDAT telemetry the other head units
 * get, and draws the wind fields from it. It has no buttons, sends no ~APCMD,
 * and never transmits anything at all - which is why its AutoPilot mirror has
 * none of the optimistic-update machinery the button-equipped display needs
 * (see AutoPilot.h).
 *
 * Panel: the same 320x480 SPI TFT as the other head unit, either an Adafruit
 * HX8357 breakout or the VIEWE ST7365P on the LCD carrier, auto-detected at
 * boot from the MISO strap (tft.ino, identical to the display's copy). The
 * difference is orientation: this unit runs PORTRAIT (320 wide, 480 tall),
 * where the display unit runs landscape.
 */

#include "AutoPilot.h"
// Must be included here, in the main sketch file, not just where it is used:
// the Arduino build concatenates every .ino and emits generated prototypes at
// the top, so TftType has to be visible before the prototype for any .ino
// function that takes one. Same reason AutoPilot.h is included above.
#include "tft.h"
// Same reason as tft.h above: the generated prototypes are emitted before any
// of the .ino bodies, so every sketch-defined type that appears in a function
// signature has to be included here. ScreenState (screen.h) is returned by
// screen.ino's current_state(); Surface and DialRect (dial.h) are all over
// dial.ino's helpers.
#include "screen.h"
#include "dial.h"
#include <WiFi.h>

#define DEBUG_ENABLED 1
#if DEBUG_ENABLED
// Gate every debug write behind `if (Serial)` (USBCDC's operator bool = host
// connected) so we never enter the USB CDC write path while unplugged - that
// path can deadlock on a physical detach, parking the calling task until reset.
#define DEBUG_PRINT(x) do { if (Serial) Serial.print(x); } while (0)
#define DEBUG_PRINT2(x, y) do { if (Serial) Serial.print(x, y); } while (0)
#define DEBUG_PRINTLN(x) do { if (Serial) Serial.println(x); } while (0)
#define DEBUG_PRINTLN2(x, y) do { if (Serial) Serial.println(x, y); } while (0)
#define DEBUG_PRINTF(...) do { if (Serial) Serial.printf(__VA_ARGS__); } while (0)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINT2(x, y)
#define DEBUG_PRINTLN(x)
#define DEBUG_PRINTLN2(x, y)
#define DEBUG_PRINTF(...)
#endif

// FreeRTOS task cores (the Nano ESP32 is dual-core)
#define CORE_0 0
#define CORE_1 1

void display_task(void *pvParameters);
void network_task(void *pvParameters);

AutoPilot autoPilot = AutoPilot(&Serial);

void setup() {
  Serial.begin(38400);
  // USB-CDC serial: never block on TX. Without this, once the TX buffer fills
  // with no host draining it (USB unplugged), the next Serial.print() blocks and
  // stalls the task that called it. With a 0ms timeout, prints are dropped while
  // unplugged and resume cleanly when a laptop is reconnected - no reset needed.
  Serial.setTxTimeoutMs(0);
  DEBUG_PRINTLN("Start");

  setup_screen();
  setup_wifi();
  setup_subscribe();

  // Same split as the rudder and wind sensor boards: rendering on one core,
  // anything that can block for a long time on the other. A Wi-Fi association
  // attempt inside connect_wifi() sits for up to 10 s per try, and it must not
  // be able to freeze the dial - a needle stuck at its last angle looks exactly
  // like a steady wind, which is the one failure this display must never show.
  // The screen keeps repainting from the mirror throughout, and the mirror is
  // cleared by the receive timeout, so a lost link becomes a visible "NO LINK"
  // rather than a frozen picture.
  xTaskCreatePinnedToCore(display_task, "Task Display", 10000, NULL, 1, NULL, CORE_0);
  xTaskCreatePinnedToCore(network_task, "Task Network", 10000, NULL, 2, NULL, CORE_1);

  DEBUG_PRINTLN("Setup Complete");
#ifdef DEBUG_ENABLED
  DEBUG_PRINTLN("Debug Enabled");
#else
  DEBUG_PRINTLN("Debug Disabled");
#endif
}

void loop() {
}

// 10 Hz is far faster than the 1 Hz telemetry, and all three reasons matter:
// display() is change-driven so the extra polls cost almost nothing; the
// link-lost message appears promptly rather than on the next telemetry tick
// that is never coming; and it is the rate the screen's damping filter runs at,
// which is what turns a once-a-second jump of the needle into a glide.
void display_task(void *pvParameters) {
  for (;;) {  // A Task shall never return or exit.
    display();
    vTaskDelay(DISPLAY_TICK_MS / portTICK_PERIOD_MS);
  }
}

void network_task(void *pvParameters) {
  for (;;) {  // A Task shall never return or exit.
    check_subscription();
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}
