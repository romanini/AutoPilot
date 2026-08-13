// Nav-source arbitration — the unified two-source selector (NavigationEngagePlan §2).
//
// Tracks two independent navigation sources, GARMIN (live Garmin RMB stream) and
// OPENCPN (the plugin's `w` heartbeat), each carrying {dest, live, last_update}.
// navsource_tick() picks one and aims the controller at it:
//   GARMIN if live, else OPENCPN if live, else NONE.
//   selected != NONE -> setWaypoint(selected.dest) + setMode(2).
//   selected == NONE -> mode and the last commanded waypoint are left alone.
//                       We only stop announcing a live source (nav_source ->
//                       NONE). The controller keeps steering at the last
//                       waypoint it was told about, whether that's because the
//                       route ended or the source just went quiet -- end-of-
//                       route behavior is "circle the last mark" (decided
//                       2026-07-03), not compass-hold.
// Garmin wins when both are live; a non-selected source can never redirect the
// active steering. CORE RULE: this file NEVER calls setNavigationEnabled() — the
// operator always presses Enable. (The old "Follow-Garmin armed" auto-engage path
// has been deleted.)
//
// Threading: the GARMIN feed (navsource_garmin_rmb) and the selector
// (navsource_tick) run in command_task, but the OPENCPN feed
// (navsource_opencpn_waypoint / navsource_opencpn_clear) runs in the AsyncUDP
// task (process_udp_command). The file-scope source state is therefore touched
// from two tasks and is guarded by navsource_mux below. DEBUG prints and the
// autoPilot setters (already mutex-guarded internally) are kept OUTSIDE the
// critical section so we never hold the spinlock across a blocking/USB call.

#include <math.h>

#define GARMIN_NAV_TIMEOUT_MS  6000   // RMB silence after which a live Garmin source goes stale
#define OPENCPN_PROMOTE_MS     3000   // a 2nd `w` within this window promotes OPENCPN to live (Follow)
#define OPENCPN_NAV_TIMEOUT_MS 6000   // `w` heartbeat silence after which a live OpenCPN source goes stale

enum NavSrc { NAV_NONE, NAV_GARMIN, NAV_OPENCPN };

// One navigation source's tracked state.
struct NavSourceState {
  double   dest_lat;
  double   dest_lon;
  bool     have_pos;        // we have ever received a valid destination
  bool     live;            // currently a valid source to steer by
  bool     show_pending;    // OPENCPN only: a lone Set WP to surface once (no Mode 2)
  uint32_t last_update_ms;  // millis() of the last RMB / `w`
};

static portMUX_TYPE navsource_mux = portMUX_INITIALIZER_UNLOCKED;

static NavSourceState garmin  = { 0, 0, false, false, false, 0 };
static NavSourceState opencpn = { 0, 0, false, false, false, 0 };

// What the selector last applied to the autopilot. Touched only in command_task
// (navsource_tick / the telnet 's'-via-print accessor), so it needs no lock.
static NavSrc  applied_source = NAV_NONE;
static double  applied_lat    = NAN;
static double  applied_lon    = NAN;

// Called for each RMB parsed off the live Garmin channel (garmin.ino). status 'A'
// + position makes GARMIN live (and refreshes it); status 'V' clears it. The
// `arrival` field (RMB field 13) is intentionally unused for now: explicit
// arrival/alarm handling is a deferred TODO (NavigationEngagePlan §3).
void navsource_garmin_rmb(char status, double dest_lat, double dest_lon,
                          bool have_pos, char arrival) {
  (void)arrival;
  uint32_t now = millis();
  bool promoted = false, cleared = false;

  portENTER_CRITICAL(&navsource_mux);
  garmin.last_update_ms = now;
  if (status == 'A' && have_pos) {
    garmin.dest_lat = dest_lat;
    garmin.dest_lon = dest_lon;
    garmin.have_pos = true;
    promoted = !garmin.live;
    garmin.live = true;
  } else if (status == 'V') {
    cleared = garmin.live;
    garmin.live = false;
  }
  portEXIT_CRITICAL(&navsource_mux);

  if (promoted) DEBUG_PRINTLN("[navsource] GARMIN live (RMB 'A')");
  if (cleared)  DEBUG_PRINTLN("[navsource] GARMIN cleared (RMB 'V')");
}

// Called for every OpenCPN `w` (subscribe.ino, AsyncUDP task). Always records the
// destination; promotes OPENCPN to live only on a SUSTAINED heartbeat (a 2nd `w`
// within OPENCPN_PROMOTE_MS of the previous one). A lone Set WP click does not
// promote — it just flags the destination to be surfaced once (no Mode 2).
void navsource_opencpn_waypoint(double lat, double lon) {
  uint32_t now = millis();
  bool promoted = false;

  portENTER_CRITICAL(&navsource_mux);
  if (!opencpn.live) {
    if (opencpn.last_update_ms != 0 &&
        (now - opencpn.last_update_ms) <= OPENCPN_PROMOTE_MS) {
      opencpn.live = true;             // sustained stream -> Follow
      opencpn.show_pending = false;    // the live path owns the destination now
      promoted = true;
    } else {
      opencpn.show_pending = true;     // lone Set WP so far: surface it once
    }
  }
  opencpn.dest_lat = lat;
  opencpn.dest_lon = lon;
  opencpn.have_pos = true;
  opencpn.last_update_ms = now;
  portEXIT_CRITICAL(&navsource_mux);

  if (promoted) DEBUG_PRINTLN("[navsource] OPENCPN live (sustained heartbeat)");
}

