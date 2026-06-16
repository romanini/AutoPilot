#pragma once

#include <wx/wx.h>
#include <wx/socket.h>
#include <wx/timer.h>

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

private:
    void OnTimer(wxTimerEvent& event);
    void DrainSocket();
    void ParsePacket(char* data);
    void SendCommand(const wxString& cmd);

    static char* AdvanceField(char* p);

    AutoPilotPanel*    m_panel;
    wxDatagramSocket*  m_recv_sock;
    wxDatagramSocket*  m_send_sock;
    wxTimer            m_timer;
    AutoPilotState     m_state;
    wxLongLong         m_last_receive_ms;
    wxIPV4address      m_controller_addr;

    static const int TIMEOUT_MS      = 10000;
    static const int POLL_INTERVAL_MS = 250;
    static const int RECV_BUF_SIZE   = 512;

    wxDECLARE_EVENT_TABLE();
};
