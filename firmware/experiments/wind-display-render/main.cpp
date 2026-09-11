#include "Arduino.h"
#include "SPI.h"
#include "Adafruit_SPITFT.h"
#include "screen.h"
#include <cstdio>

// ---- stand-ins for the pieces of the sketch that talk to hardware ----------
#define DEBUG_PRINT(x)
#define DEBUG_PRINT2(x, y)
#define DEBUG_PRINTLN(x)
#define DEBUG_PRINTLN2(x, y)
#define DEBUG_PRINTF(...)

SPIClass SPI;

// Scripted telemetry, standing in for the parsed ~APDAT mirror.
struct FakeAutoPilot {
  float windAngle = 47.0f, windSpeed = 14.2f;
  float trueAngle = 62.0f, trueSpeed = 11.8f;
  float sog = 5.6f, heading = 218.0f, airTemp = 18.4f;
  bool connected = true, windOk = true, trueOk = true, fix = true, tempOk = true;

  float getWindAngle() { return windAngle; }
  float getWindSpeed() { return windSpeed; }
  bool isWindOk() { return windOk; }
  float getTrueWindAngle() { return trueAngle; }
  float getTrueWindSpeed() { return trueSpeed; }
  bool isTrueWindOk() { return trueOk; }
  float getSpeed() { return sog; }
  float getHeading() { return heading; }
  bool hasFix() { return fix; }
  float getAirTemperature() { return airTemp; }
  bool isAirTemperatureOk() { return tempOk; }
  bool isConnected() { return connected; }
} autoPilot;

#include "dial.h"

Adafruit_SPITFT *tft = nullptr;

// The sketch's own drawing code, compiled verbatim.
#include "dial.ino"

// dial.ino's fill_pointer is static; re-expose it for the overlap test.
static void fill_pointer_for_test(float a, int r_point, int r_wide, int half_w,
                                  uint16_t color) {
  Surface s = {tft, 0, 0};
  fill_pointer(s, a, r_point, r_wide, half_w, color);
}

// dial.ino's pointer_vertices is static; re-expose it for the box test above.
static void pointer_vertices_for_test(float a, int point, int wide, int half,
                                      int16_t *x, int16_t *y) {
  pointer_vertices(a, point, wide, half, x, y);
}

// screen.ino's panel bring-up talks to real drivers; everything below that line
// is layout, which is what we are checking. Pull it in with the bring-up
// replaced by a framebuffer panel.
#define SETUP_SCREEN_REPLACED 1
#include "screen_layout.inc"

