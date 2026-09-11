/*
 * Geometry and palette for the wind dial, and the entry points dial.ino
 * provides to screen.ino.
 *
 * The panel runs PORTRAIT here (320 wide, 480 tall) - the other head unit runs
 * the same hardware landscape. Everything below is in that portrait frame.
 *
 * Layout, top to bottom:
 *
 *     y   0.. 23   status bar: pointer legend, air temperature
 *     y  34..338   the dial (centre 160,186 - outer radius 152)
 *     y 344..480   numbers: AWA/AWS, TWA/TWS, SOG/HDG
 *
 * The dial is deliberately split into a static ring and a repainted interior:
 *
 *   - The RING (r 118..152) carries the red/grey/green arcs, the tick marks and
 *     the degree labels. It is drawn once at boot and never touched again -
 *     it costs ~700 small fills, which is fine once and far too slow at 1 Hz.
 *   - The INTERIOR (r < 118) carries the no-go wedge, the hull and the two
 *     pointers, and is the only thing that repaints when the wind moves.
 *
 * That split is why the degree labels live on the ring band rather than inside
 * it as they would on a paper dial: it keeps everything the pointers sweep over
 * to a handful of shapes that are cheap to erase and redraw. Adafruit_GFX has
 * no clipping and no off-screen colour buffer to double-buffer with, so a
 * repaint that had to restore text under the needle would mean clearing and
 * redrawing the whole disc - ~30 ms of visible black flash, once a second,
 * forever. Erasing two triangles and repainting the wedge and hull is ~8 ms and
 * flickers not at all.
 */

// Guard name deliberately namespaced, for the same reason screen.h's is: short
// header-guard names collide with the short constant names these files define.
#ifndef WIND_DISPLAY_DIAL_H
#define WIND_DISPLAY_DIAL_H

#include <Arduino.h>
#include "Adafruit_SPITFT.h"

// The panel, constructed in screen.ino once detect_tft() has said which one is
// fitted. Declared here because the Arduino build concatenates the .ino files
// in alphabetical order after the main sketch, so dial.ino is compiled before
// screen.ino and cannot see its definition otherwise. Held as the base class so
// every draw call below runs unchanged on either controller.
extern Adafruit_SPITFT *tft;

// --- screen ---------------------------------------------------------------
#define SCREEN_W 320
#define SCREEN_H 480

#define STATUS_H 24  // status bar occupies y 0..STATUS_H-1

// --- dial -----------------------------------------------------------------
#define DIAL_CX 160
#define DIAL_CY 186
#define DIAL_R_OUT 152  // outside edge of the coloured ring
#define DIAL_R_IN 118   // inside edge of the ring = outside edge of the interior

#define TICK_MINOR_R 124  // minor ticks run DIAL_R_IN..TICK_MINOR_R
#define TICK_MAJOR_R 127  // major (every 30 deg) run DIAL_R_IN..TICK_MAJOR_R
#define LABEL_R 138       // radius of the centre of the degree labels

// The interior, where the pointers live. One pixel inside the ring so an erase
// can be drawn slightly oversized without touching the ring's colour.
#define DIAL_R_INTERIOR (DIAL_R_IN - 2)

// The pointers are arrows aimed INWARD: the point is at R_POINT, in toward the
// boat, and the wide end is a chord out at R_WIDE. That is the way round a wind
// arrow has to be - the wide end sits at the bearing the wind is blowing FROM,
// and the arrow shows it arriving at the boat. Pointing outward would read as
// the boat throwing wind at the horizon.
//
// Wide enough at the wide end that the lack of anti-aliasing in Adafruit_GFX is
// invisible - a 22 px chord has no staircase to see, where a 1 px line would.
//
// R_WIDE cannot simply be pushed out to meet the ring, and the limit is a
// property of the repaint scheme rather than a taste call. Dirty regions are
// axis-aligned rectangles, and the bounding box of a chord lying on the
// diagonal has a corner at about r * sqrt(2) * sin(45 + halfwidth) - roughly
// 1.09 * r - which lands outside the ring's inner edge long before the chord
// itself does. Since tiles are filled edge to edge and the ring is painted once
// at boot, such a tile would notch it permanently. Anything drawn inside the
// ring therefore has to stay within about r=106; the host renderer's `boxes`
// check enforces it over all 3600 tenth-degree angles, and it is what caught
// this when the arrows were first reversed at their old length.
//
// R_POINT is free of that constraint - the bounding box is set by the outer
// end - so the arrows get their length by reaching further IN, not further out.
#define APPARENT_R_POINT 34
#define APPARENT_R_WIDE 102
#define APPARENT_HALF_W 11

// True-wind arrow: deliberately thinner and shorter as well as a different
// colour, so the two are told apart by shape in a glance and by colour on
// inspection - colour alone is a bad primary cue on a sunlit transflective
// panel, and worse for a red/green-blind helm.
#define TRUE_R_POINT 48
#define TRUE_R_WIDE 94
#define TRUE_HALF_W 6

// Half-width of the shaded no-go wedge, in degrees off the bow.
//
// 40 is a placeholder for a typical cruising boat, not a measurement. It is the
// one number on this screen that is a property of the BOAT rather than of the
// instrument, so trim it once the real close-hauled angle is known - the wedge
// is only useful if its edge is where the boat actually stops going forward.
//
// It also sets where the coloured ring starts, so changing it moves both.
#define NOGO_HALF_ANGLE 40

