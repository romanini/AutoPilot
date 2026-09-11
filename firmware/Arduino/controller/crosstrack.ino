// Cross-track (XTE/BOD) steering for Garmin-sourced waypoint nav -- deferred
// Item A (.claude/docs/DeferredWork-PostSeaTrial.md). The whole file compiles
// out when XTE_STEERING_ENABLED is 0 (the default; flip it in controller.ino).
//
// desired_heading = leg_course +/- clamp(Kxt * XTE, +/-max), blended back to
// plain bearing-to-mark as range -> 0 so we don't chase the leg line through
// the waypoint. Falls back to bearing-to-mark whenever BOD/XTE are missing or
// stale, or the caller isn't on a live GARMIN mode-2 nav (gated in
// controller.ino's control_task) -- this file never invents a heading on its
// own, it only reshapes one that's already being steered to.
#if XTE_STEERING_ENABLED

#include <math.h>

#define XTE_KXT_DEG_PER_NM      20.0  // correction gain; tuning candidate, see pid/ workflow
#define XTE_MAX_CORRECTION_DEG  30.0  // clamp on the correction term
#define XTE_BLEND_RADIUS_NM      0.1  // full XTE-course blend beyond this range; pure bearing-to-mark at 0
#define XTE_STALE_MS            6000  // matches GARMIN_NAV_TIMEOUT_MS in navsource.ino

// Threading: the updaters below run in command_task (fed from check_garmin)
// while crosstrack_get_desired_heading runs in control_task on the other core.
// The doubles are 64-bit and their loads/stores are NOT atomic on the ESP32,
// so unguarded access can hand the steering loop a torn value. Same pattern as
// navsource.ino: a spinlock held only for the plain copies, never across a
// blocking call.
static portMUX_TYPE crosstrack_mux = portMUX_INITIALIZER_UNLOCKED;

static double   leg_course_true = 0.0;  // latest BOD (origin->dest bearing, true)
static uint32_t leg_course_ms   = 0;
static double   xte_dist_nm     = 0.0;  // latest cross-track error magnitude, nm
static char     xte_steer_dir   = 0;    // 'L' or 'R' -- which way to steer to correct
static uint32_t xte_ms          = 0;

// Local copy of AutoPilot::normalizeDegrees (private to that class): map any
// angle to a compass bearing in [0, 360).
static double crosstrack_normalize(double degrees) {
  degrees = fmod(degrees, 360.0);
  if (degrees < 0.0) degrees += 360.0;
  return degrees;
}

// Called from garmin.ino for each BOD parsed off the live Garmin channel.
void crosstrack_update_bod(double leg_course_true_in) {
  uint32_t now = millis();
  portENTER_CRITICAL(&crosstrack_mux);
  leg_course_true = leg_course_true_in;
  leg_course_ms = now;
  portEXIT_CRITICAL(&crosstrack_mux);
}

// Called from garmin.ino for each XTE parsed off the live Garmin channel.
void crosstrack_update_xte(double xte_nm, char steer_dir) {
  uint32_t now = millis();
  portENTER_CRITICAL(&crosstrack_mux);
  xte_dist_nm = xte_nm;
  xte_steer_dir = steer_dir;
  xte_ms = now;
  portEXIT_CRITICAL(&crosstrack_mux);
}

// Returns true and fills *out_heading with the blended setpoint when both BOD
// and XTE are fresh; returns false (leaves *out_heading untouched) otherwise,
// so the caller falls back to plain bearing-to-mark.
bool crosstrack_get_desired_heading(float bearing_to_mark, float distance_nm, float* out_heading) {
  uint32_t now = millis();

  // Snapshot the shared state under the spinlock so the math below works on a
  // coherent set of values (and never on a torn 64-bit read).
  double   leg_course, xte_nm;
  char     steer_dir;
  uint32_t leg_ms, x_ms;
  portENTER_CRITICAL(&crosstrack_mux);
  leg_course = leg_course_true;
  leg_ms     = leg_course_ms;
  xte_nm     = xte_dist_nm;
  steer_dir  = xte_steer_dir;
  x_ms       = xte_ms;
  portEXIT_CRITICAL(&crosstrack_mux);

  if (leg_ms == 0 || x_ms == 0) return false;
  if ((now - leg_ms) > XTE_STALE_MS) return false;
  if ((now - x_ms) > XTE_STALE_MS) return false;

  double correction = XTE_KXT_DEG_PER_NM * xte_nm;
  if (correction > XTE_MAX_CORRECTION_DEG) correction = XTE_MAX_CORRECTION_DEG;
  double signed_correction = (steer_dir == 'L') ? -correction : correction;
  double xte_course = crosstrack_normalize(leg_course + signed_correction);

  // Blend toward plain bearing-to-mark as range -> 0. xte_weight is 0 at the
  // mark (pure bearing-to-mark) and 1 at/beyond the blend radius (pure
  // leg-course + XTE).
  double xte_weight = (double)distance_nm / XTE_BLEND_RADIUS_NM;
  if (xte_weight < 0.0) xte_weight = 0.0;
  if (xte_weight > 1.0) xte_weight = 1.0;

  // Shortest angular path from bearing_to_mark to xte_course, so the blend
  // never wraps the wrong way around the compass.
  double diff = crosstrack_normalize(xte_course - (double)bearing_to_mark);
  if (diff > 180.0) diff -= 360.0;
  double blended = crosstrack_normalize((double)bearing_to_mark + diff * xte_weight);

  *out_heading = (float)blended;
  return true;
}

#endif  // XTE_STEERING_ENABLED
