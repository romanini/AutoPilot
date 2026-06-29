// Nav-source arbitration — step 4: the GARMIN source ("Follow-Garmin") only.
//
// The full §7.3 state machine (OPENCPN source, liveness selector, failover,
// agreement check, nav_source telemetry) is step 6/7. This unit implements just
// Option 1: parse the live Garmin RMB stream (fed from garmin.ino) and, behind
// the operator "armed" guard, engage the controller's existing waypoint-navigate
// machinery toward the RMB destination.
//
// Threading: every function here runs in command_task (Garmin parse, telnet `f`
// arming, and the periodic tick all originate there), so the file-scope state
// below needs no mutex. The actual nav-state changes go through autoPilot's
// locked setters, which keeps them safe against control_task.

#define GARMIN_NAV_TIMEOUT_MS 6000   // RMB silence after which we drop a stale Garmin follow

static bool     follow_garmin_armed = false;  // operator safety guard (default: disarmed)
static bool     garmin_engaged = false;        // nav was auto-engaged *by* the Garmin source
static bool     garmin_active = false;         // last RMB had status 'A' (valid nav)
static uint32_t garmin_last_rmb_ms = 0;

// Arm/disarm "Follow-Garmin". Armed: an RMB with status 'A' auto-engages
// waypoint-navigate. Disarmed: RMB still populates the waypoint, but the operator
// presses Enable (preserves the project's "operator engages" safety pattern).
void navsource_set_armed(bool armed) {
  follow_garmin_armed = armed;
  if (!armed) {
    // Hand control back to the operator: stop treating nav as Garmin-driven.
    // Leave the current nav state as-is (don't yank the helm); the operator
    // decides whether to keep navigating or press Disable.
    garmin_engaged = false;
  }
}

bool navsource_is_armed()       { return follow_garmin_armed; }
bool navsource_garmin_active()  { return garmin_active; }

// Called for each RMB parsed off the live Garmin channel (garmin.ino).
//   status 'A' + position -> populate the waypoint; if armed, engage nav.
//   status 'V'            -> route complete / nav invalid; disengage if we engaged.
void navsource_garmin_rmb(char status, double dest_lat, double dest_lon,
                          bool have_pos, char arrival) {
  garmin_last_rmb_ms = millis();

  if (status == 'A' && have_pos) {
    garmin_active = true;
    // Populate the destination on every RMB so leg advances (new dest) track the
    // Garmin. setWaypoint recomputes distance; bearing follows on the next GPS fix.
    autoPilot.setWaypoint((float)dest_lat, (float)dest_lon);

    if (follow_garmin_armed) {
      autoPilot.setMode(2);                  // no-op-safe: waypoint_set is now true
      autoPilot.setNavigationEnabled(true);
      if (!garmin_engaged) {
        garmin_engaged = true;
        DEBUG_PRINTLN("[navsource] GARMIN engaged (armed, RMB 'A')");
      }
    }
  } else if (status == 'V') {
    garmin_active = false;
    if (garmin_engaged) {
      autoPilot.setNavigationEnabled(false);
      garmin_engaged = false;
      DEBUG_PRINTLN("[navsource] GARMIN disengaged (RMB 'V' / route end)");
    }
  }
}

// Periodic liveness guard (call from command_task). If a Garmin follow we engaged
// goes stale (RMB stream stopped — unplug, crash, out of range), disengage so we
// don't keep steering to a frozen waypoint. Only affects Garmin-engaged nav; a
// manually-engaged nav is left to the operator.
void navsource_tick() {
  if (garmin_engaged && (millis() - garmin_last_rmb_ms) > GARMIN_NAV_TIMEOUT_MS) {
    autoPilot.setNavigationEnabled(false);
    garmin_engaged = false;
    garmin_active = false;
    DEBUG_PRINTLN("[navsource] GARMIN stale (no RMB), disengaged");
  }
}
