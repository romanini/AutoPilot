#include "wx/wxprec.h"
#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif

#include "AutoPilotLink.h"
#include "AutoPilotPanel.h"

#include <cstring>
#include <cstdlib>
#include <cmath>
#include <algorithm>

wxBEGIN_EVENT_TABLE(AutoPilotLink, wxEvtHandler)
    EVT_TIMER(wxID_ANY, AutoPilotLink::OnTimer)
wxEND_EVENT_TABLE()

AutoPilotLink::AutoPilotLink(AutoPilotPanel* panel)
    : m_panel(panel)
    , m_recv_sock(nullptr)
    , m_send_sock(nullptr)
    , m_timer(this)
    , m_state{}
    , m_last_receive_ms(0)
    , m_suppress_until_ms(0)
    , m_rte_total(0)
    , m_rte_count(0)
{
    m_controller_addr.Hostname("10.20.1.1");
    m_controller_addr.Service(8889);
}

AutoPilotLink::~AutoPilotLink() {
    Stop();
}

bool AutoPilotLink::Start() {
    // Receive socket: binds to 0.0.0.0:8888, receives subnet-directed broadcasts
    // from the controller (10.20.1.255:8888, see controller/publish.ino).
    wxIPV4address recv_addr;
    recv_addr.AnyAddress();
    recv_addr.Service(8888);
    m_recv_sock = new wxDatagramSocket(recv_addr, wxSOCKET_NOWAIT | wxSOCKET_REUSEADDR);
    if (!m_recv_sock->IsOk()) {
        delete m_recv_sock;
        m_recv_sock = nullptr;
        wxLogError("AutoPilotLink: failed to bind receive socket on port 8888");
        return false;
    }

    // Send socket: ephemeral local port, unicasts commands to controller.
    wxIPV4address send_addr;
    send_addr.AnyAddress();
    m_send_sock = new wxDatagramSocket(send_addr, wxSOCKET_NOWAIT);
    if (!m_send_sock->IsOk()) {
        delete m_send_sock;
        m_send_sock = nullptr;
        delete m_recv_sock;
        m_recv_sock = nullptr;
        wxLogError("AutoPilotLink: failed to create send socket");
        return false;
    }

    m_timer.Start(POLL_INTERVAL_MS);
    return true;
}

void AutoPilotLink::Stop() {
    m_timer.Stop();
    if (m_recv_sock) { m_recv_sock->Destroy(); m_recv_sock = nullptr; }
    if (m_send_sock) { m_send_sock->Destroy(); m_send_sock = nullptr; }
}

bool AutoPilotLink::IsConnected() const {
    if (m_last_receive_ms == 0) return false;
    wxLongLong age = wxGetLocalTimeMillis() - m_last_receive_ms;
    return age.ToLong() < TIMEOUT_MS;
}

void AutoPilotLink::SendMode(int mode) {
    if (mode == 1 || (mode == 2 && m_state.waypoint_set)) {
        m_state.mode = mode;
        if (mode == 1) {
            m_state.heading_desired = m_state.heading;
            m_state.bearing = m_state.heading_desired;
            m_state.bearing_correction = CourseCorrection(m_state.bearing, m_state.heading);
        } else if (mode == 2) {
            m_state.bearing = GeodesicBearing(m_state.location_lat, m_state.location_lon,
                                              m_state.wp_lat, m_state.wp_lon);
            m_state.bearing_correction = CourseCorrection(m_state.bearing, m_state.course);
        }
        m_suppress_until_ms = wxGetLocalTimeMillis() + LOCAL_SUPPRESS_MS;
        if (m_panel) m_panel->UpdateFromState(m_state, IsConnected());
    }
    SendCommand(wxString::Format("m%d", mode));
}

void AutoPilotLink::SendNavEnable(bool enable) {
    if (!m_state.nav_enabled && enable && m_state.mode == 1) {
        m_state.heading_desired = m_state.heading;
        m_state.bearing = m_state.heading_desired;
        m_state.bearing_correction = 0.0;
    }
    m_state.nav_enabled = enable;
    m_suppress_until_ms = wxGetLocalTimeMillis() + LOCAL_SUPPRESS_MS;
    if (m_panel) m_panel->UpdateFromState(m_state, IsConnected());
    SendCommand(wxString::Format("n%d", enable ? 1 : 0));
}

