#include "wx/wxprec.h"
#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif

#include "AutoPilotLink.h"
#include "AutoPilotPanel.h"

#include <cstring>
#include <cstdlib>
#include <cmath>

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

void AutoPilotLink::ParsePacket(char* data) {
    // data has been stripped of the leading '~' and trailing '$'.
    // Expected prefix: "APDAT,..."
    if (strncmp(data, "APDAT,", 6) != 0) return;

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
    s.pitch           = nextDouble();
    s.roll            = nextDouble();
    s.stability       = nextInt();
    s.bearing         = nextDouble();
    s.bearing_correction = nextDouble();
    s.speed           = nextDouble();
    s.distance        = nextDouble();
    s.course          = nextDouble();
    s.location_lat    = nextDouble();
    s.location_lon    = nextDouble();

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

        // Strip frame: find leading '~', trailing '$'
        char* start = strchr(buf, '~');
        if (!start) continue;
        start++;  // skip '~'
        char* end = strchr(start, '$');
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
