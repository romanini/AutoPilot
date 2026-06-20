#pragma once

#include <wx/wx.h>
#include <wx/scrolwin.h>
#include <wx/timer.h>
#include <wx/checkbox.h>
#include "AutoPilotLink.h"

class AutoPilotPanel : public wxScrolledWindow {
public:
    AutoPilotPanel(wxWindow* parent, AutoPilotLink* link);
    ~AutoPilotPanel();

    void UpdateFromState(const AutoPilotState& state, bool connected);
    void SetNavigateTarget(bool available, double lat = 0.0, double lon = 0.0);

    // Fixed content dimensions (pixels).
    // Box heights derived from TFT pixel values (screen.ino), scaled so each
    // column is 200px tall — which at typical Linux GTK scaling renders to
    // roughly the 480×320 TFT data area footprint.
    static const int kPanelW = 480;   // fixed content width; height computed dynamically in BuildUI

private:
    void BuildUI();
    void OnMode(wxCommandEvent& event);
    void OnNavToggle(wxCommandEvent& event);
    void OnSendWP(wxCommandEvent& event);
    void OnPortShort(wxCommandEvent& event);
    void OnPortLong(wxCommandEvent& event);
    void OnStbdShort(wxCommandEvent& event);
    void OnStbdLong(wxCommandEvent& event);

    AutoPilotLink* m_link;

    // Left column: Speed + IMU  (TFT left pane, CYAN/YELLOW)
    wxStaticText* m_speed_val;
    wxStaticText* m_heading_val;
    wxStaticText* m_pitch_val;
    wxStaticText* m_roll_val;
    wxStaticText* m_stability_val;

    // Middle column: Destination (mode + target) + Bearing  (TFT centre pane)
    wxStaticText* m_destination_val;
    wxStaticText* m_bearing_val;
    wxStaticText* m_bearing_corr_val;

    // Right column: GPS data  (TFT right pane)
    wxStaticText* m_distance_val;
    wxStaticText* m_course_val;
    wxStaticText* m_location_val;

    // Bottom bar
    wxStaticText* m_datetime_val;
    wxStaticText* m_gpsfix_val;

    // Controls — single row: mode + adjust + nav toggle; Send WP + Follow embedded in data area
    wxButton*   m_btn_port_long;
    wxButton*   m_btn_port_short;
    wxButton*   m_btn_stbd_short;
    wxButton*   m_btn_stbd_long;
    wxButton*   m_btn_mode;
    wxButton*   m_btn_nav_toggle;
    wxButton*   m_btn_send_wp;
    wxCheckBox* m_chk_follow;

    bool   m_navigate_available;
    double m_navigate_lat;
    double m_navigate_lon;

    static const float ADJUSTMENT_SHORT;
    static const float ADJUSTMENT_LONG;

    wxDECLARE_EVENT_TABLE();
};
