#pragma once

#include <wx/wx.h>
#include <wx/socket.h>
#include <wx/timer.h>
#include "ocpn_plugin.h"
#include <map>
#include <vector>

// Mirrors the ~APDAT field layout from controller/publish.ino.
// All fields default to zero/false; "connected" is separate.
struct AutoPilotState {
    int  year, month, day, hour, minute;
    bool fix;
    int  fixquality, satellites;
    bool nav_enabled;
    int  mode;             // 0=off, 1=compass-hold, 2=waypoint-navigate
    bool waypoint_set;
    double wp_lat, wp_lon;
    double heading_desired;
    double heading;
    double pitch, roll;
    int    stability;      // raw classification integer
    double bearing, bearing_correction;
    double speed, distance, course;
    double location_lat, location_lon;
};

class AutoPilotPanel;

class AutoPilotLink : public wxEvtHandler {
public:
    explicit AutoPilotLink(AutoPilotPanel* panel);
    ~AutoPilotLink();

    void SetPanel(AutoPilotPanel* panel) { m_panel = panel; }

    bool Start();
    void Stop();

    bool IsConnected() const;
    const AutoPilotState& State() const { return m_state; }

    void SendMode(int mode);
    void SendNavEnable(bool enable);
    void SendAdjust(float degrees);
    void SendWaypoint(double lat, double lon);

    // §1c — wrap a single NMEA sentence as ~APTX and unicast to controller
    void SendNmea(const wxString& nmea_line);

    // §1c — serialize route → WPL+RTE → SendNmea each line.
    // short_id is the route identifier embedded in the RTE sentence (≤6 chars).
    // §3.3 route-activation spike result: use HostApi121::ActivateRoutePI(guid,true)
    // obtained from GetHostApi(); it is the supported call for activating a route
    // by GUID from inside a plugin.  De-dup logic (step 5) will live around
    // FlushInboundRoute() and will call this API after matching the received route
    // to an existing one.
    void SendRoute(const PlugIn_Route* route, const wxString& short_id = "OCPN01");

private:
    void OnTimer(wxTimerEvent& event);
    void DrainSocket();
    void ParsePacket(char* data);
    void ParseApdat(char* data);           // extracted body of old ParsePacket
    void ParseAprx(const char* nmea_line); // handle ~APRX frame contents

    // Per-sentence-type parsers called from ParseAprx
    bool ParseWplLine(const char* sentence);
    bool ParseRteLine(const char* sentence);
    void ParseRmbLine(const char* sentence);

    // Called when all RTE messages for one route have arrived
    void FlushInboundRoute();

    void SendCommand(const wxString& cmd);

    static char*  AdvanceField(char* p);
    static double NormalizeDegrees(double d);
    static double CourseCorrection(double bearing, double heading);
    static double GeodesicBearing(double lat1, double lon1, double lat2, double lon2);

    // NMEA helpers (§3.1 serialize path)
    static unsigned char NmeaXorChecksum(const wxString& body);
    static wxString      FormatWPL(const wxString& name, double lat, double lon);
    static wxArrayString FormatRTE(const wxString& id, const wxArrayString& names);
    static wxString      MakeShortId(const wxString& name, int index);

    // NMEA helpers (§3.1 parse path)
    static bool VerifyNmeaChecksum(const char* sentence);
    static void SplitNmeaFields(const char* sentence, std::vector<wxString>& out);

    AutoPilotPanel*    m_panel;
    wxDatagramSocket*  m_recv_sock;
    wxDatagramSocket*  m_send_sock;
    wxTimer            m_timer;
    AutoPilotState     m_state;
    wxLongLong         m_last_receive_ms;
    wxLongLong         m_suppress_until_ms;
    wxIPV4address      m_controller_addr;

    // Inbound WPL/RTE assembly (§3.1 parse path)
    struct InboundWpt { wxString name; double lat, lon; };
    std::map<wxString, InboundWpt>  m_wpl_buffer;  // name → position, from WPL lines
    std::vector<wxString>            m_rte_order;   // waypoint names in route order, from RTE
    wxString                         m_rte_id;      // route identifier from RTE field 4
    int                              m_rte_total;   // total RTE messages expected
    int                              m_rte_count;   // RTE messages received so far
    wxString                         m_rmb_dest;    // active dest name from last RMB A sentence

    static const int TIMEOUT_MS        = 10000;
    static const int POLL_INTERVAL_MS  = 250;
    static const int RECV_BUF_SIZE     = 512;
    static const int LOCAL_SUPPRESS_MS = 2000;

    wxDECLARE_EVENT_TABLE();
};
