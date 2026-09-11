/*
 * The wind dial itself: the polar geometry, and the painting of the ring, the
 * no-go wedge, the hull silhouette and the two pointers.
 *
 * screen.ino owns what to show and when; this file owns how it is drawn. See
 * dial.h for the layout, the palette, why the ring is static, and why the
 * interior is composited off-screen before it reaches the panel.
 */

#include "Adafruit_GFX.h"
#include "tft.h"
#include "dial.h"
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>

// Angles everywhere in this file are degrees CLOCKWISE FROM THE BOW, matching
// the wire convention of ~APDAT's wind fields: 0 dead ahead, 90 on the
// starboard beam, 180 astern. Screen coordinates put the bow at the top, so the
// conversion is a -90 degree rotation into the usual maths frame.
static inline float dial_radians(float angle_deg) {
  return (angle_deg - 90.0f) * (float)PI / 180.0f;
}

// ---------------------------------------------------------------------------
// Surfaces
//
// Everything inside the ring is drawn through a Surface (declared in dial.h)
// rather than straight at the panel, so the identical code can paint either to
// the screen or into an off-screen tile that is then pushed in one go. That
// indirection is the whole anti-flicker mechanism - see paint_tile() below.
// ---------------------------------------------------------------------------

static Surface panel_surface() {
  Surface s = {tft, 0, 0};
  return s;
}

static void surface_triangle(const Surface &s, int16_t x0, int16_t y0, int16_t x1,
                             int16_t y1, int16_t x2, int16_t y2, uint16_t color) {
  s.gfx->fillTriangle(x0 - s.ox, y0 - s.oy, x1 - s.ox, y1 - s.oy, x2 - s.ox,
                      y2 - s.oy, color);
}

static void surface_line(const Surface &s, int16_t x0, int16_t y0, int16_t x1,
                         int16_t y1, uint16_t color) {
  s.gfx->drawLine(x0 - s.ox, y0 - s.oy, x1 - s.ox, y1 - s.oy, color);
}

static void polar(float angle_deg, float radius, int16_t *x, int16_t *y) {
  float r = dial_radians(angle_deg);
  *x = (int16_t)lroundf(DIAL_CX + radius * cosf(r));
  *y = (int16_t)lroundf(DIAL_CY + radius * sinf(r));
}

// ---------------------------------------------------------------------------
// Rectangles
// ---------------------------------------------------------------------------

// The three corners of a pointer: the point in at r_point, and a wide end of
// width 2*half_w square across the radius out at r_wide.
static void pointer_vertices(float angle_deg, int r_point, int r_wide, int half_w,
                             int16_t *x, int16_t *y) {
  float r = dial_radians(angle_deg);
  float c = cosf(r);
  float s = sinf(r);

  x[0] = (int16_t)lroundf(DIAL_CX + r_point * c);
  y[0] = (int16_t)lroundf(DIAL_CY + r_point * s);
  // Perpendicular to the pointer axis is (-s, c).
  x[1] = (int16_t)lroundf(DIAL_CX + r_wide * c - half_w * s);
  y[1] = (int16_t)lroundf(DIAL_CY + r_wide * s + half_w * c);
  x[2] = (int16_t)lroundf(DIAL_CX + r_wide * c + half_w * s);
  y[2] = (int16_t)lroundf(DIAL_CY + r_wide * s - half_w * c);
}

static bool rects_overlap(const DialRect &a, const DialRect &b) {
  return !(a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y ||
           b.y + b.h <= a.y);
}

static DialRect rect_union(const DialRect &a, const DialRect &b) {
  int16_t x0 = min(a.x, b.x);
  int16_t y0 = min(a.y, b.y);
  int16_t x1 = max((int16_t)(a.x + a.w), (int16_t)(b.x + b.w));
  int16_t y1 = max((int16_t)(a.y + a.h), (int16_t)(b.y + b.h));
  DialRect r = {x0, y0, (int16_t)(x1 - x0), (int16_t)(y1 - y0)};
  return r;
}

