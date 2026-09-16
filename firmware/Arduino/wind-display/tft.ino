#include <Preferences.h>
#include "tft.h"

// Namespace matches the pattern already used for persisted settings elsewhere
// in the project (controller/pid.ino, rudder/angle.ino).
#define TFT_NVS_NAMESPACE "display"
#define TFT_NVS_KEY "tft"

// STRAP_SAMPLES / STRAP_LOW_THRESHOLD now live in tft.h - see the note there.

// What the last detection saw, kept so report_panel() can print it long after
// the boot-time log line has been lost.  On native USB CDC that is the normal
// case, not an edge case: nothing written before the host enumerates is ever
// seen, and setup_screen() runs a couple of seconds before a laptop plugged in
// at the bench has opened a monitor.
static TftStrap strap = {TFT_AUTO, TFT_AUTO, 0, 0};

const TftStrap &tft_strap() { return strap; }

const char *tft_name(TftType type) {
  switch (type) {
    case TFT_HX8357: return "HX8357";
    case TFT_ST7365: return "ST7365P";
    default:           return "auto";
  }
}

void set_tft_override(TftType type) {
  Preferences prefs;
  prefs.begin(TFT_NVS_NAMESPACE, false);
  prefs.putUChar(TFT_NVS_KEY, (uint8_t)type);
  prefs.end();
  DEBUG_PRINT("Panel override stored: ");
  DEBUG_PRINTLN(tft_name(type));
}

TftType detect_tft() {
  // An operator override beats the hardware every time - that is the point of
  // it.  A unit whose strap resistor is wrong can be corrected in the field.
  Preferences prefs;
  prefs.begin(TFT_NVS_NAMESPACE, true);
  uint8_t forced = prefs.getUChar(TFT_NVS_KEY, TFT_AUTO);
  prefs.end();

  strap.stored = TFT_AUTO;
  strap.lows = 0;
  strap.samples = 0;

  if (forced == TFT_HX8357 || forced == TFT_ST7365) {
    DEBUG_PRINT("Panel: forced by NVS override to ");
    DEBUG_PRINTLN(tft_name((TftType)forced));
    strap.stored = (TftType)forced;
    strap.detected = (TftType)forced;
    return (TftType)forced;
  }

  // Strap.  Reading SPI_MISO as a GPIO only works before SPI.begin() claims
  // the pin, which is why setup_screen() calls this first.
  pinMode(SPI_MISO, INPUT_PULLUP);
  delay(2); // settle through R12 (4k7) and the internal pull-up
  uint8_t lows = 0;
  for (uint8_t i = 0; i < STRAP_SAMPLES; i++) {
    if (digitalRead(SPI_MISO) == LOW) lows++;
    delayMicroseconds(200);
  }
  pinMode(SPI_MISO, INPUT);

  TftType type = (lows >= STRAP_LOW_THRESHOLD) ? TFT_ST7365 : TFT_HX8357;
  strap.lows = lows;
  strap.samples = STRAP_SAMPLES;
  strap.detected = type;

  DEBUG_PRINT("Panel: strap ");
  DEBUG_PRINT(lows);
  DEBUG_PRINT("/");
  DEBUG_PRINT(STRAP_SAMPLES);
  DEBUG_PRINT(" low -> ");
  DEBUG_PRINTLN(tft_name(type));

  // Neither cleanly pulled nor cleanly floating means a marginal R12 or a
  // flaky MISO connection.  The verdict above still stands, but say so - this
  // is exactly the fault the sample count exists to surface.
  if (lows > 2 && lows < STRAP_LOW_THRESHOLD) {
    DEBUG_PRINTLN("WARNING: panel strap reading is marginal, check R12/MISO");
  }

  return type;
}
