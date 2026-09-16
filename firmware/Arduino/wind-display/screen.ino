/*
 * What the wind display shows, and when.
 *
 * dial.ino owns the polar drawing; this file owns panel bring-up, the three
 * screen states, and the numbers below the dial. Everything here is
 * change-driven: each item remembers what it last painted and returns early if
 * nothing moved, so the 10 Hz display task costs almost nothing between the
 * 1 Hz telemetry packets.
 */

#include <SPI.h>
#include "Adafruit_GFX.h"
#include "Adafruit_HX8357.h"
#include "Adafruit_ST7365.h"
#include "tft.h"
#include "dial.h"
#include "screen.h"
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>

// Forward declarations. Arduino's auto-prototype pass runs over the whole
// concatenated sketch, but these are file-static and used above their
// definitions, so declare them explicitly rather than relying on it.
static void draw_status_chrome();
static void draw_text(int16_t left, int16_t top, int16_t w, int16_t h,
                      const GFXfont *font, const char *text, uint16_t color,
                      bool centered);
static void initialize_displayed_values();
static void reset_damping();
#ifndef SETUP_SCREEN_REPLACED
static void run_test_pattern();
static void advance_bus_test();
static void reinit_panel();
#endif

// Per-panel SPI clock, same values and same reasoning as the display unit: the
// HX8357 has always run at 24 MHz here, and the ST7365P is held to its
// datasheet TSCYCW limit of 66 ns (~15 MHz) even though the bench rig ran clean
// past 20 MHz - see circuit/Display/LCD-Carrier/README.md.
static constexpr uint32_t HX8357_SPI_HZ = 24000000;
static constexpr uint32_t ST7365_SPI_HZ_ACTUAL = 15000000;

// Held as a base-class pointer so all the drawing code runs unchanged on either
// panel - both drivers are Adafruit_SPITFT subclasses.
Adafruit_SPITFT *tft = nullptr;
TftType fittedTft = TFT_AUTO;

// Kept for report_panel(). The clock is chosen per panel below, so seeing it
// echoed back is how you confirm which branch actually ran.
static uint32_t tftSpiHz = 0;
static bool backlightOn = false;

// Set from the serial console on the loop task and consumed by display() on
// the display task. Same rule as the sensor boards' UDP callbacks: the
// requesting side only ever sets a flag, because the work belongs to whichever
// task already owns the resource - here, the SPI bus.
static volatile bool testPatternRequested = false;
// A count, not a flag: display() consumes one step per tick, and a bool would
// silently merge keypresses that arrive inside the same 50 ms - so a fast
// operator (or a script) would see steps vanish rather than advance.
static volatile uint8_t busTestPending = 0;
static volatile bool reinitRequested = false;

// True while the bus test owns the pins. display() draws nothing at all in
// this state - it would be fighting the test for the same GPIOs, and the SPI
// peripheral is not even attached to them.
static bool busTestActive = false;
static uint8_t busTestStep = 0;

// What the screen last painted, so nothing is redrawn unnecessarily.
static struct {
  ScreenState state;

  // Angles last handed to the dial, for the redraw deadband.
  float apparentAngle;
  float trueAngle;
  bool trueOk;

  TextCache awa, aws, twa, tws, sog, hdg, temperature;
} disp;

static void initialize_displayed_values() {
  disp.state = SCREEN_UNKNOWN;
  disp.apparentAngle = 0.0f;
  disp.trueAngle = 0.0f;
  disp.trueOk = false;

  disp.awa.valid = false;
  disp.aws.valid = false;
  disp.twa.valid = false;
  disp.tws.valid = false;
  disp.sog.valid = false;
  disp.hdg.valid = false;
  disp.temperature.valid = false;
}