// A tile is only safe to paint if every one of its corners is inside the
// interior circle. Tiles are filled edge to edge - a corner that reached past
// DIAL_R_IN would blank a bite out of the coloured ring, and the ring is drawn
// once at boot and never repainted, so that damage would be permanent.
//
// Single pointer boxes always pass (the worst case is about r=113 against the
// interior's 116, which the host renderer asserts over all 360 angles). It is
// merged boxes that need the test: the union of a pointer at 0 degrees and one
// at 90 has a corner out at r=160, well outside the panel's dial entirely.
static bool rect_inside_interior(const DialRect &r) {
  const int32_t limit = (int32_t)DIAL_R_INTERIOR * DIAL_R_INTERIOR;
  const int16_t xs[2] = {r.x, (int16_t)(r.x + r.w - 1)};
  const int16_t ys[2] = {r.y, (int16_t)(r.y + r.h - 1)};
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < 2; j++) {
      int32_t dx = xs[i] - DIAL_CX;
      int32_t dy = ys[j] - DIAL_CY;
      if (dx * dx + dy * dy > limit) return false;
    }
  }
  return true;
}

// The bounding box of a pointer, grown by a pixel.
//
// The box - not a slightly larger triangle - is what both erases and bounds a
// pointer. A grown triangle looks like a geometric superset and is not one in
// practice: fillTriangle rasterises from integer vertices, so re-rounding them
// shifts scanline spans by a pixel and the grown fill misses occasional edge
// pixels near the point. At 1 Hz those accumulate into a spray of stale
// amber and cyan specks across the dial within a minute. A bounding box is a
// superset however the rasteriser rounds.
static DialRect pointer_box(float angle_deg, int r_point, int r_wide, int half_w) {
  int16_t x[3], y[3];
  pointer_vertices(angle_deg, r_point, r_wide, half_w, x, y);

  int16_t minx = min(x[0], min(x[1], x[2])) - 1;
  int16_t maxx = max(x[0], max(x[1], x[2])) + 1;
  int16_t miny = min(y[0], min(y[1], y[2])) - 1;
  int16_t maxy = max(y[0], max(y[1], y[2])) + 1;

  DialRect r = {minx, miny, (int16_t)(maxx - minx + 1), (int16_t)(maxy - miny + 1)};
  return r;
}

static void fill_pointer(const Surface &s, float angle_deg, int r_point, int r_wide,
                         int half_w, uint16_t color) {
  int16_t x[3], y[3];
  pointer_vertices(angle_deg, r_point, r_wide, half_w, x, y);
  surface_triangle(s, x[0], y[0], x[1], y[1], x[2], y[2], color);
}

// ---------------------------------------------------------------------------
// The static ring
// ---------------------------------------------------------------------------

// A filled annular sector, built from quads (two triangles each) one degree
// wide. Adafruit_GFX has no arc or ring primitive, and this is the cheapest
// shape that has no seams: consecutive quads share an edge exactly, and the
// half-degree overlap covers the rounding at the shared vertices.
static void fill_ring_segment(float from_deg, float to_deg, int r_inner,
                              int r_outer, uint16_t color) {
  Surface s = panel_surface();
  for (float a = from_deg; a < to_deg; a += 1.0f) {
    int16_t x0, y0, x1, y1, x2, y2, x3, y3;
    polar(a, r_inner, &x0, &y0);
    polar(a, r_outer, &x1, &y1);
    polar(a + 1.5f, r_outer, &x2, &y2);
    polar(a + 1.5f, r_inner, &x3, &y3);
    surface_triangle(s, x0, y0, x1, y1, x2, y2, color);
    surface_triangle(s, x0, y0, x2, y2, x3, y3, color);
  }
}

static void draw_tick(float angle_deg, int r_from, int r_to, uint16_t color,
                      bool thick) {
  Surface s = panel_surface();
  int16_t x1, y1, x2, y2;
  polar(angle_deg, r_from, &x1, &y1);
  polar(angle_deg, r_to, &x2, &y2);
  surface_line(s, x1, y1, x2, y2, color);
  if (thick) {
    // No line-width in Adafruit_GFX. One pixel of offset perpendicular to the
    // tick is enough to read as "major" without a second full trig pass.
    float r = dial_radians(angle_deg);
    int16_t dx = (int16_t)lroundf(-sinf(r));
    int16_t dy = (int16_t)lroundf(cosf(r));
    surface_line(s, x1 + dx, y1 + dy, x2 + dx, y2 + dy, color);
    surface_line(s, x1 - dx, y1 - dy, x2 - dx, y2 - dy, color);
  }
}

