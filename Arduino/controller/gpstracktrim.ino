// Outer, slow (~10 s) GPS-frame correction loop for mode 2 (Option 3 waypoint-
// nav restructure). The 100 Hz inner loop (control_task, controller.ino)
// always steers by the compass now, in both modes; in mode 2 its setpoint is
// AutoPilot's heading_command rather than the raw GPS bearing-to-waypoint.
// gps_track_trim() is the only place that compares a GPS-frame target course
// against the damped GPS track and nudges heading_command to close the gap.
// Because both sides of that comparison are true-referenced and only the
// *error* crosses into the magnetic-frame heading_command, declination,
// compass deviation, leeway, and current all cancel out of the loop instead
// of needing to be modeled - the boat is simply trimmed until its track
// matches the target, however it gets there.
//
// This is the outer half of a cascade; applyHeadingCommandTrim() (AutoPilot.cpp)
// is the inner half, doing the actual angle-wrap math and clamping so it stays
// alongside the rest of AutoPilot's locked state.

#define GPS_TRACK_TRIM_PERIOD_MS 10000  // outer loop cadence - long enough that the inner PID always settles before the next tick
#define GPS_TRACK_TRIM_GAIN         0.4 // fraction of the observed track error applied per tick
#define GPS_TRACK_TRIM_CLAMP_DEG   10.0 // max heading_command move per tick

#if XTE_STEERING_ENABLED
// Defined in crosstrack.ino. Reshapes bearing-to-mark into a leg-course +
// cross-track-error blended target when live Garmin BOD/XTE data is fresh;
// returns false (leaves *out_heading untouched) otherwise, meaning "use plain
// bearing-to-mark".
bool crosstrack_get_desired_heading(float bearing_to_mark, float distance_nm, float* out_heading);
#endif

void gps_track_trim() {
  static uint32_t last_tick = 0;
  uint32_t now = millis();
  if (now - last_tick < GPS_TRACK_TRIM_PERIOD_MS) {
    return;
  }
  last_tick = now;

  if (autoPilot.getMode() != 2 || !autoPilot.isNavigationEndabled() || !autoPilot.hasFix()) {
    return;  // nothing to trim - compass hold, disabled, or no position yet
  }
  if (!autoPilot.isDampedCourseValid()) {
    return;  // too slow / no trusted COG samples yet - hold heading_command as-is
  }

  float target = autoPilot.getBearing();  // plain bearing-to-mark, the default target

#if XTE_STEERING_ENABLED
  // Only meaningful for a live Garmin route (it has a leg origin -> line);
  // OpenCPN waypoints have no BOD/XTE and always fall back to bearing-to-mark.
  float xte_target;
  if (autoPilot.getNavSource() == 1 &&
      crosstrack_get_desired_heading(target, autoPilot.getDistance(), &xte_target)) {
    target = xte_target;
  }
#endif

  autoPilot.applyHeadingCommandTrim(target, GPS_TRACK_TRIM_GAIN, GPS_TRACK_TRIM_CLAMP_DEG);
}