void setup_screen() {
  DEBUG_PRINTLN("Starting Setup Display");

  // MUST come before SPI.begin(): detection samples SPI_MISO as a GPIO to read
  // the panel-ID strap, and the SPI peripheral takes that pin over.
  fittedTft = detect_tft();

  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
  pinMode(TFT_DC, OUTPUT);

  SPI.begin(SPI_SCLK, SPI_MISO, SPI_MOSI, TFT_CS);

  DEBUG_PRINT("Setting up Display: ");
  DEBUG_PRINTLN(tft_name(fittedTft));

  uint32_t freq;
  if (fittedTft == TFT_ST7365) {
    tft = new Adafruit_ST7365(&SPI, TFT_CS, TFT_DC, TFT_RST);
    freq = ST7365_SPI_HZ_ACTUAL;
  } else {
    tft = new Adafruit_HX8357(&SPI, TFT_CS, TFT_DC, TFT_RST);
    freq = HX8357_SPI_HZ;
  }
  tftSpiHz = freq;

  tft->begin(freq);
  delay(20);

  // Backlight on. The LCD carrier's backlight FET gate is held low by its own
  // 100k pull-down, so an ST7365P panel comes up DARK unless something drives
  // this - the display unit gets away with not driving it only because the
  // units in service are HX8357 breakouts, whose backlight is hard-wired on.
  // Driving it high is harmless on those (the pin just runs to the ribbon).
  //
  // Plain digitalWrite rather than PWM for now: dimming for night use is worth
  // having on a cockpit unit, but it needs a way to ask for it, and this unit
  // deliberately has no buttons.
  pinMode(TFT_BL, OUTPUT);
  set_backlight(true);

  // Portrait: 320 wide, 480 tall. This is the one place the wind display
  // differs from the head unit, which runs the same panel at setRotation(1).
  tft->setRotation(0);
  tft->fillScreen(COLOR_BLACK);

  initialize_displayed_values();
  draw_status_chrome();
  draw_dial_chrome();
}

// SETUP_SCREEN_REPLACED is defined by the host renderer
// (firmware/experiments/wind-display-render/main.cpp), which substitutes a
// framebuffer for the panel. Everything below reaches for tft.h and real GPIO,
// so it is firmware-only - the renderer strips setup_screen() textually but
// cannot strip a function that sits outside it.
#ifndef SETUP_SCREEN_REPLACED

// ---------------------------------------------------------------------------
// diagnostics
//
// A dark panel produces no error anywhere: detection picks a driver, that
// driver's init sequence goes out over SPI, every call returns, and the sketch
// carries on drawing a dial nobody can see. So the only way to tell the three
// causes apart - wrong driver, backlight off, bus not reaching the glass - is
// to ask for what was decided and then to paint something that uses none of
// the layout code.
// ---------------------------------------------------------------------------

void set_backlight(bool on) {
  backlightOn = on;
  digitalWrite(TFT_BL, on ? HIGH : LOW);
}

bool backlight_on() { return backlightOn; }