// Which of the ring's four arcs a given bearing falls on.
static uint16_t ring_color(float angle_deg) {
  float a = fmodf(angle_deg, 360.0f);
  if (a < 0.0f) a += 360.0f;
  if (a <= NOGO_HALF_ANGLE || a >= 360.0f - NOGO_HALF_ANGLE) return DIAL_NOGO;
  if (a >= 180.0f - RUN_HALF_ANGLE && a <= 180.0f + RUN_HALF_ANGLE) return DIAL_RUN;
  return (a < 180.0f) ? DIAL_STBD_GREEN : DIAL_PORT_RED;
}

// Degree labels sit ON the ring band - see the note in dial.h about why they
// are not inside it. Each is rendered into a 1-bit canvas and blitted, which
// both centres it on the tick and paints its own background in one pass.
static void draw_label(float angle_deg, const char *text) {
  int16_t cx, cy;
  polar(angle_deg, LABEL_R, &cx, &cy);

  const int16_t w = 30;
  const int16_t h = 14;
  GFXcanvas1 canvas(w, h);
  canvas.fillScreen(0);
  canvas.setFont(&FreeSansBold9pt7b);
  canvas.setTextColor(1);
  canvas.setTextWrap(false);

  int16_t bx, by;
  uint16_t bw, bh;
  canvas.getTextBounds(text, 0, 0, &bx, &by, &bw, &bh);
  canvas.setCursor((w - (int16_t)bw) / 2 - bx, (h - (int16_t)bh) / 2 - by);
  canvas.print(text);

  // Blitted WITHOUT a background, so only the glyph pixels land and whatever the
  // ring already put there shows through. The alternative - passing
  // ring_color(angle_deg) as a background - works only while no label straddles
  // an arc boundary, and a label box is about 12 degrees wide at this radius, so
  // moving NOGO_HALF_ANGLE or RUN_HALF_ANGLE to within 6 degrees of a labelled
  // tick would paint a solid grey rectangle across the neighbouring colour.
  // Drawing transparently removes that coupling entirely: the sector angles are
  // then free to be whatever the boat wants.
  //
  // Safe because chrome is drawn once, over a freshly painted ring, in order:
  // arcs, then ticks, then these.
  tft->drawBitmap(cx - w / 2, cy - h / 2, canvas.getBuffer(), w, h, DIAL_LABEL);
}

// The static ring: red to port, green to starboard, ticks every 10 degrees and
// labels every 30. Drawn once at boot - it is several hundred small fills and
// takes an appreciable fraction of a second, which is why nothing repaints it.
void draw_dial_chrome() {
  // Four arcs. Colour occupies only the sectors where "which side is the wind
  // on" is the question the helm is actually asking: it starts at the edge of
  // the no-go zone and stops at the edge of the downwind sector. The no-go arc
  // is written as 320..400 rather than as two pieces because polar() is happy
  // past 360 - that sector wraps through the bow.
  fill_ring_segment(NOGO_HALF_ANGLE, 180.0f - RUN_HALF_ANGLE, DIAL_R_IN, DIAL_R_OUT,
                    DIAL_STBD_GREEN);
  fill_ring_segment(180.0f + RUN_HALF_ANGLE, 360.0f - NOGO_HALF_ANGLE, DIAL_R_IN,
                    DIAL_R_OUT, DIAL_PORT_RED);
  fill_ring_segment(180.0f - RUN_HALF_ANGLE, 180.0f + RUN_HALF_ANGLE, DIAL_R_IN,
                    DIAL_R_OUT, DIAL_RUN);
  fill_ring_segment(360.0f - NOGO_HALF_ANGLE, 360.0f + NOGO_HALF_ANGLE, DIAL_R_IN,
                    DIAL_R_OUT, DIAL_NOGO);

  for (int a = 0; a < 360; a += 10) {
    bool major = (a % 30) == 0;
    draw_tick((float)a, DIAL_R_IN, major ? TICK_MAJOR_R : TICK_MINOR_R,
              major ? COLOR_WHITE : DIAL_LABEL, major);
  }

  // 0 and 180 are left unlabelled: the bow is obvious from the hull and the
  // stern from the dial, and a "0" at the top would sit exactly where the
  // no-go wedge wants the eye to go.
  static const int labelled[] = {30, 60, 90, 120, 150};
  char text[6];
  for (unsigned i = 0; i < sizeof(labelled) / sizeof(labelled[0]); i++) {
    snprintf(text, sizeof(text), "%d", labelled[i]);
    draw_label((float)labelled[i], text);          // starboard side
    draw_label((float)(360 - labelled[i]), text);  // port side
  }
}

