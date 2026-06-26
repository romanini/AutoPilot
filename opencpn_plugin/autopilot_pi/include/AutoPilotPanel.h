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
    void OnPortShort(wxCommandEvent& event);
    void OnPortLong(wxCommandEvent& event);
    void OnStbdShort(wxCommandEvent& event);
    void OnStbdLong(wxCommandEvent& event);
    void OnUndock(wxCommandEvent& event);

    AutoPilotLink* m_link;
    DockMode       m_dock_mode;
    std::function<void()> m_undock_cb;

    // Left column: Speed + IMU
    wxStaticText* m_speed_val;
    wxStaticText* m_heading_val;
    wxStaticText* m_pitch_val;
    wxStaticText* m_roll_val;
    wxStaticText* m_stability_val;

    // Middle column: Destination + Bearing
    wxStaticText* m_destination_val;
    wxStaticText* m_bearing_val;
    wxStaticText* m_bearing_corr_val;

    // Right column: GPS data
    wxStaticText* m_distance_val;
    wxStaticText* m_course_val;
    wxStaticText* m_location_val;

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
    wxButton*   m_btn_undock;   // null in FLOAT mode
    wxCheckBox* m_chk_follow;

    bool   m_navigate_available;
    double m_navigate_lat;
    double m_navigate_lon;

    static const float ADJUSTMENT_SHORT;
    static const float ADJUSTMENT_LONG;

    wxDECLARE_EVENT_TABLE();
};
