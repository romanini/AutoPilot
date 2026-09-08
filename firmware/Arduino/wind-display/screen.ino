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
  digitalWrite(TFT_BL, HIGH);

  // Portrait: 320 wide, 480 tall. This is the one place the wind display
  // differs from the head unit, which runs the same panel at setRotation(1).
  tft->setRotation(0);
  tft->fillScreen(COLOR_BLACK);

  initialize_displayed_values();
  draw_status_chrome();
  draw_dial_chrome();
}

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