// ---------------------------------------------------------------------------
// The interior: no-go wedge, hull, pointers
// ---------------------------------------------------------------------------

// Where the pointers were last drawn, so the next repaint knows what to cover.
// File scope rather than local to draw_dial_wind() so that clear_dial_interior()
// can invalidate it: after the interior has been wiped there is nothing left to
// erase, and covering the old positions anyway would waste a tile.
static bool pointers_drawn = false;
static float previous_apparent = 0.0f;
static bool previous_apparent_drawn = false;
static float previous_true = 0.0f;
static bool previous_true_drawn = false;

void clear_dial_interior() {
  tft->fillCircle(DIAL_CX, DIAL_CY, DIAL_R_IN - 1, COLOR_BLACK);
  pointers_drawn = false;
}

// A filled pie sector from the centre, as a fan of triangles.
static void fill_sector(const Surface &s, float from_deg, float to_deg, int radius,
                        uint16_t color) {
  for (float a = from_deg; a < to_deg; a += 2.0f) {
    int16_t x1, y1, x2, y2;
    polar(a, radius, &x1, &y1);
    polar(min(a + 2.5f, to_deg), radius, &x2, &y2);
    surface_triangle(s, DIAL_CX, DIAL_CY, x1, y1, x2, y2, color);
  }
}

// The boat: bow up, drawn as a filled silhouette with a lighter outline so it
// reads as the reference frame rather than as data. Points are a hull-shaped
// polygon in dial-centred coordinates; the fan fill works because the shape is
// convex about the centre, which it is by construction.
static const int8_t hull_x[] = {0, 14, 21, 22, 17, -17, -22, -21, -14};
static const int8_t hull_y[] = {-78, -52, -20, 26, 54, 54, 26, -20, -52};
static const int hull_n = sizeof(hull_x) / sizeof(hull_x[0]);

static void draw_hull(const Surface &s) {
  for (int i = 0; i < hull_n; i++) {
    int j = (i + 1) % hull_n;
    surface_triangle(s, DIAL_CX, DIAL_CY, DIAL_CX + hull_x[i], DIAL_CY + hull_y[i],
                     DIAL_CX + hull_x[j], DIAL_CY + hull_y[j], DIAL_HULL_FILL);
  }
  for (int i = 0; i < hull_n; i++) {
    int j = (i + 1) % hull_n;
    surface_line(s, DIAL_CX + hull_x[i], DIAL_CY + hull_y[i],
                 DIAL_CX + hull_x[j], DIAL_CY + hull_y[j], DIAL_HULL_EDGE);
  }
}

// The wedge is drawn out to NOGO_R_OUT (the ring's inner edge) so it meets the
// ring's own no-go arc with no black seam between them. Tiles clip well inside
// that - no tile ever reaches past r=114 - so the outermost couple of pixels
// are painted only by the from-scratch pass and never repainted, which is
// correct: nothing moves out there.
static void draw_nogo(const Surface &s) {
  fill_sector(s, -NOGO_HALF_ANGLE, NOGO_HALF_ANGLE, NOGO_R_OUT, DIAL_NOGO);
  int16_t x, y;
  polar(-NOGO_HALF_ANGLE, NOGO_R_OUT, &x, &y);
  surface_line(s, DIAL_CX, DIAL_CY, x, y, DIAL_NOGO_EDGE);
  polar(NOGO_HALF_ANGLE, NOGO_R_OUT, &x, &y);
  surface_line(s, DIAL_CX, DIAL_CY, x, y, DIAL_NOGO_EDGE);
}

// Bounding boxes of the two static interior shapes, so a tile that cannot
// contain them skips their (clipped, but not free) rasterisation entirely.
static DialRect nogo_box() {
  int16_t half = (int16_t)lroundf(NOGO_R_OUT * sinf(NOGO_HALF_ANGLE * (float)PI / 180.0f)) + 2;
  DialRect r = {(int16_t)(DIAL_CX - half), (int16_t)(DIAL_CY - NOGO_R_OUT - 1),
                (int16_t)(2 * half + 1), (int16_t)(NOGO_R_OUT + 3)};
  return r;
}

