#pragma once

#include <functional>
#include <wx/wx.h>
#include <wx/scrolwin.h>
#include <wx/timer.h>
#include <wx/checkbox.h>
#include "AutoPilotLink.h"

enum class DockMode { FLOAT, RIGHT, TOP_BOTTOM };

class AutoPilotPanel : public wxScrolledWindow {
public:
    AutoPilotPanel(wxWindow* parent, AutoPilotLink* link);
    ~AutoPilotPanel();

    void UpdateFromState(const AutoPilotState& state, bool connected);
    void SetNavigateTarget(bool available, double lat = 0.0, double lon = 0.0);
    void SetActiveRoute(const wxString& guid);

    // Returns true if the layout was actually rebuilt.
    bool SetDockMode(DockMode mode);
    void SetUndockCallback(std::function<void()> cb) { m_undock_cb = cb; }

    // Fixed content width for the floating layout.
    static const int kPanelW = 480;
    // Target pane width for right-dock mode; set this on wxAuiPaneInfo after rebuild.
    static const int kRightDockW = 100;

private:
    void BuildUI_Float();
    void BuildUI_Right();
    void BuildUI_TopBottom();

    void OnMode(wxCommandEvent& event);
    void OnNavToggle(wxCommandEvent& event);
    void OnSendWP(wxCommandEvent& event);
    void OnSendRoute(wxCommandEvent& event);
    void OnPortShort(wxCommandEvent& event);
    void OnPortLong(wxCommandEvent& event);
    void OnStbdShort(wxCommandEvent& event);
    void OnStbdLong(wxCommandEvent& event);
    void OnUndock(wxCommandEvent& event);
    void OnFollowChanged(wxCommandEvent& event);
    void OnHeartbeat(wxTimerEvent& event);

    AutoPilotLink* m_link;
    DockMode       m_dock_mode;
    std::function<void()> m_undock_cb;

    // Left column: Heading / Speed / Track
    wxStaticText* m_heading_val;
    wxStaticText* m_speed_val;
    wxStaticText* m_track_val;

    // Middle column: Mode (folds in nav_source, mirroring the display unit's
    // display_mode()) / Target + Correction (one box, no label on Correction)
    wxStaticText* m_mode_val;
    wxStaticText* m_target_val;
    wxStaticText* m_correction_val;

    // Right column: Location / Distance / Waypoint
    wxStaticText* m_location_val;
    wxStaticText* m_distance_val;
    wxStaticText* m_waypoint_val;

    // Bottom bar
    wxStaticText* m_datetime_val;
    wxStaticText* m_gpsfix_val;

    // Controls
    wxButton*   m_btn_port_long;
    wxButton*   m_btn_port_short;
    wxButton*   m_btn_stbd_short;
    wxButton*   m_btn_stbd_long;
    wxButton*   m_btn_mode;
    wxButton*   m_btn_nav_toggle;
    wxButton*   m_btn_send_wp;
    wxButton*   m_btn_send_route;
    wxButton*   m_btn_undock;   // null in FLOAT mode
    wxCheckBox* m_chk_follow;

    bool     m_navigate_available;
    double   m_navigate_lat;
    double   m_navigate_lon;
    wxString m_route_guid;      // active OpenCPN route GUID; empty when none
    wxTimer  m_heartbeat_timer;

    static const float ADJUSTMENT_SHORT;
    static const float ADJUSTMENT_LONG;
    static const int   HEARTBEAT_INTERVAL_MS;

    wxDECLARE_EVENT_TABLE();
};
