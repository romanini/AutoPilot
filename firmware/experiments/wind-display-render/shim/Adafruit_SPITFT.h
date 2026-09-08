#pragma once
#include "Adafruit_GFX.h"
// Host stand-in for the real SPI panel base class: the sketch only ever calls
// Adafruit_GFX primitives through it, so a framebuffer-backed subclass is a
// faithful substitute for layout purposes.
class Adafruit_SPITFT : public Adafruit_GFX {
public:
  uint16_t *fb;
  Adafruit_SPITFT(int16_t w, int16_t h) : Adafruit_GFX(w, h) {
    fb = (uint16_t *)calloc((size_t)w * h, sizeof(uint16_t));
  }
  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (x < 0 || y < 0 || x >= _width || y >= _height) return;
    fb[(size_t)y * _width + x] = color;
  }
  void begin(uint32_t) {}

  // Instrumentation for the "cost" scenario: how many contiguous pushes the
  // dial makes per update, and how many pixels they carry. On the real panel
  // each of these is one SPI window; the whole point of the tile compositing is
  // that a frame is a small number of them and the screen never shows a
  // half-finished state.
  uint32_t blits = 0, blit_pixels = 0;
  void drawRGBBitmap(int16_t x, int16_t y, uint16_t *bitmap, int16_t w, int16_t h) {
    blits++;
    blit_pixels += (uint32_t)w * h;
    Adafruit_GFX::drawRGBBitmap(x, y, bitmap, w, h);
  }
};
