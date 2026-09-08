/*
 * The screen's three states, and the entry points screen.ino provides.
 *
 * This exists as a header for the same reason tft.h does: the Arduino build
 * concatenates every .ino and emits its generated prototypes at the TOP of the
 * result, so any function returning (or taking) a sketch-defined type needs
 * that type visible before them - which means a header the main sketch includes,
 * not a definition part-way down a .ino. Defining ScreenState in screen.ino
 * fails with "'ScreenState' does not name a type" pointing at the function that
 * returns it, several hundred lines from the real cause.
 */

// Guard name is deliberately not SCREEN_H: dial.h defines SCREEN_H as the
// panel's pixel height, and the two would collide. Whichever header lost the
// race would either warn about a redefinition or - worse, if dial.h got there
// first - see its own guard already "defined" and silently emit nothing at all,
// leaving every type below missing and the errors pointing at screen.ino.
#ifndef WIND_DISPLAY_SCREEN_H
#define WIND_DISPLAY_SCREEN_H

#include "Adafruit_GFX.h"

// The three things this screen can be showing. They are ordered by which
// failure hides the others: no link means we know nothing at all, no wind data
// means the link is fine but the masthead is not answering. Keeping them
// distinct is the whole point - "nothing on screen" would leave the operator
// unable to tell a flat battery at the masthead from a Wi-Fi dropout, which are
// very different trips up the mast.
enum ScreenState {
  SCREEN_UNKNOWN = -1,  // nothing painted yet; forces the first pass to draw
  SCREEN_NO_LINK = 0,   // no ~APDAT inside the receive timeout
  SCREEN_NO_WIND = 1,   // controller heard, but its isWindOk() is false
  SCREEN_WIND = 2,      // drawing a real reading
};

// How often display() is called, from wind-display.ino's display task.
//
// 20 Hz, well above the 1 Hz telemetry, because this is the rate the needle
// GLIDES at, not the rate new readings arrive at: between packets the damping
// filter below is still closing the gap, and every tick it does that on is a
// frame. At 50 ms the motion is smooth; at 100 ms you can see it step.
//
// It costs little. display() is change-driven and each repaint is a couple of
// small composited tiles (3-12 ms of SPI), so even a needle in constant motion
// leaves the bus mostly idle - and a settled one redraws nothing at all.
//
// The damping filter turns a time constant into a per-tick fraction using this
// value, so the two must not drift apart.
#define DISPLAY_TICK_MS 50

// Display damping.
//
// The masthead vane is genuinely noisy - it is a light vane on a mast head that
// is itself swinging through an arc in any seaway - and ~APDAT delivers a raw
// snapshot of it once a second. Drawn as-is, the needle teleports to a new
// angle every second and the numbers flicker between neighbouring values, which
// is unreadable long before it is inaccurate.
//
// So the screen runs its own first-order filter over the four wind values and
// draws the filtered ones. TAU is the time constant: a step change is ~63%
// covered after one TAU and settled after about three.
//
// 350 ms settles in about a second, which is the useful ceiling here - the
// readings themselves only arrive once a second, so a filter that has not
// finished before the next one lands is permanently chasing and reads as lag.
// (700 ms was the first attempt and was visibly over-damped for exactly that
// reason: only ~76% of each step was covered before the next arrived.)
//
// Raise it for a calmer dial, lower it for a twitchier one; set it to 0 to draw
// the raw values.
//
// This is display damping only. Nothing here is transmitted, and the controller
// still steers from the undamped values it computes itself.
//
// Overridable from the compiler command line, which the host renderer
// (firmware/experiments/wind-display-render/) uses: its repaint test needs
// undamped values, because a filter converges asymptotically and never lands on
// exactly the same pixel twice.
#ifndef WIND_DAMPING_TAU_MS
#define WIND_DAMPING_TAU_MS 350
#endif  // WIND_DISPLAY_SCREEN_H

// Below this the needle is not redrawn at all.
//
// It is what lets a steady wind settle to zero redraws instead of chasing vane
// noise forever, but it also quantises the glide: one pixel at the pointer tip
// is about 0.4 degrees, so at 1 degree the needle advances in visible ~2.5 px
// hops. 0.5 degree puts each step near the pixel grid, which is as smooth as
// this dial can be, and costs only a few more tiles on the way to a new angle.
#define WIND_REDRAW_DEADBAND_DEG 0.5f

// Types that appear in screen.ino function signatures live here, not there.
// The Arduino build emits its generated prototypes ahead of every .ino body, so
// a type defined part-way down a .ino is invisible to the prototype for the
// function that uses it - the same trap as ScreenState above.

// What one field last painted. The numbers cache the rendered STRING rather
// than the value behind it, and that is not fussiness: the damped values change
// by a fraction of a degree every tick, so comparing floats would repaint "47S"
// over "47S" ten times a second. Comparing the text means a field is touched
// only when it actually reads differently.
struct TextCache {
  char text[16];
  uint16_t color;
  const GFXfont *font;
  bool valid;
};

// One value under the damping filter.
struct Damped {
  float value;
  bool primed;  // false until the first real sample, so we start at it, not at 0
};

// One reading, already damped, shared by the dial and the numbers so the needle
// and the figure under it can never disagree.
struct WindReading {
  bool connected;
  bool apparentOk;
  float apparentAngle;
  float apparentSpeed;
  bool trueOk;
  float trueAngle;
  float trueSpeed;
};

void setup_screen();
void display();

#endif  // WIND_DISPLAY_SCREEN_H
