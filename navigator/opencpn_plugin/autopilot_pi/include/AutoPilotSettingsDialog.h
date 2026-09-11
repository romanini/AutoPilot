#pragma once

#include <functional>
#include <wx/wx.h>
#include "AutoPilotLink.h"

// Modeless calibration window opened from the main panel's "Settings" button.
// Modeless (Show(), not ShowModal()) rather than a blocking dialog because the
// wind-vane trim buttons are meant to be tapped repeatedly while watching AWA
// update live - a modal dialog would freeze the panel's own telemetry refresh
// underneath it.
//
// None of the ~APCMD verbs this sends (z/v/d/k) have an ack (see the autopilot
// skill's rudder/wind calibration sections), so this window can show live
// *readings* (current rudder angle, current AWA/AWS) but never the currently
// stored offset/slope/calibration values - the caller confirms a change landed
// by watching the next reading move, same as everywhere else in this project.
class AutoPilotSettingsDialog : public wxDialog {
public:
    AutoPilotSettingsDialog(wxWindow* parent, AutoPilotLink* link);

    void UpdateFromState(const AutoPilotState& state, bool connected);

    // Invoked once, right before this window destroys itself (close box or
    // the Close button) so the owning panel can drop its now-dangling pointer.
    void SetCloseCallback(std::function<void()> cb) { m_close_cb = cb; }

private:
    void OnZeroRudder(wxCommandEvent& event);
    void OnZeroVane(wxCommandEvent& event);
    void OnVaneNudge(int degrees);
    void OnVaneNudgeM10(wxCommandEvent&) { OnVaneNudge(-10); }
    void OnVaneNudgeM1(wxCommandEvent&)  { OnVaneNudge(-1); }
    void OnVaneNudgeP1(wxCommandEvent&)  { OnVaneNudge(1); }
    void OnVaneNudgeP10(wxCommandEvent&) { OnVaneNudge(10); }
    void OnApplySpeedCal(wxCommandEvent& event);
    void OnClose(wxCloseEvent& event);
    void OnCloseButton(wxCommandEvent& event);

    AutoPilotLink* m_link;
    std::function<void()> m_close_cb;

    wxStaticText* m_rudder_val;
    wxButton*     m_btn_zero_rudder;

    wxStaticText* m_wind_val;
    wxButton*     m_btn_zero_vane;
    wxButton*     m_btn_vane_m10;
    wxButton*     m_btn_vane_m1;
    wxButton*     m_btn_vane_p1;
    wxButton*     m_btn_vane_p10;

    wxTextCtrl* m_txt_slope;
    wxTextCtrl* m_txt_offset;
    wxButton*   m_btn_apply_cal;

    bool m_nav_enabled;
    bool m_connected;

    wxDECLARE_EVENT_TABLE();
};