void report_panel() {
  const TftStrap &strap = tft_strap();

  DEBUG_PRINTLN("--- Panel ---");

  DEBUG_PRINT("NVS override:   ");
  if (strap.stored == TFT_AUTO) {
    DEBUG_PRINTLN("none - detecting from the strap");
  } else {
    DEBUG_PRINT(tft_name(strap.stored));
    DEBUG_PRINTLN(" (forced; '0' clears it)");
  }

  DEBUG_PRINT("MISO strap D12: ");
  if (strap.samples == 0) {
    DEBUG_PRINTLN("not sampled - the override above short-circuited it");
  } else {
    DEBUG_PRINT(strap.lows);
    DEBUG_PRINT("/");
    DEBUG_PRINT(strap.samples);
    DEBUG_PRINT(" low - ");
    if (strap.lows >= STRAP_LOW_THRESHOLD) {
      DEBUG_PRINTLN("pulled down, so R10 is present: LCD carrier");
    } else if (strap.lows > 2) {
      // Neither cleanly pulled nor cleanly floating. Worth shouting about: it
      // is the one reading that means the verdict below is a coin toss.
      DEBUG_PRINTLN("MARGINAL - check R10 (10k) and the MISO joint");
    } else {
      DEBUG_PRINTLN("floating, so no R10: HX8357 breakout");
    }
  }

  DEBUG_PRINT("Driver:         ");
  DEBUG_PRINT(tft_name(fittedTft));
  if (tft == NULL) {
    DEBUG_PRINTLN(" - NOT CONSTRUCTED, setup_screen() has not run");
    return;
  }
  DEBUG_PRINT(" at ");
  DEBUG_PRINT(tftSpiHz / 1000000);
  DEBUG_PRINT(" MHz, ");
  DEBUG_PRINT(tft->width());
  DEBUG_PRINT("x");
  DEBUG_PRINT(tft->height());
  DEBUG_PRINT(" rotation ");
  DEBUG_PRINTLN(tft->getRotation());

  DEBUG_PRINT("Backlight D8:   ");
  DEBUG_PRINTLN(backlightOn ? "on" : "OFF");

  // The carrier reaching the wrong verdict is the failure this whole report
  // exists for, and it has exactly one cheap test, so say so rather than
  // leaving it to be worked out with a dark screen in hand.
  if (fittedTft == TFT_HX8357) {
    DEBUG_PRINTLN("If this board IS the LCD carrier, press '2' then 'r' to force ST7365P.");
  }
}

void request_test_pattern() { testPatternRequested = true; }

// Full-screen flat colours, then a repaint from scratch.
//
// This is the one test that separates "the panel never initialised" from "the
// drawing code is painting something wrong": it touches nothing but
// fillScreen, so if the glass stays black through it the fault is below the
// dial entirely - driver, bus or panel - and no amount of reading dial.ino
// will find it.
//
// Runs on the display task, never on the caller's, because it drives SPI and
// the display task is already on that bus at 20 Hz from the other core.
static void run_test_pattern() {
  static const struct {
    uint16_t color;
    const char *name;
  } steps[] = {
      {COLOR_RED, "red"},     {COLOR_GREEN, "green"}, {COLOR_BLUE, "blue"},
      {COLOR_WHITE, "white"}, {COLOR_BLACK, "black"},
  };

  DEBUG_PRINTLN("Test pattern: 5 full-screen fills, ~700 ms each");
  for (uint8_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
    DEBUG_PRINT("  fillScreen ");
    DEBUG_PRINTLN(steps[i].name);
    tft->fillScreen(steps[i].color);
    delay(700);
  }

  // Every cache now describes pixels that are gone, so this has to be a full
  // repaint and not just a state change - including the ring, which is drawn
  // once at boot and would otherwise never come back.
  initialize_displayed_values();
  reset_damping();
  draw_status_chrome();
  draw_dial_chrome();
  DEBUG_PRINTLN("Test pattern done");
}

void request_reinit() { reinitRequested = true; }

// Re-run the panel's bring-up without a reboot.
//
// Worth having separately from 'r' because the interesting experiments are
// physical - reseat the FPC, reflow a joint, re-strap IM - and each one
// otherwise costs a power cycle and a fresh Wi-Fi join to find out whether it
// helped. tft->begin() issues SWRESET, so this is a real re-initialisation of
// the panel and not just a repaint.
static void reinit_panel() {
  DEBUG_PRINT("Re-initialising panel: ");
  DEBUG_PRINTLN(tft_name(fittedTft));

  SPI.begin(SPI_SCLK, SPI_MISO, SPI_MOSI, TFT_CS);
  tft->begin(tftSpiHz);
  delay(20);
  set_backlight(true);
  tft->setRotation(0);
  tft->fillScreen(COLOR_BLACK);

  initialize_displayed_values();
  reset_damping();
  draw_status_chrome();
  draw_dial_chrome();
  DEBUG_PRINTLN("Re-init done");
}