static DialRect hull_box() {
  DialRect r = {(int16_t)(DIAL_CX - 24), (int16_t)(DIAL_CY - 80), 49, 137};
  return r;
}

// Paint the whole interior onto one surface. Used both for a from-scratch paint
// straight to the panel and, tile by tile, for incremental updates - which is
// what guarantees the two can never disagree.
static void paint_interior(const Surface &s, const DialRect *clip,
                           float apparent_angle, bool apparent_ok,
                           float true_angle, bool true_ok) {
  if (clip == NULL || rects_overlap(*clip, nogo_box())) {
    draw_nogo(s);
  }
  if (clip == NULL || rects_overlap(*clip, hull_box())) {
    draw_hull(s);
  }

  // No hub dot. There was one while the pointers were needles pivoting about
  // the centre; now that they are arrows aimed inward from the rim there is no
  // pivot for it to mark, and it read as a stray dot on the boat.
  //
  // APPARENT FIRST, TRUE ON TOP - and that order is load-bearing, not
  // arbitrary. The true arrow is shorter at both ends and narrower everywhere
  // (48..94 against 34..102, half-width 6 against 11 at the rim), so it lies
  // strictly inside the apparent arrow's footprint at every radius. Drawn
  // underneath it is not merely partly hidden when the two coincide, it
  // disappears completely - and they coincide exactly when the boat is stopped,
  // which includes every bench test.
  //
  // Painted on top it reads as a cyan core inside an amber border, because that
  // strict-containment is symmetric: the apparent arrow still shows a margin on
  // every side (about 4 px at the wide end, tapering to the point). Both are
  // legible whether they agree or not. Anything that changes the two arrows'
  // proportions needs to preserve that containment or this stops working.
  if (apparent_ok) {
    fill_pointer(s, apparent_angle, APPARENT_R_POINT, APPARENT_R_WIDE,
                 APPARENT_HALF_W, DIAL_APPARENT);
  }
  if (true_ok) {
    fill_pointer(s, true_angle, TRUE_R_POINT, TRUE_R_WIDE, TRUE_HALF_W, DIAL_TRUE);
  }
}

// Repaint one rectangle of the interior WITHOUT the panel ever showing a
// half-finished state.
//
// This is the anti-flicker mechanism, and the reason for the Surface
// indirection above. The old approach drew straight at the panel: black out the
// old needle, repaint the wedge, repaint the hull, draw the new needle - four
// separate SPI bursts, ~10 ms apart, every time the wind moved. At 1 Hz in a
// shifty breeze you watch that happen, and it reads as the dial flashing.
//
// Here the same drawing runs into an off-screen 16-bit tile and reaches the
// panel as a single contiguous write of final pixels. There is no intermediate
// state to see, at any update rate.
//
// If the canvas cannot be allocated we fall back to painting the tile's content
// straight to the panel - visibly worse, but a blank dial would be worse still,
// and this is the one path here that can fail at runtime.
static void paint_tile(const DialRect &r, float apparent_angle, bool apparent_ok,
                       float true_angle, bool true_ok) {
  GFXcanvas16 canvas(r.w, r.h);
  if (canvas.getBuffer() == NULL) {
    tft->fillRect(r.x, r.y, r.w, r.h, COLOR_BLACK);
    Surface s = panel_surface();
    paint_interior(s, &r, apparent_angle, apparent_ok, true_angle, true_ok);
    return;
  }

  canvas.fillScreen(COLOR_BLACK);
  Surface s = {&canvas, r.x, r.y};
  paint_interior(s, &r, apparent_angle, apparent_ok, true_angle, true_ok);
  tft->drawRGBBitmap(r.x, r.y, canvas.getBuffer(), r.w, r.h);
}

