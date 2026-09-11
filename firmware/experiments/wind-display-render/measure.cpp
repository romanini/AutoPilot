#include "Arduino.h"
#include "Adafruit_SPITFT.h"
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <cstdio>
int main() {
  Adafruit_SPITFT p(320, 480);
  const char *strings[] = {"APPARENT", "TRUE", "waiting for SoberPilot",
                           "masthead not reporting", "NO LINK", "NO WIND",
                           "NO FIX", "SOG 12.3 kn", "HDG 218", "AWS kn", "180P", "18.4C"};
  const GFXfont *fonts[] = {&FreeSansBold9pt7b, &FreeSansBold12pt7b, &FreeSansBold18pt7b};
  const char *names[] = {"9pt", "12pt", "18pt"};
  for (int f = 0; f < 3; f++) {
    p.setFont(fonts[f]);
    for (auto s : strings) {
      int16_t bx, by; uint16_t bw, bh;
      p.getTextBounds(s, 0, 0, &bx, &by, &bw, &bh);
      printf("%-5s %-24s w=%3u h=%2u\n", names[f], s, bw, bh);
    }
    printf("\n");
  }
}