// ---------------------------------------------------------------------------
// bus test
//
// The panel is write-only in practice - the ST7365P does not drive SDO for the
// status registers - so no amount of talking to it proves the wires are there.
// This drives each signal to a static level instead, one at a time, so every
// net can be metered with a DMM at the JST-XH and again at the FPC while the
// board is powered. Static, not a square wave: a meter averages a square wave
// into a meaningless mid-rail number.
//
// It is stepped by hand, one keypress per step, because the operator needs
// both hands and an unhurried probe, not a timer.
// ---------------------------------------------------------------------------

static const struct {
  int8_t pin;
  const char *name;
  const char *where;
} BUS_PINS[] = {
    {TFT_CS, "CS", "U3 pin 3 -> FPC 9"},
    {TFT_DC, "D/C", "U3 pin 2 -> FPC 11"},
    {SPI_SCLK, "SCK", "U3 pin 6 -> FPC 10"},
    {SPI_MOSI, "MOSI", "U3 pin 4 -> FPC 13"},
    {TFT_BL, "BL_PWM", "U3 pin 1 -> R4 -> Q1 gate"},
};
static const uint8_t BUS_PIN_COUNT = sizeof(BUS_PINS) / sizeof(BUS_PINS[0]);

// One step past the last "all" step exits and restores the screen.
#define BUS_STEP_ALL_HIGH BUS_PIN_COUNT
#define BUS_STEP_ALL_LOW (BUS_PIN_COUNT + 1)
#define BUS_STEP_EXIT (BUS_PIN_COUNT + 2)

static void bus_drive(int8_t high_pin, bool all_high, bool all_low) {
  for (uint8_t i = 0; i < BUS_PIN_COUNT; i++) {
    bool level = all_high || (!all_low && BUS_PINS[i].pin == high_pin);
    digitalWrite(BUS_PINS[i].pin, level ? HIGH : LOW);
  }
}

void request_bus_test_step() { busTestPending++; }

static void advance_bus_test() {
  if (!busTestActive) {
    // The SPI peripheral has SCK/MOSI/MISO claimed; digitalWrite on a pin it
    // owns does nothing at all, silently. Hand them back first.
    SPI.end();
    for (uint8_t i = 0; i < BUS_PIN_COUNT; i++) {
      pinMode(BUS_PINS[i].pin, OUTPUT);
      digitalWrite(BUS_PINS[i].pin, LOW);
    }
    busTestActive = true;
    busTestStep = 0;
    DEBUG_PRINTLN("--- Bus test ---");
    DEBUG_PRINTLN("One signal HIGH (3.3V) at a time, everything else LOW.");
    DEBUG_PRINTLN("Meter each at the JST-XH and again at the FPC; 'w' steps on.");
  } else {
    busTestStep++;
  }

  if (busTestStep == BUS_STEP_EXIT) {
    busTestActive = false;
    DEBUG_PRINTLN("Bus test finished - restoring the panel");
    reinit_panel();
    return;
  }

  if (busTestStep == BUS_STEP_ALL_HIGH) {
    bus_drive(-1, true, false);
    DEBUG_PRINTLN("  ALL HIGH  - every signal above should read 3.3V");
  } else if (busTestStep == BUS_STEP_ALL_LOW) {
    bus_drive(-1, false, true);
    DEBUG_PRINTLN("  ALL LOW   - every signal above should read 0V");
  } else {
    bus_drive(BUS_PINS[busTestStep].pin, false, false);
    DEBUG_PRINT("  HIGH: ");
    DEBUG_PRINT(BUS_PINS[busTestStep].name);
    DEBUG_PRINT("  at ");
    DEBUG_PRINTLN(BUS_PINS[busTestStep].where);
  }

  // MISO is an input throughout, so it can be watched for free - and it is the
  // one net already known to be wrong. With the internal pull-up against R10
  // (10k to GND) a healthy carrier reads LOW here.
  pinMode(SPI_MISO, INPUT_PULLUP);
  delay(2);
  DEBUG_PRINT("    MISO (FPC 14) now reads ");
  DEBUG_PRINTLN(digitalRead(SPI_MISO) == LOW ? "LOW  (R10 present)"
                                             : "HIGH (no pull-down seen)");
}

