/*!
 * @file Adafruit_ST7365.h
 *
 * Driver for the Sitronix ST7365P in 4-line SPI mode, as fitted to the VIEWE
 * UEED035HV-RX40-L001A 3.47" 320x480 transflective panel on the AP-LCD-Carrier
 * board.
 *
 * Vendored into the sketch rather than pulled from sketch.yaml because no
 * maintained Adafruit-GFX ST7365P driver exists.  It is a thin Adafruit_SPITFT
 * subclass, so it presents exactly the same API as Adafruit_HX8357 and the two
 * are interchangeable behind an Adafruit_SPITFT pointer.
 *
 * Every value here was confirmed on the bench with Arduino/st7365_bringup -
 * see circuit/Display/LCD-Carrier/README.md.  Two of them are counter-
 * intuitive and must not be "corrected" without re-testing:
 *
 *  - INVON is REQUIRED.  The module spec says "Transflective Normally BLACK",
 *    which reads like it should mean INVOFF, but that phrase describes the LC
 *    cell rather than the polarity of the data path.  With INVOFF, white
 *    paints black and black paints white.
 *
 *  - COLMOD 0x55 (RGB565) genuinely works over 4-line SPI.  This part is
 *    usually described as "ILI9488 compatible", and the ILI9488 cannot do
 *    16 bpp over SPI - it forces 3 bytes/pixel.  The ST7365P documents 16 bpp
 *    for the 4-line interface in section 8.4.2.3, and it was verified here.
 *    That is why the display sketch's existing 16-bit GFXcanvas1 drawing code
 *    carries over unchanged.
 *
 * Register READS are not supported by this driver.  The panel does not appear
 * to drive SDO for the status registers (a MADCTL write/readback does not
 * return what was written), so readcommand8() must not be used.
 */

#ifndef _ADAFRUIT_ST7365_H
#define _ADAFRUIT_ST7365_H

#include <Adafruit_SPITFT.h>

#define ST7365_TFTWIDTH 320  ///< Native panel width, portrait
#define ST7365_TFTHEIGHT 480 ///< Native panel height, portrait

// Datasheet TSCYCW is 66 ns (~15 MHz).  The bench rig ran clean past 20 MHz,
// but that is one sample at room temperature and this part is rated -20..+70
// in a cockpit, so stay inside the guaranteed limit.  The speed buys nothing:
// the largest single blit in the display sketch is ~21 ms at 15 MHz.
#define ST7365_SPI_HZ 15000000 ///< Default SPI clock

#define ST7365_NOP 0x00     ///< No-op
#define ST7365_SWRESET 0x01 ///< Software reset
#define ST7365_SLPIN 0x10   ///< Enter sleep (~25 uA typ)
#define ST7365_SLPOUT 0x11  ///< Exit sleep
#define ST7365_INVOFF 0x20  ///< Inversion off
#define ST7365_INVON 0x21   ///< Inversion on - REQUIRED, see above
#define ST7365_DISPOFF 0x28 ///< Display off
#define ST7365_DISPON 0x29  ///< Display on
#define ST7365_CASET 0x2A   ///< Column address set
#define ST7365_PASET 0x2B   ///< Page address set
#define ST7365_RAMWR 0x2C   ///< Memory write
#define ST7365_MADCTL 0x36  ///< Memory access control
#define ST7365_COLMOD 0x3A  ///< Interface pixel format

// Colour constants are plain RGB565 and identical to the HX8357 library's, so
// tft.h defines one shared COLOR_* set for both drivers instead.

class Adafruit_ST7365 : public Adafruit_SPITFT {
public:
  Adafruit_ST7365(int8_t cs, int8_t dc, int8_t rst = -1);
#if !defined(ESP8266)
  Adafruit_ST7365(SPIClass *spi, int8_t cs, int8_t dc, int8_t rst = -1);
#endif
  ~Adafruit_ST7365(void);

  void begin(uint32_t freq = 0);
  void setRotation(uint8_t r);
  void invertDisplay(bool i);
  void setAddrWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

  /*!
   * @brief  Enter minimum-power mode: ~25 uA typ / 50 uA max, against ~12 mA
   *         running (datasheet 7.3).  GRAM contents SURVIVE, so waking needs
   *         no repaint - do not invalidate the caller's change-detection
   *         state.  Backlight must be handled separately by the caller.
   */
  void sleep(void);

  /*!
   * @brief  Leave sleep.  Blocks for the datasheet's 120 ms settling time,
   *         after which the previous image is on screen again.
   */
  void wake(void);

private:
  bool asleep = false;
};

#endif // _ADAFRUIT_ST7365_H