void AutoPilotLink::SendAdjust(float degrees) {
    if (m_state.mode == 2) {
        m_state.mode = 1;
        m_state.heading_desired = m_state.heading;
        m_state.bearing = m_state.heading_desired;
    }
    m_state.heading_desired = NormalizeDegrees(m_state.heading_desired + degrees);
    m_state.bearing = m_state.heading_desired;
    m_state.bearing_correction = CourseCorrection(m_state.bearing, m_state.heading);
    m_suppress_until_ms = wxGetLocalTimeMillis() + LOCAL_SUPPRESS_MS;
    if (m_panel) m_panel->UpdateFromState(m_state, IsConnected());
    SendCommand(wxString::Format("a%.2f", degrees));
}

void AutoPilotLink::SendWaypoint(double lat, double lon) {
    SendCommand(wxString::Format("w%.6f,%.6f", lat, lon));
}

void AutoPilotLink::SendStopFollow() {
    SendCommand("X");
}

// ---------------------------------------------------------------------------
// §1c — NMEA send path
// ---------------------------------------------------------------------------

void AutoPilotLink::SendNmea(const wxString& nmea_line) {
    if (!m_send_sock || !m_send_sock->IsOk()) return;
    wxString frame = wxString::Format("~APTX,%s$", nmea_line);
    wxCharBuffer buf = frame.ToAscii();
    m_send_sock->SendTo(m_controller_addr, buf.data(), buf.length());
}

void AutoPilotLink::SendRoute(const PlugIn_Route* route, const wxString& short_id) {
    if (!route || !route->pWaypointList) return;

    // Build short names and send WPL for each waypoint first.
    wxArrayString names;
    std::vector<std::pair<double,double>> positions;
    Plugin_WaypointList::Node* node = route->pWaypointList->GetFirst();
    int idx = 0;
    while (node) {
        PlugIn_Waypoint* wp = node->GetData();
        if (wp) {
            wxString sname = MakeShortId(wp->m_MarkName, idx);
            names.Add(sname);
            positions.push_back({wp->m_lat, wp->m_lon});
            SendNmea(FormatWPL(sname, wp->m_lat, wp->m_lon));
        }
        node = node->GetNext();
        idx++;
    }

    // Then send RTE sentence(s) — split into chunks to keep each line ≤80 chars.
    for (const wxString& rte : FormatRTE(short_id, names))
        SendNmea(rte);

    // Record this send so the inbound echo can be recognised and de-duped (Phase C §3.3).
    // Key 1 match in FlushInboundRoute() uses short_id → guid to avoid creating a duplicate.
    SentRoute sr;
    sr.guid      = route->m_GUID;
    sr.positions = positions;
    sr.sent_ms   = wxGetLocalTimeMillis();
    m_sent_routes[short_id] = sr;
}

// ---------------------------------------------------------------------------
// §1c — NMEA serialization helpers
// ---------------------------------------------------------------------------

// XOR checksum of the sentence body (the chars between '$' and '*').
unsigned char AutoPilotLink::NmeaXorChecksum(const wxString& body) {
    unsigned char cs = 0;
    for (size_t i = 0; i < body.length(); i++)
        cs ^= (unsigned char)body[i];
    return cs;
}

// Build a $GPWPL sentence.  Name is already truncated to a short ID.
wxString AutoPilotLink::FormatWPL(const wxString& name, double lat, double lon) {
    char ns = lat >= 0 ? 'N' : 'S';
    char ew = lon >= 0 ? 'E' : 'W';
    double alat = fabs(lat), alon = fabs(lon);
    int dlat = (int)alat, dlon = (int)alon;
    double mlat = (alat - dlat) * 60.0;
    double mlon = (alon - dlon) * 60.0;
    // Keep minutes from rounding up to 60.0
    if (mlat >= 60.0) { mlat -= 60.0; dlat++; }
    if (mlon >= 60.0) { mlon -= 60.0; dlon++; }

    // DDMM.MMMM / DDDMM.MMMM — %07.4f gives exactly MM.MMMM (7 chars, zero-padded)
    wxString body = wxString::Format("GPWPL,%02d%07.4f,%c,%03d%07.4f,%c,%s",
                                     dlat, mlat, ns, dlon, mlon, ew, name);
    return wxString::Format("$%s*%02X", body, (unsigned)NmeaXorChecksum(body));
}

