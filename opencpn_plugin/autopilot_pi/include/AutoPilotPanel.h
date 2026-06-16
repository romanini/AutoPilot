#pragma once

#include <wx/wx.h>
#include <wx/timer.h>
#include "AutoPilotLink.h"

class AutoPilotPanel : public wxPanel {
public:
    AutoPilotPanel(wxWindow* parent, AutoPilotLink* link);
    ~AutoPilotPanel();

    // Called from AutoPilotLink's poll timer — refreshes display fields.
    void UpdateFromState(const AutoPilotState& state, bool connected);

    // Called from AutoPilotPlugin::SetActiveLegInfo.
    // Also called with available=true when active waypoint lat/lon are resolved.
    void SetActiveLeg(double btw, double dtw, const wxString& wp_name);
    void SetNavigateTarget(bool available, double lat = 0.0, double lon = 0.0);

private:
    void BuildUI();
    void OnModeCompass(wxCommandEvent& event);
    void OnModeWaypoint(wxCommandEvent& event);
    void OnNavToggle(wxCommandEvent& event);
    void OnPortShort(wxCommandEvent& event);
    void OnPortLong(wxCommandEvent& event);
    void OnStbdShort(wxCommandEvent& event);
    void OnStbdLong(wxCommandEvent& event);
    void OnNavigate(wxCommandEvent& event);

    AutoPilotLink*  m_link;

    // Status display
    wxStaticText* m_connected_label;
    wxStaticText* m_heading_val;
    wxStaticText* m_hdg_desired_val;
    wxStaticText* m_bearing_val;
    wxStaticText* m_bearing_corr_val;
    wxStaticText* m_mode_val;
    wxStaticText* m_nav_enabled_val;
    wxStaticText* m_gps_fix_val;
    wxStaticText* m_satellites_val;
    wxStaticText* m_location_val;
    wxStaticText* m_waypoint_val;
    wxStaticText* m_distance_val;
    wxStaticText* m_speed_val;
    wxStaticText* m_course_val;
    wxStaticText* m_active_leg_val;

    // Controls
    wxButton* m_btn_compass;
    wxButton* m_btn_waypoint;
    wxButton* m_btn_nav_toggle;
    wxButton* m_btn_navigate;

    // Current nav_enabled state, tracked for toggle button label
    bool m_nav_enabled;

    // Navigate target resolved from OpenCPN active waypoint
    bool   m_navigate_available;
    double m_navigate_lat;
    double m_navigate_lon;

    static const float ADJUSTMENT_SHORT;  // 1.0 deg, from display/button.ino
    static const float ADJUSTMENT_LONG;   // 10.0 deg

    wxDECLARE_EVENT_TABLE();
};
