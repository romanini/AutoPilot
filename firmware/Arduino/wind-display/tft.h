/*
 * Which TFT is fitted to this head unit, and the colour names shared by both.
 *
 * There are two kinds of display board in service:
 *
 *   - the original Adafruit HX8357 breakout behind AP-Button-LED-V2
 *   - the AP-LCD-Carrier with a VIEWE UEED035HV (Sitronix ST7365P), a
 *     transflective panel that stays readable in direct sunlight
 *
 * One firmware binary drives both, so the panel is identified at boot rather
 * than at compile time - otherwise every unit needs its own build and the
 * binaries are easy to mix up in the field.
 *
 * Detection is a hardware strap.  R10 (10k) on the carrier pulls MISO to
 * ground; the HX8357 breakout has no such resistor.  Both panels leave SDO
 * high-Z when CS is high (measured with firmware/Arduino/panel_probe), so with the
 * ESP32's internal pull-up enabled the carrier reads LOW and the HX8357 reads
 * HIGH.  The old units therefore need no modification at all.
 *
 * That divider is tighter than it looks and it is worth knowing which way it
 * fails.  The ESP32-S3's internal pull-up is specified only as a range - 45k
 * typical, but as low as 10k - so R10 holds MISO anywhere from 0.6 V (typical,
 * comfortably under the 0.825 V VIL) to 1.65 V (worst case, a solid HIGH).  A
 * carrier therefore mis-reads as an HX8357 rather than the other way round,
 * and the failure is total: the wrong driver's init sequence leaves the panel
 * dark with no error anywhere.  If a board with R10 fitted reports HX8357, the
 * strap is the first suspect, not the panel - report_panel() prints the raw
 * sample count so it can be told apart from a broken MISO joint.
 *
 * A MADCTL write/readback cross-check was tried and abandoned: the ST7365P
 * does not appear to drive SDO for the status registers, so the check returned
 * "no information" on healthy hardware and could not tell a broken strap from
 * normal operation.  The strap measured 16/16 and 0/16 on the two units, so
 * there is no ambiguity for it to resolve.
 */

#ifndef TFT_H
#define TFT_H

#include <Arduino.h>

// Shared by both drivers.  Same pins, same bus - only the controller differs.
#define TFT_CS D10
#define TFT_DC D9
#define TFT_RST -1 // HX8357 ties RST to the board reset; the carrier has an RC

#define SPI_MISO D12 // also the panel-ID strap - see detect_tft()
#define SPI_MOSI D11
#define SPI_SCLK D13

// D8 is BL_PWM - it runs through the button board's 2x10 ribbon to the LCD
// carrier's backlight FET gate (circuit/Display-LCD/). The carrier's 100k
// pull-down holds that gate low, so an ST7365P panel comes up backlit-OFF
// unless something drives this pin; setup_screen() in this sketch does, which
// is the one place this file differs from the display unit's copy. HX8357
// modules are unaffected either way - their backlight is hard-wired on - so
// driving it is safe on both panels.
//
// Still a plain digitalWrite rather than PWM: dimming for night use wants a
// way to ask for it, and this unit deliberately has no buttons.
#define TFT_BL D8

// Values are deliberately non-zero so 0 can mean "not set" in NVS.
enum TftType {
  TFT_AUTO = 0,   ///< no override stored; detect from the strap
  TFT_HX8357 = 1, ///< original Adafruit breakout
  TFT_ST7365 = 2, ///< AP-LCD-Carrier, VIEWE transflective
};

/*!
 * @brief  Identify the fitted panel.
 *
 * MUST be called before SPI.begin(): it samples SPI_MISO as a GPIO, and once
 * the SPI peripheral claims that pin the strap can no longer be read.
 *
 * An override stored in NVS wins over the strap, so a unit that misdetects can
 * be corrected over telnet instead of needing a reflash or an iron.
 */
TftType detect_tft();

/*! @brief  Force a panel type, or TFT_AUTO to go back to the strap. */
void set_tft_override(TftType type);

/*! @brief  Human-readable name, for the boot log. */
const char *tft_name(TftType type);

// The strap is sampled repeatedly rather than once.  An unstrapped line is
// held only by the ESP32's weak internal pull-up, so a single read says
// nothing about margin; the count does.  Measured 16/16 on the carrier and
// 0/16 on an HX8357 unit, so the threshold has enormous room either side.
//
// These live here rather than in tft.ino because screen.ino's report_panel()
// prints against them, and .ino files are concatenated in alphabetical order -
// screen before tft - so a #define in tft.ino is invisible to it.
#define STRAP_SAMPLES 16
#define STRAP_LOW_THRESHOLD 14

/*!
 * @brief  What detect_tft() actually saw, as opposed to what it concluded.
 *
 * The verdict on its own is half an answer when the glass is dark: it cannot
 * distinguish a healthy strap from a marginal one, nor either of those from an
 * NVS override left over from a previous experiment.  All three lead to the
 * same symptom and to completely different fixes.
 */
struct TftStrap {
  TftType stored;   ///< NVS override in force, or TFT_AUTO if none
  TftType detected; ///< what detect_tft() returned
  uint8_t lows;     ///< samples that read LOW
  uint8_t samples;  ///< samples taken; 0 if an override short-circuited them
};

/*! @brief  The last detect_tft() result.  Meaningful only after it has run. */
const TftStrap &tft_strap();

// RGB565.  The HX8357_* and ST7365 colour constants are identical values, so
// the drawing code uses one neutral set and does not care which panel is
// fitted.
static const uint16_t COLOR_BLACK = 0x0000;
static const uint16_t COLOR_BLUE = 0x001F;
static const uint16_t COLOR_RED = 0xF800;
static const uint16_t COLOR_GREEN = 0x07E0;
static const uint16_t COLOR_CYAN = 0x07FF;
static const uint16_t COLOR_MAGENTA = 0xF81F;
static const uint16_t COLOR_YELLOW = 0xFFE0;
static const uint16_t COLOR_WHITE = 0xFFFF;

#endif // TFT_H