#endif  // !SETUP_SCREEN_REPLACED

// True if this field now reads differently from what is on the glass, and
// records the new state as painted.
static bool text_changed(TextCache *cache, const char *text, uint16_t color,
                         const GFXfont *font) {
  if (cache->valid && cache->color == color && cache->font == font &&
      strncmp(cache->text, text, sizeof(cache->text)) == 0) {
    return false;
  }
  snprintf(cache->text, sizeof(cache->text), "%s", text);
  cache->color = color;
  cache->font = font;
  cache->valid = true;
  return true;
}

// ---------------------------------------------------------------------------
// damping
//
// A first-order lag over the four wind values, stepped once per display task
// tick. See screen.h for what it is for and how to tune it.
// ---------------------------------------------------------------------------

static Damped dampedApparentAngle;
static Damped dampedApparentSpeed;
static Damped dampedTrueAngle;
static Damped dampedTrueSpeed;

// Unprime everything, so the next sample is taken as-is rather than glided to
// from a value that is now meaningless. Called whenever the screen state
// changes - after a link drop the old reading is not a starting point, it is
// stale data.
static void reset_damping() {
  dampedApparentAngle.primed = false;
  dampedApparentSpeed.primed = false;
  dampedTrueAngle.primed = false;
  dampedTrueSpeed.primed = false;
}

// Fraction of the remaining gap closed per tick, from the time constant and the
// display task period. Computed once - expf() is not something to run four
// times a tick for a constant.
static float damping_alpha() {
  static float alpha = -1.0f;
  if (alpha < 0.0f) {
    alpha = (WIND_DAMPING_TAU_MS > 0)
                ? 1.0f - expf(-(float)DISPLAY_TICK_MS / (float)WIND_DAMPING_TAU_MS)
                : 1.0f;
  }
  return alpha;
}

static float damp_scalar(Damped *d, float target) {
  if (!d->primed) {
    d->value = target;
    d->primed = true;
  } else {
    d->value += damping_alpha() * (target - d->value);
  }
  return d->value;
}

// The shortest signed arc from b to a, in (-180, 180].
static float angle_difference(float a, float b) {
  float difference = fmodf(a - b, 360.0f);
  if (difference > 180.0f) difference -= 360.0f;
  if (difference < -180.0f) difference += 360.0f;
  return difference;
}

// The shortest-arc distance between two angles, for the redraw deadband.
static float angle_gap(float a, float b) {
  return fabsf(angle_difference(a, b));
}

// Angles must be filtered along the SHORTEST ARC, not on the raw numbers.
// Averaging 359 and 1 arithmetically gives 180 - a needle asked to cross the
// bow would instead sweep all the way round the stern to get there. This
// accumulates the signed difference instead, then renormalises.
static float damp_angle(Damped *d, float target) {
  if (!d->primed) {
    d->value = target;
    d->primed = true;
    return d->value;
  }
  d->value = fmodf(d->value + damping_alpha() * angle_difference(target, d->value),
                   360.0f);
  if (d->value < 0.0f) d->value += 360.0f;
  return d->value;
}

// ---------------------------------------------------------------------------
// text helpers
// ---------------------------------------------------------------------------