// Fold the dirty rectangles together where the result is still small enough to
// composite in one tile and still safely inside the ring.
//
// Merging matters for the common case: a needle that has moved a couple of
// degrees produces two heavily overlapping boxes, and painting them as one tile
// is both cheaper and avoids writing the overlap twice. Boxes that stay
// separate are simply painted in turn - each tile carries the complete final
// content of its own rectangle, so an overlap that is painted twice is correct
// both times.
static int merge_boxes(DialRect *boxes, int n) {
  bool merged = true;
  while (merged && n > 1) {
    merged = false;
    for (int i = 0; i < n && !merged; i++) {
      for (int j = i + 1; j < n && !merged; j++) {
        DialRect u = rect_union(boxes[i], boxes[j]);
        if ((int32_t)u.w * (int32_t)u.h <= DIAL_MAX_TILE_PIXELS &&
            rect_inside_interior(u)) {
          boxes[i] = u;
          boxes[j] = boxes[n - 1];
          n--;
          merged = true;
        }
      }
    }
  }
  return n;
}

void draw_dial_wind(float apparent_angle, bool apparent_ok, float true_angle,
                    bool true_ok) {
  // A fresh interior (just cleared, or first paint) has nothing to cover and
  // nothing to flicker against, so paint it straight to the panel.
  if (!pointers_drawn) {
    Surface s = panel_surface();
    paint_interior(s, NULL, apparent_angle, apparent_ok, true_angle, true_ok);
  } else {
    DialRect boxes[4];
    int n = 0;
    if (previous_apparent_drawn) {
      boxes[n++] = pointer_box(previous_apparent, APPARENT_R_POINT, APPARENT_R_WIDE,
                               APPARENT_HALF_W);
    }
    if (previous_true_drawn) {
      boxes[n++] = pointer_box(previous_true, TRUE_R_POINT, TRUE_R_WIDE, TRUE_HALF_W);
    }
    if (apparent_ok) {
      boxes[n++] = pointer_box(apparent_angle, APPARENT_R_POINT, APPARENT_R_WIDE,
                               APPARENT_HALF_W);
    }
    if (true_ok) {
      boxes[n++] = pointer_box(true_angle, TRUE_R_POINT, TRUE_R_WIDE, TRUE_HALF_W);
    }

    n = merge_boxes(boxes, n);
    for (int i = 0; i < n; i++) {
      paint_tile(boxes[i], apparent_angle, apparent_ok, true_angle, true_ok);
    }
  }

  pointers_drawn = true;
  previous_apparent = apparent_angle;
  previous_apparent_drawn = apparent_ok;
  previous_true = true_angle;
  previous_true_drawn = true_ok;
}

// ---------------------------------------------------------------------------
// Messages
// ---------------------------------------------------------------------------

// Two lines of text centred in the interior, for the states where there is no
// wind to point at. Clears the interior first, so it is also the way back from
// a drawn dial.
void draw_dial_message(const char *line1, const char *line2, uint16_t color) {
  clear_dial_interior();

  const int16_t w = 2 * DIAL_R_IN - 20;
  GFXcanvas1 canvas(w, 30);
  int16_t bx, by;
  uint16_t bw, bh;

  canvas.fillScreen(0);
  canvas.setFont(&FreeSansBold18pt7b);
  canvas.setTextColor(1);
  canvas.setTextWrap(false);
  canvas.getTextBounds(line1, 0, 0, &bx, &by, &bw, &bh);
  canvas.setCursor((w - (int16_t)bw) / 2 - bx, (30 - (int16_t)bh) / 2 - by);
  canvas.print(line1);
  tft->drawBitmap(DIAL_CX - w / 2, DIAL_CY - 26, canvas.getBuffer(), w, 30,
                  color, COLOR_BLACK);

  // The second line is 9pt, not 12pt, and that is a measurement rather than a
  // preference: at 12pt the longest of these strings is 269 px against the
  // 216 px that fits inside the ring, so it would be centre-clipped at both
  // ends. Anything added here has to be measured against that 216.
  if (line2 != NULL && *line2 != '\0') {
    GFXcanvas1 sub(w, 20);
    sub.fillScreen(0);
    sub.setFont(&FreeSansBold9pt7b);
    sub.setTextColor(1);
    sub.setTextWrap(false);
    sub.getTextBounds(line2, 0, 0, &bx, &by, &bw, &bh);
    sub.setCursor((w - (int16_t)bw) / 2 - bx, (20 - (int16_t)bh) / 2 - by);
    sub.print(line2);
    tft->drawBitmap(DIAL_CX - w / 2, DIAL_CY + 12, sub.getBuffer(), w, 20,
                    DIAL_DIM, COLOR_BLACK);
  }
}