// Build all $GPRTE sentences for a route, splitting at 8 waypoints per message
// to stay well under the 80-char NMEA line limit.
wxArrayString AutoPilotLink::FormatRTE(const wxString& id, const wxArrayString& names) {
    wxArrayString out;
    const int CHUNK = 8;
    int total = ((int)names.size() + CHUNK - 1) / CHUNK;
    if (total == 0) total = 1;

    for (int msg = 0; msg < total; msg++) {
        wxString body = wxString::Format("GPRTE,%d,%d,C,%s", total, msg + 1, id);
        int end = std::min((int)names.size(), (msg + 1) * CHUNK);
        for (int i = msg * CHUNK; i < end; i++)
            body += "," + names[i];
        out.Add(wxString::Format("$%s*%02X", body, (unsigned)NmeaXorChecksum(body)));
    }
    return out;
}

// Return a ≤6-char uppercase alphanumeric ID derived from the waypoint name.
// Falls back to WPnnn when the name has no usable characters.
wxString AutoPilotLink::MakeShortId(const wxString& name, int index) {
    wxString s;
    for (size_t i = 0; i < name.length() && (int)s.length() < 6; i++) {
        wxChar c = wxToupper(name[i]);
        if (wxIsalnum(c)) s += c;
    }
    if (s.IsEmpty())
        s = wxString::Format("WP%03d", index);
    return s;
}

// ---------------------------------------------------------------------------
// §1c — NMEA parse path (inbound ~APRX frames)
// ---------------------------------------------------------------------------

// Verify the *XX checksum at the end of a standard NMEA sentence.
bool AutoPilotLink::VerifyNmeaChecksum(const char* sentence) {
    const char* dollar = strchr(sentence, '$');
    if (!dollar) return false;
    const char* star = strchr(dollar, '*');
    if (!star || strlen(star) < 3) return false;

    unsigned char cs = 0;
    for (const char* p = dollar + 1; p < star; p++)
        cs ^= (unsigned char)*p;

    char expected[3];
    snprintf(expected, sizeof(expected), "%02X", cs);
    return strncasecmp(star + 1, expected, 2) == 0;
}

// Split "$GPXXX,f1,f2,...,fN*CC" into ["GPXXX","f1","f2",...,"fN"].
// Stops at '*' so the checksum is not included.
void AutoPilotLink::SplitNmeaFields(const char* sentence,
                                     std::vector<wxString>& out) {
    const char* p = sentence;
    if (*p == '$') p++;
    while (*p && *p != '*') {
        const char* start = p;
        while (*p && *p != ',' && *p != '*') p++;
        out.push_back(wxString(start, p - start));
        if (*p == ',') p++;
    }
}

// Dispatch an inbound ~APRX payload (the raw NMEA line after "APRX,").
void AutoPilotLink::ParseAprx(const char* nmea_line) {
    if (!VerifyNmeaChecksum(nmea_line)) {
        wxLogWarning("AutoPilot: ~APRX checksum fail: %s", nmea_line);
        return;
    }

    // Identify sentence type: skip '$' + 2-char talker ID, read 3-char type.
    const char* p = nmea_line;
    while (*p && *p != '$') p++;
    if (!*p || strlen(p) < 6) return;
    const char* type = p + 3;  // p+1 skips '$', p+3 skips talker (GP/II/etc.)

    if (strncmp(type, "WPL", 3) == 0)
        ParseWplLine(nmea_line);
    else if (strncmp(type, "RTE", 3) == 0)
        ParseRteLine(nmea_line);
    else if (strncmp(type, "RMB", 3) == 0)
        ParseRmbLine(nmea_line);
}

// Parse $GPWPL,DDMM.MMMM,N,DDDMM.MMMM,E,NAME*XX → add to m_wpl_buffer.
bool AutoPilotLink::ParseWplLine(const char* sentence) {
    std::vector<wxString> f;
    SplitNmeaFields(sentence, f);
    // f: [0]=GPWPL [1]=lat [2]=N/S [3]=lon [4]=E/W [5]=name
    if (f.size() < 6) return false;

    double lat_raw = wxAtof(f[1]);
    int    lat_deg = (int)(lat_raw / 100);
    double lat     = lat_deg + (lat_raw - lat_deg * 100.0) / 60.0;
    if (f[2] == "S") lat = -lat;

    double lon_raw = wxAtof(f[3]);
    int    lon_deg = (int)(lon_raw / 100);
    double lon     = lon_deg + (lon_raw - lon_deg * 100.0) / 60.0;
    if (f[4] == "W") lon = -lon;

    wxString name = f[5];
    m_wpl_buffer[name] = {name, lat, lon};
    return true;
}