// Render text into a 1-bit canvas and blit it. Painting the background in the
// same pass is what makes this flicker-free: the old pixels are never visibly
// cleared, they are simply overwritten by the new frame in one SPI window.
static void draw_text(int16_t left, int16_t top, int16_t w, int16_t h,
                      const GFXfont *font, const char *text, uint16_t color,
                      bool centered) {
  GFXcanvas1 canvas(w, h);
  canvas.fillScreen(0);
  if (text != NULL && *text != '\0') {
    canvas.setFont(font);
    canvas.setTextColor(1);
    // Wrapping off. GFX wraps by default, so a string one pixel wider than its
    // canvas silently folds onto a second line *inside the same box* and comes
    // out as two overlapping half-rows rather than as clipped text - a failure
    // that looks like a font bug and is not.
    canvas.setTextWrap(false);
    int16_t bx, by;
    uint16_t bw, bh;
    canvas.getTextBounds(text, 0, 0, &bx, &by, &bw, &bh);
    // getTextBounds reports the box relative to a cursor at the origin, and for
    // a baseline-relative font its top is negative - so subtracting bx/by is
    // what actually puts the glyphs inside the canvas rather than above it.
    int16_t x = centered ? (w - (int16_t)bw) / 2 - bx : -bx;
    canvas.setCursor(x, (h - (int16_t)bh) / 2 - by);
    canvas.print(text);
  }
  tft->drawBitmap(left, top, canvas.getBuffer(), w, h, color, COLOR_BLACK);
}

// Wind angles are shown the way they are read aloud - magnitude off the bow
// plus the side it is on - rather than as a 0-360 bearing. "47S" is a trim
// instruction; "313" needs arithmetic first.
static void format_wind_angle(float angle, char *out, size_t len) {
  float a = fmodf(angle, 360.0f);
  if (a < 0.0f) a += 360.0f;

  int degrees = (int)lroundf(a);
  if (degrees >= 360) degrees -= 360;

  if (degrees == 0) {
    snprintf(out, len, "0");
  } else if (degrees == 180) {
    snprintf(out, len, "180");
  } else if (degrees < 180) {
    snprintf(out, len, "%dS", degrees);
  } else {
    snprintf(out, len, "%dP", 360 - degrees);
  }
}

// ---------------------------------------------------------------------------
// status bar
// ---------------------------------------------------------------------------

// The legend is drawn once: a fat amber wedge and a thin cyan one, the same two
// shapes as the pointers on the dial. It exists because the two pointers are
// otherwise unlabelled, and a wind display that leaves you guessing which
// needle is apparent is worse than one with a single needle.
static void draw_status_chrome() {
  tft->fillTriangle(8, 4, 4, 18, 12, 18, DIAL_APPARENT);
  draw_text(18, 3, 104, 18, &FreeSansBold9pt7b, "APPARENT", DIAL_APPARENT, false);

  tft->fillTriangle(134, 4, 131, 18, 137, 18, DIAL_TRUE);
  draw_text(142, 3, 50, 18, &FreeSansBold9pt7b, "TRUE", DIAL_TRUE, false);

  tft->drawFastHLine(0, STATUS_H - 1, SCREEN_W, 0x2104);
}

static void display_air_temperature(const WindReading &reading) {
  char text[16];
  if (autoPilot.isAirTemperatureOk() && reading.connected) {
    snprintf(text, sizeof(text), "%.1fC", autoPilot.getAirTemperature());
  } else {
    // Blank, not "--": the masthead thermometer failing on its own is a
    // non-event (it is the one sensor on that board whose loss costs nothing),
    // so it should quietly disappear rather than sit there as a fault.
    text[0] = '\0';
  }
  if (!text_changed(&disp.temperature, text, DIAL_DIM, &FreeSansBold9pt7b)) return;
  draw_text(230, 3, 84, 18, &FreeSansBold9pt7b, text, DIAL_DIM, false);
}

// ---------------------------------------------------------------------------
// numbers below the dial
// ---------------------------------------------------------------------------

// One field: a small dim label and a large value, in one column of the panel.
// The label is painted only on the first pass - it never changes - so a value
// update touches nothing but the value.
static void draw_field(TextCache *cache, int16_t x, int16_t top, const char *label,
                       const char *value, uint16_t color, const GFXfont *font) {
  bool first = !cache->valid;
  if (!text_changed(cache, value, color, font)) return;
  if (first) {
    draw_text(x, top, PANEL_COL_W, 14, &FreeSansBold9pt7b, label, DIAL_DIM, true);
  }
  draw_text(x, top + 15, PANEL_COL_W, 38, font, value, color, true);
}