static void write_ppm(const char *path, Adafruit_SPITFT *panel, int w, int h) {
  FILE *f = fopen(path, "wb");
  fprintf(f, "P6\n%d %d\n255\n", w, h);
  for (int i = 0; i < w * h; i++) {
    uint16_t c = panel->fb[i];
    unsigned char rgb[3] = {
      (unsigned char)(((c >> 11) & 0x1F) * 255 / 31),
      (unsigned char)(((c >> 5) & 0x3F) * 255 / 63),
      (unsigned char)((c & 0x1F) * 255 / 31)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

int main(int argc, char **argv) {
  const char *scenario = argc > 1 ? argv[1] : "wind";
  tft = new Adafruit_SPITFT(320, 480);
  tft->fillScreen(COLOR_BLACK);

  if (!strcmp(scenario, "nolink")) {
    autoPilot.connected = false;
  } else if (!strcmp(scenario, "nowind")) {
    autoPilot.windOk = false; autoPilot.trueOk = false;
  } else if (!strcmp(scenario, "sweepref")) {
    // A single clean frame at exactly the sweep's final values, so the sweep
    // can be diffed against what a from-scratch paint would have produced.
    autoPilot.windAngle = 0.0f; autoPilot.trueAngle = 15.0f;
    autoPilot.windSpeed = 12.5f;
  } else if (!strcmp(scenario, "recoverref")) {
    autoPilot.windAngle = 312.0f; autoPilot.trueAngle = 295.0f;
  } else if (!strcmp(scenario, "coincide")) {
    // Boat stopped: true wind IS apparent wind, so the two arrows land exactly
    // on top of each other. The smaller cyan one lies strictly inside the amber
    // one's footprint, so it is only visible if it is painted last.
    autoPilot.trueAngle = autoPilot.windAngle;
    autoPilot.trueSpeed = autoPilot.windSpeed;
    autoPilot.sog = 0.0f;
  } else if (!strcmp(scenario, "nofix")) {
    autoPilot.trueOk = false; autoPilot.fix = false;
  }

  initialize_displayed_values();
  draw_status_chrome();
  draw_dial_chrome();
  // The screen damps what it draws, so a scenario has to be held long enough
  // for the filter to settle before the frame means anything. 40 ticks is 4 s
  // at DISPLAY_TICK_MS, comfortably past 3 * WIND_DAMPING_TAU_MS.
  for (int i = 0; i < 40; i++) display();

  // Incremental-repaint checks: a single frame cannot show whether the erase
  // path leaves stale needle pixels behind, so drive the same display() the
  // firmware calls through a sequence and look at the final frame.
  if (!strcmp(scenario, "boxes")) {
    // Brute force: every single-pointer box, at every tenth of a degree, must
    // lie entirely inside the interior circle. Tiles are filled edge to edge,
    // so a corner reaching past DIAL_R_IN would bite a permanent notch out of
    // the coloured ring - which is drawn once at boot and never repainted.
    int worst_angle = -1;
    double worst = 0.0;
    for (int i = 0; i < 3600; i++) {
      float a = i / 10.0f;
      const struct { int point, wide, half; } p[2] = {
          {APPARENT_R_POINT, APPARENT_R_WIDE, APPARENT_HALF_W},
          {TRUE_R_POINT, TRUE_R_WIDE, TRUE_HALF_W}};
      for (int k = 0; k < 2; k++) {
        int16_t x[3], y[3];
        pointer_vertices_for_test(a, p[k].point, p[k].wide, p[k].half, x, y);
        int16_t minx = min(x[0], min(x[1], x[2])) - 1;
        int16_t maxx = max(x[0], max(x[1], x[2])) + 1;
        int16_t miny = min(y[0], min(y[1], y[2])) - 1;
        int16_t maxy = max(y[0], max(y[1], y[2])) + 1;
        int16_t xs[2] = {minx, maxx}, ys[2] = {miny, maxy};
        for (int u = 0; u < 2; u++)
          for (int v = 0; v < 2; v++) {
            double dx = xs[u] - DIAL_CX, dy = ys[v] - DIAL_CY;
            double r = sqrt(dx * dx + dy * dy);
            if (r > worst) { worst = r; worst_angle = i; }
          }
      }
    }
    printf("widest pointer box corner over all angles: r=%.2f (at %.1f deg)\n",
           worst, worst_angle / 10.0);
    printf("interior limit DIAL_R_INTERIOR=%d, ring inner edge DIAL_R_IN=%d -> %s\n",
           DIAL_R_INTERIOR, DIAL_R_IN,
           worst <= DIAL_R_INTERIOR ? "OK, every tile stays off the ring"
                                    : "FAIL, a tile can reach the ring");
    return worst <= DIAL_R_INTERIOR ? 0 : 1;
  }

  if (!strcmp(scenario, "cost")) {
    // What one wind update actually costs the panel, for a range of angle
    // steps. At 24 MHz SPI a pixel is 16 bits, so 24e6/16 = 1.5 Mpx/s.
    printf("%8s %7s %9s %9s\n", "step", "tiles", "pixels", "ms@24MHz");
    const float steps[] = {1.0f, 3.0f, 8.0f, 20.0f, 45.0f, 90.0f, 180.0f};
    for (float step : steps) {
      autoPilot.windAngle = 40.0f;
      autoPilot.trueAngle = 55.0f;
      display();
      tft->blits = 0;
      tft->blit_pixels = 0;
      autoPilot.windAngle = 40.0f + step;
      autoPilot.trueAngle = 55.0f + step;
      display();
      printf("%7.0f%s %7u %9u %9.1f\n", step, "d", tft->blits, tft->blit_pixels,
             tft->blit_pixels / 1500.0);
    }
    return 0;
  }

  if (!strcmp(scenario, "overlap")) {
    // When the boat is stopped the true wind IS the apparent wind, so the two
    // arrows land exactly on top of each other. The cyan one is smaller in
    // every dimension, so it is only visible because it is painted last - and
    // that only reads correctly if the amber one still shows a margin all the
    // way round it. Prove it: paint the pair coincident at every whole degree
    // on a bare background and check that no cyan pixel ever touches the
    // background directly. A cyan pixel adjacent to black means the cyan has
    // broken out through the amber somewhere, which is what would happen if
    // anyone retuned the two arrows' proportions independently.
    int worst_angle = -1;
    long breaches = 0;
    for (int deg = 0; deg < 360; deg++) {
      tft->fillScreen(COLOR_BLACK);
      fill_pointer_for_test((float)deg, APPARENT_R_POINT, APPARENT_R_WIDE,
                            APPARENT_HALF_W, DIAL_APPARENT);
      fill_pointer_for_test((float)deg, TRUE_R_POINT, TRUE_R_WIDE, TRUE_HALF_W,
                            DIAL_TRUE);
      long here = 0, cyan = 0;
      for (int y = 1; y < 479; y++) {
        for (int x = 1; x < 319; x++) {
          if (tft->fb[y * 320 + x] != DIAL_TRUE) continue;
          cyan++;
          const int n[4] = {(y - 1) * 320 + x, (y + 1) * 320 + x, y * 320 + x - 1,
                            y * 320 + x + 1};
          for (int k = 0; k < 4; k++) {
            uint16_t c = tft->fb[n[k]];
            if (c != DIAL_TRUE && c != DIAL_APPARENT) here++;
          }
        }
      }
      if (cyan == 0) { here++; }  // cyan drawn but invisible is also a failure
      if (here > 0 && worst_angle < 0) worst_angle = deg;
      breaches += here;
    }
    printf("coincident arrows: cyan pixels touching the background over all "
           "360 angles: %ld\n", breaches);
    printf("  -> %s\n", breaches == 0
               ? "OK, the true arrow stays wholly inside the apparent one"
               : "FAIL, the true arrow breaks out of the apparent one");
    if (breaches) printf("  first failing angle: %d\n", worst_angle);
    return breaches == 0 ? 0 : 1;
  }

  if (!strcmp(scenario, "sweep")) {
    // 90 frames of wind swinging right round the boat, plus the true wind
    // trailing it. If erase_pointer() under-covers by even a pixel, the result
    // is a visible fan of leftovers rather than two clean pointers.
    for (int i = 0; i <= 90; i++) {
      autoPilot.windAngle = fmodf(i * 4.0f, 360.0f);
      autoPilot.trueAngle = fmodf(i * 4.0f + 15.0f, 360.0f);
      autoPilot.windSpeed = 8.0f + i * 0.05f;
      display();
    }
    // Let the filter catch up to the final value, so the frame is comparable
    // with a from-scratch paint of that same reading.
    for (int i = 0; i < 60; i++) display();
  } else if (!strcmp(scenario, "recover")) {
    // wind -> link lost -> link back. The interior is wiped by the message, so
    // the pointer history must be invalidated with it; if it is not, the first
    // frame back punches black triangles through the fresh wedge and hull.
    for (int i = 0; i < 5; i++) { autoPilot.windAngle = 30.0f + i * 6.0f; display(); }
    autoPilot.connected = false; autoPilot.windOk = false; autoPilot.trueOk = false;
    for (int i = 0; i < 5; i++) display();
    autoPilot.connected = true; autoPilot.windOk = true; autoPilot.trueOk = true;
    autoPilot.windAngle = 312.0f; autoPilot.trueAngle = 295.0f;
    for (int i = 0; i < 40; i++) display();
  }

  char path[256];
  snprintf(path, sizeof(path), "%s.ppm", scenario);
  write_ppm(path, tft, 320, 480);
  printf("wrote %s\n", path);
  return 0;
}