// Called on `~APCMD,X$` (subscribe.ino, AsyncUDP task): the operator stopped
// Follow. Clear the OPENCPN source immediately instead of waiting out the
// timeout. last_update_ms is zeroed so a later lone `w` can't auto-promote off a
// stale heartbeat clock.
void navsource_opencpn_clear() {
  bool cleared;
  portENTER_CRITICAL(&navsource_mux);
  cleared = opencpn.live;
  opencpn.live = false;
  opencpn.show_pending = false;
  opencpn.last_update_ms = 0;
  portEXIT_CRITICAL(&navsource_mux);

  if (cleared) DEBUG_PRINTLN("[navsource] OPENCPN cleared ('X' / Follow stopped)");
}

// Periodic selector (command_task). Expires stale sources, picks the active one,
// and aims the autopilot at it. Only ever changes Mode (never nav-enable).
void navsource_tick() {
  uint32_t now = millis();

  // --- snapshot + timeout demotion of the cross-task source state ---
  bool   g_live, o_live, do_show = false;
  double g_lat, g_lon, o_lat, o_lon, show_lat = 0, show_lon = 0;
  bool   g_demoted = false, o_demoted = false;
  NavSrc selected;

  portENTER_CRITICAL(&navsource_mux);
  if (garmin.live && (now - garmin.last_update_ms) > GARMIN_NAV_TIMEOUT_MS) {
    garmin.live = false;
    g_demoted = true;
  }
  if (opencpn.live && (now - opencpn.last_update_ms) > OPENCPN_NAV_TIMEOUT_MS) {
    opencpn.live = false;
    opencpn.last_update_ms = 0;
    o_demoted = true;
  }
  g_live = garmin.live;  g_lat = garmin.dest_lat;  g_lon = garmin.dest_lon;
  o_live = opencpn.live; o_lat = opencpn.dest_lat; o_lon = opencpn.dest_lon;

  selected = g_live ? NAV_GARMIN : (o_live ? NAV_OPENCPN : NAV_NONE);

  // A lone Set WP is surfaced once, but only while nothing is actively steering
  // (so it can never redirect a live source). Consume the flag under the lock.
  if (opencpn.show_pending) {
    if (selected == NAV_NONE) {
      do_show = true;
      show_lat = opencpn.dest_lat;
      show_lon = opencpn.dest_lon;
    }
    // Consumed either way. If a source is live right now the pending
    // destination must be dropped, not deferred: were it left set, it would
    // fire whenever that source later went quiet - possibly hours on - and
    // silently swing the boat from the last commanded waypoint to a stale one.
    opencpn.show_pending = false;
  }
  portEXIT_CRITICAL(&navsource_mux);

  if (g_demoted) DEBUG_PRINTLN("[navsource] GARMIN stale (no RMB), demoted");
  if (o_demoted) DEBUG_PRINTLN("[navsource] OPENCPN stale (no heartbeat), demoted");

  // --- apply the selection (autoPilot setters are self-locked) ---
  if (selected != NAV_NONE) {
    double lat = (selected == NAV_GARMIN) ? g_lat : o_lat;
    double lon = (selected == NAV_GARMIN) ? g_lon : o_lon;
    bool source_switch = (selected != applied_source);
    if (source_switch || lat != applied_lat || lon != applied_lon) {
      autoPilot.setWaypoint((float)lat, (float)lon);
      applied_lat = lat;
      applied_lon = lon;
    }
    if (source_switch) {
      autoPilot.setMode(2);
      DEBUG_PRINT("[navsource] selected -> ");
      DEBUG_PRINTLN(selected == NAV_GARMIN ? "GARMIN" : "OPENCPN");
      applied_source = selected;
      autoPilot.setNavSource(applied_source);  // publish who's steering (APDAT)
    }
  } else {
    // No live source. End-of-route / source-silence policy: keep steering
    // (circling) the last commanded waypoint rather than falling back to
    // compass-hold. Mode and the waypoint are left exactly as last applied --
    // only the announced source changes, so telemetry honestly shows nobody
    // is currently feeding a destination.
    if (applied_source != NAV_NONE) {
      DEBUG_PRINTLN("[navsource] selected -> NONE (holding last waypoint, mode unchanged)");
      applied_source = NAV_NONE;
      autoPilot.setNavSource(NAV_NONE);  // publish who's steering (APDAT)
    }
    // Surface a lone Set WP destination (no mode change).
    if (do_show) {
      autoPilot.setWaypoint((float)show_lat, (float)show_lon);
    }
  }
}

// The source the selector is currently steering by (for the telnet 'p' readout).
// command_task-only access -> no lock needed.
const char* navsource_selected_name() {
  switch (applied_source) {
    case NAV_GARMIN:  return "GARMIN";
    case NAV_OPENCPN: return "OPENCPN";
    default:          return "NONE";
  }
}