static void display_apparent_numbers(const WindReading &reading) {
  char text[16];

  if (reading.apparentOk) {
    format_wind_angle(reading.apparentAngle, text, sizeof(text));
  } else {
    snprintf(text, sizeof(text), "--");
  }
  draw_field(&disp.awa, PANEL_COL_A_X, PANEL_TOP, "AWA", text, DIAL_APPARENT,
             &FreeSansBold24pt7b);

  if (reading.apparentOk) {
    snprintf(text, sizeof(text), "%.1f", reading.apparentSpeed);
  } else {
    snprintf(text, sizeof(text), "--");
  }
  draw_field(&disp.aws, PANEL_COL_B_X, PANEL_TOP, "AWS kn", text, DIAL_APPARENT,
             &FreeSansBold24pt7b);
}

static void display_true_numbers(const WindReading &reading) {
  // True wind can be unavailable while the apparent wind above it is perfectly
  // good, and there is exactly one reason for that: the controller derives it
  // from SOG, so no GPS fix means no boat speed to subtract. Saying "NO FIX"
  // instead of "--" turns a mystery into a one-word diagnosis.
  const char *unavailable =
      (autoPilot.isWindOk() && reading.connected) ? "NO FIX" : "--";

  char text[16];
  uint16_t color = reading.trueOk ? DIAL_TRUE : DIAL_DIM;
  const GFXfont *font = reading.trueOk ? &FreeSansBold24pt7b : &FreeSansBold12pt7b;

  if (reading.trueOk) {
    format_wind_angle(reading.trueAngle, text, sizeof(text));
  } else {
    snprintf(text, sizeof(text), "%s", unavailable);
  }
  draw_field(&disp.twa, PANEL_COL_A_X, PANEL_TOP + 60, "TWA", text, color, font);

  if (reading.trueOk) {
    snprintf(text, sizeof(text), "%.1f", reading.trueSpeed);
  } else {
    snprintf(text, sizeof(text), "%s", unavailable);
  }
  draw_field(&disp.tws, PANEL_COL_B_X, PANEL_TOP + 60, "TWS kn", text, color, font);
}

// Boat speed and heading, small, at the bottom. They are here because they are
// what turns the wind numbers into decisions - the true wind above is derived
// from this SOG, so seeing them together is what makes an implausible true wind
// explain itself.
//
// Undamped, unlike the wind: both come from the controller already smoothed,
// and neither jitters the way a masthead vane does.
static void display_boat_numbers(const WindReading &reading) {
  char text[16];

  if (autoPilot.hasFix() && reading.connected) {
    snprintf(text, sizeof(text), "SOG %.1f kn", autoPilot.getSpeed());
  } else {
    snprintf(text, sizeof(text), "SOG --");
  }
  if (text_changed(&disp.sog, text, DIAL_LABEL, &FreeSansBold9pt7b)) {
    draw_text(PANEL_COL_A_X, 458, PANEL_COL_W, 18, &FreeSansBold9pt7b, text,
              DIAL_LABEL, true);
  }

  if (reading.connected) {
    snprintf(text, sizeof(text), "HDG %d", (int)lroundf(autoPilot.getHeading()));
  } else {
    snprintf(text, sizeof(text), "HDG --");
  }
  if (text_changed(&disp.hdg, text, DIAL_LABEL, &FreeSansBold9pt7b)) {
    draw_text(PANEL_COL_B_X, 458, PANEL_COL_W, 18, &FreeSansBold9pt7b, text,
              DIAL_LABEL, true);
  }
}

// ---------------------------------------------------------------------------
// the dial
// ---------------------------------------------------------------------------

static ScreenState current_state() {
  if (!autoPilot.isConnected()) return SCREEN_NO_LINK;
  if (!autoPilot.isWindOk()) return SCREEN_NO_WIND;
  return SCREEN_WIND;
}