// Parse $GPRTE,<total>,<n>,C,<id>,<wp1>,<wp2>,...*XX → accumulate, flush when done.
bool AutoPilotLink::ParseRteLine(const char* sentence) {
    std::vector<wxString> f;
    SplitNmeaFields(sentence, f);
    // f: [0]=GPRTE [1]=total [2]=msg_n [3]=C/W [4]=route_id [5..]=wp names
    if (f.size() < 5) return false;

    long total = 1, msgn = 1;
    f[1].ToLong(&total);
    f[2].ToLong(&msgn);

    if (msgn == 1) {
        // First (or only) message: start fresh
        m_rte_order.clear();
        m_rte_id    = f[4];
        m_rte_total = (int)total;
        m_rte_count = 0;
    }

    for (size_t i = 5; i < f.size(); i++)
        m_rte_order.push_back(f[i]);

    m_rte_count++;

    if (m_rte_count >= m_rte_total)
        FlushInboundRoute();

    return true;
}

// Parse $GPRMB,A,...,<dest_name>,...*XX → track active dest for de-dup (step 5).
// Status V (not navigating) clears the dest.
void AutoPilotLink::ParseRmbLine(const char* sentence) {
    std::vector<wxString> f;
    SplitNmeaFields(sentence, f);
    // f: [0]=GPRMB [1]=A/V [2]=xte [3]=L/R [4]=orig [5]=dest ...
    if (f.size() < 6) return;

    if (f[1] == "A")
        m_rmb_dest = f[5];
    else
        m_rmb_dest.Clear();
}

// Phase C §3.3 — geometry search helper.
// Iterates all local OpenCPN routes and returns the GUID of the first whose
// ordered waypoint positions all match pts within ROUTE_MATCH_EPSILON degrees.
// Returns an empty string when no match is found.
wxString AutoPilotLink::FindMatchingLocalRoute(
        const std::vector<std::pair<double,double>>& pts) const
{
    if (pts.empty()) return wxString();

    wxArrayString guids = GetRouteGUIDArray();
    for (size_t i = 0; i < guids.GetCount(); i++) {
        auto route = GetRoute_Plugin(guids[i]);
        if (!route || !route->pWaypointList) continue;
        if ((size_t)route->pWaypointList->GetCount() != pts.size()) continue;

        bool match = true;
        Plugin_WaypointList::Node* node = route->pWaypointList->GetFirst();
        for (size_t j = 0; j < pts.size() && node; j++, node = node->GetNext()) {
            PlugIn_Waypoint* wp = node->GetData();
            if (!wp || fabs(wp->m_lat - pts[j].first)  > ROUTE_MATCH_EPSILON
                    || fabs(wp->m_lon - pts[j].second) > ROUTE_MATCH_EPSILON) {
                match = false;
                break;
            }
        }
        if (match) return guids[i];
    }
    return wxString();
}

