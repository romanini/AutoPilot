/*!
 * @file Adafruit_ST7365.cpp
 *
 * Sitronix ST7365P, 4-line SPI.  See Adafruit_ST7365.h for the bench-confirmed
 * values and why two of them look wrong but are not.
 */

#include "Adafruit_ST7365.h"

#define MADCTL_MY 0x80  ///< Row address order (bottom to top)
#define MADCTL_MX 0x40  ///< Column address order (right to left)
#define MADCTL_MV 0x20  ///< Row/column exchange
#define MADCTL_ML 0x10  ///< Vertical refresh order
#define MADCTL_RGB 0x00 ///< Red-Green-Blue filter order
#define MADCTL_BGR 0x08 ///< Blue-Green-Red filter order
#define MADCTL_MH 0x04  ///< Horizontal refresh order

/*!
 * @brief  Software-SPI-free constructor using the platform's default SPI.
 */
Adafruit_ST7365::Adafruit_ST7365(int8_t cs, int8_t dc, int8_t rst)
    : Adafruit_SPITFT(ST7365_TFTWIDTH, ST7365_TFTHEIGHT, cs, dc, rst) {}

#if !defined(ESP8266)
/*!
 * @brief  Constructor taking an explicit SPI peripheral, matching the
 *         Adafruit_HX8357 signature so the two are drop-in interchangeable.
 */
Adafruit_ST7365::Adafruit_ST7365(SPIClass *spi, int8_t cs, int8_t dc,
                                 int8_t rst)
    : Adafruit_SPITFT(ST7365_TFTWIDTH, ST7365_TFTHEIGHT, spi, cs, dc, rst) {}
#endif

Adafruit_ST7365::~Adafruit_ST7365(void) {}

/*!
 * @brief  Reset and initialise the panel.
 * @param  freq  SPI bitrate, or 0 for ST7365_SPI_HZ.
 *
 * SWRESET is issued unconditionally rather than relying on a reset pin,
 * because the carrier board has no RST wire - RESET is generated locally by an
 * RC power-on reset (R11/C8).  That covers a cold boot, but an MCU-only
 * restart leaves the cap charged and RESET high, so the panel is still
 * initialised and running.  SWRESET makes both paths identical.
 *
 * Datasheet 9.2.2: SWRESET returns registers to defaults (hence re-sending
 * COLMOD/MADCTL/INVON below) but leaves frame memory intact; wait 5 ms before
 * any command and 120 ms before SLPOUT.  The 150 ms delays cover both.
 */
void Adafruit_ST7365::begin(uint32_t freq) {
  if (!freq)
    freq = ST7365_SPI_HZ;
  initSPI(freq);

  sendCommand(ST7365_SWRESET);
  delay(150);

  sendCommand(ST7365_SLPOUT);
  delay(150);

  uint8_t colmod = 0x55; // 16 bit/pixel, RGB565
  sendCommand(ST7365_COLMOD, &colmod, 1);

  sendCommand(ST7365_INVON); // required on this panel - see the header

  sendCommand(ST7365_DISPON);
  delay(50);

  asleep = false;
  setRotation(0); // establishes MADCTL and _width/_height together
}

/*!
 * @brief  Set orientation.
 *
 * All four values were confirmed on the panel; BGR is correct throughout and
 * RGB is wrong.  Note that MV on its own transposes rather than rotates, so
 * MV|BGR and MY|MV|BGR are mirror images - only the four below are true 90
 * degree steps.  Rotation 1 is the fitted orientation on the AP-LCD-Carrier:
 * landscape with the FPC tail on the left, which is what the display sketch
 * already asks for.
 */
void Adafruit_ST7365::setRotation(uint8_t r) {
  rotation = r & 3;
  uint8_t m;
  switch (rotation) {
  case 0: // portrait, tail at the bottom
    m = MADCTL_MY | MADCTL_BGR; // 0x88
    _width = ST7365_TFTWIDTH;
    _height = ST7365_TFTHEIGHT;
    break;
  case 1: // landscape, tail on the LEFT  <- as fitted
    m = MADCTL_MX | MADCTL_MY | MADCTL_MV | MADCTL_BGR; // 0xE8
    _width = ST7365_TFTHEIGHT;
    _height = ST7365_TFTWIDTH;
    break;
  case 2: // portrait, tail at the top
    m = MADCTL_MX | MADCTL_BGR; // 0x48
    _width = ST7365_TFTWIDTH;
    _height = ST7365_TFTHEIGHT;
    break;
  default: // 3: landscape, tail on the right
    m = MADCTL_MV | MADCTL_BGR; // 0x28
    _width = ST7365_TFTHEIGHT;
    _height = ST7365_TFTWIDTH;
    break;
  }
  sendCommand(ST7365_MADCTL, &m, 1);
}

/*!
 * @brief  Invert relative to the panel's normal state.
 *
 * Note the sense: begin() leaves the panel in INVON because that is what makes
 * colours correct here, so invertDisplay(true) selects INVOFF.  Passing this
 * straight through to INVON/INVOFF, as most drivers do, would make
 * invertDisplay(false) invert the display.
 */
void Adafruit_ST7365::invertDisplay(bool invert) {
  sendCommand(invert ? ST7365_INVOFF : ST7365_INVON);
}

void Adafruit_ST7365::setAddrWindow(uint16_t x1, uint16_t y1, uint16_t w,
                                    uint16_t h) {
  uint16_t x2 = (x1 + w - 1), y2 = (y1 + h - 1);
  writeCommand(ST7365_CASET);
  SPI_WRITE16(x1);
  SPI_WRITE16(x2);
  writeCommand(ST7365_PASET);
  SPI_WRITE16(y1);
  SPI_WRITE16(y2);
  writeCommand(ST7365_RAMWR);
}

void Adafruit_ST7365::sleep(void) {
  if (asleep)
    return;
  sendCommand(ST7365_DISPOFF);
  sendCommand(ST7365_SLPIN);
  delay(5); // datasheet: 5 ms before any further command
  asleep = true;
}

void Adafruit_ST7365::wake(void) {
  if (!asleep)
    return;
  sendCommand(ST7365_SLPOUT);
  delay(120); // datasheet: 120 ms before SLPIN would be legal again
  sendCommand(ST7365_DISPON);
  asleep = false;
  // GRAM survived - deliberately no repaint here.
}