static void display_dial(ScreenState state, bool stateChanged,
                         const WindReading &reading) {
  switch (state) {
    case SCREEN_NO_LINK:
      // Names the network rather than saying "check wiring", because that is
      // the actionable half: this unit has no controls, so the only thing the
      // operator can do about it is look at the controller.
      if (stateChanged) {
        draw_dial_message("NO LINK", "waiting for SoberPilot", COLOR_RED);
      }
      return;

    case SCREEN_NO_WIND:
      // The link is up, so the controller is fine and the fault is upwind of
      // it - either the masthead board has stopped transmitting or its vane
      // magnet is not being detected. Both are the same trip up the mast, so
      // one message covers them.
      if (stateChanged) {
        draw_dial_message("NO WIND", "masthead not reporting", COLOR_YELLOW);
      }
      return;

    case SCREEN_WIND:
    default:
      break;
  }

  // Repaint only on real movement. With the damping above, a steady wind
  // converges and then stops moving, so this settles to zero redraws instead of
  // chasing vane noise - and while the wind IS moving, the needle glides at the
  // task rate rather than jumping once a second.
  if (!stateChanged && reading.trueOk == disp.trueOk &&
      angle_gap(reading.apparentAngle, disp.apparentAngle) < WIND_REDRAW_DEADBAND_DEG &&
      (!reading.trueOk ||
       angle_gap(reading.trueAngle, disp.trueAngle) < WIND_REDRAW_DEADBAND_DEG)) {
    return;
  }
  disp.apparentAngle = reading.apparentAngle;
  disp.trueAngle = reading.trueAngle;
  disp.trueOk = reading.trueOk;

  // Coming back from a message screen the interior still holds text, and the
  // pointer history was invalidated when it was cleared - so wipe it once here
  // rather than letting the incremental path try to cover pointers that are
  // not there.
  if (stateChanged) {
    clear_dial_interior();
  }

  draw_dial_wind(reading.apparentAngle, true, reading.trueAngle, reading.trueOk);
}

// ---------------------------------------------------------------------------

void display() {
#ifndef SETUP_SCREEN_REPLACED
  // Every one of these drives SPI, so they run here, on the task that already
  // owns the bus, rather than on the console's task on the other core.
  if (busTestPending > 0) {
    busTestPending--;
    advance_bus_test();
  }
  if (busTestActive) return;  // the test owns the pins; draw nothing

  if (reinitRequested) {
    reinitRequested = false;
    reinit_panel();
  }
  if (testPatternRequested) {
    testPatternRequested = false;
    run_test_pattern();
  }
#endif

  ScreenState state = current_state();
  bool stateChanged = (state != disp.state);
  if (stateChanged) {
    // A new state means the previous reading is not something to glide from.
    reset_damping();
  }
  disp.state = state;

  // Step the filter exactly once per tick, here, and hand the result to
  // everything that draws - so the needle and the numbers under it are always
  // the same reading.
  WindReading reading;
  reading.connected = autoPilot.isConnected();
  reading.apparentOk = autoPilot.isWindOk() && reading.connected;
  reading.trueOk = autoPilot.isTrueWindOk() && reading.connected;

  reading.apparentAngle = reading.apparentOk
                              ? damp_angle(&dampedApparentAngle, autoPilot.getWindAngle())
                              : 0.0f;
  reading.apparentSpeed = reading.apparentOk
                              ? damp_scalar(&dampedApparentSpeed, autoPilot.getWindSpeed())
                              : 0.0f;
  reading.trueAngle = reading.trueOk
                          ? damp_angle(&dampedTrueAngle, autoPilot.getTrueWindAngle())
                          : 0.0f;
  reading.trueSpeed = reading.trueOk
                          ? damp_scalar(&dampedTrueSpeed, autoPilot.getTrueWindSpeed())
                          : 0.0f;

  display_dial(state, stateChanged, reading);
  display_apparent_numbers(reading);
  display_true_numbers(reading);
  display_boat_numbers(reading);
  display_air_temperature(reading);
}