// Half-width of the asymmetric sector, in degrees either side of DEAD ASTERN,
// so 60 puts its edges at AWA 120 and 240. Like NOGO_HALF_ANGLE this is a
// property of the boat and its sails, not of the instrument.
//
// It marks the angle abaft which the asymmetric is the sail.
//
// The number is in APPARENT wind, because that is what this dial's scale is,
// and the conversion is worth keeping in view: sail-selection angles are
// naturally TRUE wind angles and the two diverge sharply off the wind. At 12 kn
// true, TWA 120 with 6.8 kn of boat speed reads as AWA 86; TWA 150 with 6.5
// reads as AWA 123. So AWA 120 is around TWA 145-150 - past the point where a
// cruising asymmetric first goes up (nearer AWA 90) and squarely in the middle
// of where it is carried.
//
// That is a deliberate trade against the strict hoist angle: putting the edge
// at the apparent beam left colour on only 50 degrees a side and washed the
// dial out, and the port/starboard cue through the reaching angles is worth
// more than marking the hoist to the degree.
//
// What NOT to do is reach into the 135-150 range by analogy with true-wind
// figures. AWA 135 is about TWA 155-160, which is not an asymmetric angle at
// all - it is the deep limit where a symmetric kite starts dying behind the
// main.
#define RUN_HALF_ANGLE 60

// The no-go wedge runs from the hub right out to the inner edge of the ring,
// and the ring carries the same shade across that sector, so the zone reads as
// one continuous grey wedge rather than as a shaded patch sitting inside an
// unbroken ring of colour.
//
// Red and green therefore begin at the EDGE of the no-go zone rather than at
// the bow. That is the honest place for them: on the bow side of that edge the
// boat is not going anywhere on either tack, so "which side is the wind on" is
// not yet a useful question, and colouring it invites the eye to answer one
// that does not apply. They END at the edge of the downwind sector for the
// mirror-image reason: running that deep, the tack you are on stops being the
// thing the colour should be drawing your eye to.
//
// The two zones differ in shade AND shape, and both differences carry meaning.
// The no-go is a full wedge from the hub in near-black slate: a prohibition,
// and an area you cannot enter. The asymmetric sector is a band on the ring
// only, in a lighter indigo: an affordance - somewhere you can go, with a
// different sail up. Painting them identically said "neither of these is a
// sector", which was wrong about the second one and increasingly misleading as
// it grew to half the dial. Set DIAL_RUN to DIAL_NOGO to go back to one shade.
#define NOGO_R_OUT DIAL_R_IN

// --- numbers panel --------------------------------------------------------
#define PANEL_TOP 344
#define PANEL_COL_W 152
#define PANEL_COL_A_X 6    // left column x
#define PANEL_COL_B_X 162  // right column x

// --- palette (RGB565) -----------------------------------------------------
// tft.h supplies the primaries; these are the muted instrument shades. The
// ring reds/greens are darkened from full saturation on purpose: at full
// COLOR_RED/COLOR_GREEN the ring is louder than the pointer sitting on it,
// which inverts the visual hierarchy - the ring is context, the pointer is
// the reading.
#define DIAL_PORT_RED 0xC8A2      // port half of the ring
#define DIAL_STBD_GREEN 0x0CE4    // starboard half of the ring
#define DIAL_NOGO 0x1926          // no-go wedge fill (dark slate blue)
#define DIAL_RUN 0x31CB           // downwind / asymmetric sector on the ring (muted indigo)
#define DIAL_NOGO_EDGE 0x21A8     // its two radial edges
#define DIAL_HULL_FILL 0x0883     // hull silhouette interior
#define DIAL_HULL_EDGE 0x4B0D     // hull outline (steel blue)
#define DIAL_LABEL 0xCE59         // degree labels on the ring
#define DIAL_DIM 0x8C51           // field labels, units, secondary text
#define DIAL_APPARENT 0xFEA0      // apparent pointer + its numbers (amber)
#define DIAL_TRUE 0x06DF          // true pointer + its numbers (cyan)

// A drawing target: either the panel itself, or an off-screen tile whose
// top-left corner sits at (ox, oy) in panel coordinates. Everything inside the
// ring is drawn through one of these so the identical code can paint to either
// - which is what makes the flicker-free tile compositing in dial.ino possible.
struct Surface {
  Adafruit_GFX *gfx;
  int16_t ox, oy;  // subtracted from absolute dial coordinates
};

// A rectangle of the dial, in panel coordinates. Repaints work in these: the
// interior is never redrawn whole, only the tiles the pointers moved through.
struct DialRect {
  int16_t x, y, w, h;
};

// Largest tile that will be composited off-screen in one piece, in pixels.
// 12000 px is 24 kB of GFXcanvas16 on the heap, which is comfortable against
// the ~270 kB free, and it is the knob that decides how eagerly dirty
// rectangles are merged: past this they are painted as separate tiles instead.
// It also, usefully, keeps merged tiles small enough that they rarely fail the
// "corners inside the ring" test in dial.ino.
#define DIAL_MAX_TILE_PIXELS 12000

// --- entry points ---------------------------------------------------------
void draw_dial_chrome();  // the static ring: once, at boot
void clear_dial_interior();
void draw_dial_message(const char *line1, const char *line2, uint16_t color);
void draw_dial_wind(float apparent_angle, bool apparent_ok,
                    float true_angle, bool true_ok);

#endif