// Called when all RTE messages for one route have arrived (Phase C §3.3).
//
// De-dup logic (two keys, used in order):
//   Key 1 — name-tag:  m_rte_id found in m_sent_routes → we sent this route →
//            activate the existing local GUID, no creation.
//   Key 2 — geometry:  compare inbound WP positions against every local route
//            within ROUTE_MATCH_EPSILON → same result if found.
//   Neither → genuinely Garmin-originated → AddPlugInRoute + activate.
//
// Activation: HostApi121::ActivateRoutePI(guid, true) — spike confirmed (§3.3).
void AutoPilotLink::FlushInboundRoute() {
    wxLogMessage("AutoPilot: ~APRX route assembled: id='%s', %d waypoints",
                 m_rte_id, (int)m_rte_order.size());

    // Build inbound geometry: ordered positions for the waypoints we have.
    std::vector<std::pair<double,double>> inbound_pts;
    for (const wxString& name : m_rte_order) {
        auto it = m_wpl_buffer.find(name);
        if (it != m_wpl_buffer.end()) {
            inbound_pts.push_back({it->second.lat, it->second.lon});
            wxLogMessage("  WP '%s'  %.6f, %.6f", name, it->second.lat, it->second.lon);
        } else {
            wxLogMessage("  WP '%s'  (no WPL received)", name);
        }
    }

    // -----------------------------------------------------------------------
    // Key 1 — name-tag match: did we push a route with this ID to the Garmin?
    // -----------------------------------------------------------------------
    wxString found_guid;
    {
        auto it = m_sent_routes.find(m_rte_id);
        if (it != m_sent_routes.end()) {
            found_guid = it->second.guid;
            wxLogMessage("AutoPilot: de-dup Key1 match id='%s' → guid %s",
                         m_rte_id, found_guid);
        }
    }

    // -----------------------------------------------------------------------
    // Key 2 — geometry match fallback (catches routes sent before this session,
    // routes whose short_id got truncated/changed by the Garmin, or manual re-sends).
    // -----------------------------------------------------------------------
    if (found_guid.IsEmpty() && !inbound_pts.empty()) {
        found_guid = FindMatchingLocalRoute(inbound_pts);
        if (!found_guid.IsEmpty())
            wxLogMessage("AutoPilot: de-dup Key2 geometry match → guid %s", found_guid);
    }

    // -----------------------------------------------------------------------
    // Activate existing route, or create a new one and then activate it.
    // -----------------------------------------------------------------------
    // Keep `host` alive through both branches — GetHostApi() returns an owning
    // unique_ptr, so calling .get() on a temporary yields a dangling pointer at
    // the very next statement.
    auto host = GetHostApi();
    auto* api = dynamic_cast<HostApi121*>(host.get());

    if (!found_guid.IsEmpty()) {
        // Route already in OpenCPN — just activate it (idempotent guard).
        if (api) {
            if (!api->IsRouteActive(found_guid)) {
                api->ActivateRoutePI(found_guid, true);
                wxLogMessage("AutoPilot: activated existing route %s", found_guid);
            } else {
                wxLogMessage("AutoPilot: route %s already active, no-op", found_guid);
            }
        }
    } else if (!inbound_pts.empty()) {
        // Genuinely Garmin-originated route → build, add, then activate.
        PlugIn_Route* nr = new PlugIn_Route();
        // Set a deterministic GUID so we can activate it immediately after AddPlugInRoute.
        nr->m_GUID = wxString::Format("AP-%s-%lld",
                                       m_rte_id, wxGetLocalTimeMillis().GetValue());
        nr->m_NameString = wxString::Format("Garmin: %s", m_rte_id);
        nr->pWaypointList = new Plugin_WaypointList();

        for (const wxString& name : m_rte_order) {
            auto it = m_wpl_buffer.find(name);
            if (it != m_wpl_buffer.end()) {
                PlugIn_Waypoint* wp = new PlugIn_Waypoint(
                    it->second.lat, it->second.lon, "circle", name);
                nr->pWaypointList->Append(wp);
            }
        }

        wxString new_guid = nr->m_GUID;
        bool added = AddPlugInRoute(nr, /*permanent=*/true);
        delete nr;  // OpenCPN takes a deep copy; we own the shell

        if (added && api) {
            api->ActivateRoutePI(new_guid, true);
            wxLogMessage("AutoPilot: created + activated new route '%s' guid=%s",
                         m_rte_id, new_guid);
        } else {
            wxLogMessage("AutoPilot: AddPlugInRoute failed for id='%s' (added=%d api=%p)",
                         m_rte_id, (int)added, (void*)api);
        }
    }

    m_wpl_buffer.clear();
    m_rte_order.clear();
}

// ---------------------------------------------------------------------------
// Command / socket internals
// ---------------------------------------------------------------------------

void AutoPilotLink::SendCommand(const wxString& cmd) {
    if (!m_send_sock || !m_send_sock->IsOk()) return;
    wxString frame = wxString::Format("~APCMD,%s$", cmd);
    wxCharBuffer buf = frame.ToAscii();
    m_send_sock->SendTo(m_controller_addr, buf.data(), buf.length());
}

// Advance past the next comma; returns pointer to first char of next field,
// or nullptr if no more commas. Mirrors display/AutoPilot.cpp::advance_field.
char* AutoPilotLink::AdvanceField(char* p) {
    if (!p) return nullptr;
    char* comma = strchr(p, ',');
    return comma ? comma + 1 : nullptr;
}

// ---------------------------------------------------------------------------
// Packet dispatch — handles both ~APDAT and ~APRX frames
// ---------------------------------------------------------------------------

void AutoPilotLink::ParsePacket(char* data) {
    // data has been stripped of the leading '~' and trailing '$'.
    if (strncmp(data, "APDAT,", 6) == 0)
        ParseApdat(data);
    else if (strncmp(data, "APRX,", 5) == 0)
        ParseAprx(data + 5);
}

void AutoPilotLink::ParseApdat(char* data) {
    char* p = data;  // starts at 'A' of "APDAT"

    // Helper lambda: read int field, advance p, return value (0 on empty/null).
    auto nextInt = [&]() -> int {
        p = AdvanceField(p);
        return (p && *p != ',' && *p != '\0') ? atoi(p) : 0;
    };
    auto nextDouble = [&]() -> double {
        p = AdvanceField(p);
        return (p && *p != ',' && *p != '\0') ? atof(p) : 0.0;
    };

    AutoPilotState s{};

    s.year            = nextInt();
    s.month           = nextInt();
    s.day             = nextInt();
    s.hour            = nextInt();
    s.minute          = nextInt();
    s.fix             = nextInt() != 0;
    s.fixquality      = nextInt();
    s.satellites      = nextInt();
    s.nav_enabled     = nextInt() != 0;
    s.mode            = nextInt();
    s.waypoint_set    = nextInt() != 0;
    s.wp_lat          = nextDouble();
    s.wp_lon          = nextDouble();
    s.heading_desired = nextDouble();
    s.heading         = nextDouble();
    nextDouble();  // pitch — not displayed by this plugin, skip over
    nextDouble();  // roll — not displayed by this plugin, skip over
    nextInt();     // stability classification — not displayed by this plugin, skip over
    s.bearing         = nextDouble();
    s.bearing_correction = nextDouble();
    s.speed           = nextDouble();
    s.distance        = nextDouble();
    s.course          = nextDouble();
    s.location_lat    = nextDouble();
    s.location_lon    = nextDouble();
    // Phase B: nav_source; absent on pre-B firmware → nextInt returns 0 (NONE).
    s.nav_source      = nextInt();
    nextInt();  // autoTuneState — not used by this plugin, skip over
    // Damped GPS track; absent on older firmware → 0.0/false (shown as "--").
    s.cog_damped       = nextDouble();
    s.cog_damped_valid = nextInt() != 0;

    // Preserve locally-commanded fields for LOCAL_SUPPRESS_MS after a button press,
    // matching the display unit's localCommandTime suppression in AutoPilot.cpp.
    if (wxGetLocalTimeMillis() < m_suppress_until_ms) {
        s.nav_enabled        = m_state.nav_enabled;
        s.mode               = m_state.mode;
        s.heading_desired    = m_state.heading_desired;
        s.bearing            = m_state.bearing;
        s.bearing_correction = m_state.bearing_correction;
    }
    m_last_receive_ms = wxGetLocalTimeMillis();
    m_state = s;

    if (m_panel) {
        m_panel->UpdateFromState(m_state, true);
    }
}

double AutoPilotLink::NormalizeDegrees(double d) {
    d = fmod(d, 360.0);
    if (d < 0.0) d += 360.0;
    return d;
}

double AutoPilotLink::CourseCorrection(double bearing, double heading) {
    double c = bearing - heading;
    if (c > 180.0) c -= 360.0;
    else if (c < -180.0) c += 360.0;
    return c;
}

double AutoPilotLink::GeodesicBearing(double lat1, double lon1, double lat2, double lon2) {
    const double D2R = M_PI / 180.0;
    const double R2D = 180.0 / M_PI;
    double y = sin((lon2 - lon1) * D2R) * cos(lat2 * D2R);
    double x = cos(lat1 * D2R) * sin(lat2 * D2R)
             - sin(lat1 * D2R) * cos(lat2 * D2R) * cos((lon2 - lon1) * D2R);
    return NormalizeDegrees(atan2(y, x) * R2D);
}

void AutoPilotLink::DrainSocket() {
    if (!m_recv_sock || !m_recv_sock->IsOk()) return;

    char buf[RECV_BUF_SIZE];
    wxIPV4address from;

    while (true) {
        m_recv_sock->RecvFrom(from, buf, sizeof(buf) - 1);
        size_t got = m_recv_sock->LastReadCount();
        // wxSOCKET_NOWAIT: "no data available" sets wxSOCKET_WOULDBLOCK, not
        // an actual error; either way a 0-byte read means nothing more to drain.
        if (got == 0) break;

        buf[got] = '\0';

        // Strip frame: find leading '~', trailing '$'.
        // Use strrchr (last '$') so that ~APRX packets whose NMEA payload
        // starts with '$' are not truncated at the inner '$'.
        char* start = strchr(buf, '~');
        if (!start) continue;
        start++;  // skip '~'
        char* end = strrchr(start, '$');
        if (!end) continue;
        *end = '\0';

        ParsePacket(start);
    }
}

void AutoPilotLink::OnTimer(wxTimerEvent&) {
    DrainSocket();

    // Update panel with staleness status even if no new packet arrived.
    if (m_panel && !IsConnected() && m_last_receive_ms != 0) {
        m_panel->UpdateFromState(m_state, false);
    }
}
