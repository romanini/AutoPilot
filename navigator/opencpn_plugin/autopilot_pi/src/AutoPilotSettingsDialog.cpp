#include "wx/wxprec.h"
#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif

#include "AutoPilotSettingsDialog.h"

enum {
    ID_BTN_ZERO_RUDDER = wxID_HIGHEST + 100,
    ID_BTN_ZERO_VANE,
    ID_BTN_VANE_M10,
    ID_BTN_VANE_M1,
    ID_BTN_VANE_P1,
    ID_BTN_VANE_P10,
    ID_BTN_APPLY_CAL,
    ID_BTN_CLOSE,
};

wxBEGIN_EVENT_TABLE(AutoPilotSettingsDialog, wxDialog)
    EVT_BUTTON(ID_BTN_ZERO_RUDDER, AutoPilotSettingsDialog::OnZeroRudder)
    EVT_BUTTON(ID_BTN_ZERO_VANE,   AutoPilotSettingsDialog::OnZeroVane)
    EVT_BUTTON(ID_BTN_VANE_M10,    AutoPilotSettingsDialog::OnVaneNudgeM10)
    EVT_BUTTON(ID_BTN_VANE_M1,     AutoPilotSettingsDialog::OnVaneNudgeM1)
    EVT_BUTTON(ID_BTN_VANE_P1,     AutoPilotSettingsDialog::OnVaneNudgeP1)
    EVT_BUTTON(ID_BTN_VANE_P10,    AutoPilotSettingsDialog::OnVaneNudgeP10)
    EVT_BUTTON(ID_BTN_APPLY_CAL,   AutoPilotSettingsDialog::OnApplySpeedCal)
    EVT_BUTTON(ID_BTN_CLOSE,       AutoPilotSettingsDialog::OnCloseButton)
    EVT_CLOSE(AutoPilotSettingsDialog::OnClose)
wxEND_EVENT_TABLE()

AutoPilotSettingsDialog::AutoPilotSettingsDialog(wxWindow* parent, AutoPilotLink* link)
    : wxDialog(parent, wxID_ANY, "AutoPilot Settings", wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE)
    , m_link(link)
    , m_nav_enabled(false)
    , m_connected(false)
{
    auto* root = new wxBoxSizer(wxVERTICAL);
    const int kPad = 6;

    // ── Rudder ───────────────────────────────────────────────────────────
    auto* rudder_box = new wxStaticBoxSizer(wxVERTICAL, this, "Rudder");
    {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(new wxStaticText(this, wxID_ANY, "Current angle:"), 0,
                 wxALIGN_CENTER_VERTICAL | wxRIGHT, kPad);
        m_rudder_val = new wxStaticText(this, wxID_ANY, "--");
        row->Add(m_rudder_val, 0, wxALIGN_CENTER_VERTICAL);
        rudder_box->Add(row, 0, wxALL, kPad);

        m_btn_zero_rudder = new wxButton(this, ID_BTN_ZERO_RUDDER, "Zero Rudder");
        rudder_box->Add(m_btn_zero_rudder, 0, wxLEFT | wxRIGHT | wxBOTTOM, kPad);

        rudder_box->Add(new wxStaticText(this, wxID_ANY,
            "Disabled while navigation is enabled."), 0, wxLEFT | wxRIGHT | wxBOTTOM, kPad);
    }
    root->Add(rudder_box, 0, wxEXPAND | wxALL, kPad);

    // ── Wind Vane ────────────────────────────────────────────────────────
    auto* vane_box = new wxStaticBoxSizer(wxVERTICAL, this, "Wind Vane");
    {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(new wxStaticText(this, wxID_ANY, "Apparent wind:"), 0,
                 wxALIGN_CENTER_VERTICAL | wxRIGHT, kPad);
        m_wind_val = new wxStaticText(this, wxID_ANY, "--");
        row->Add(m_wind_val, 0, wxALIGN_CENTER_VERTICAL);
        vane_box->Add(row, 0, wxALL, kPad);

        m_btn_zero_vane = new wxButton(this, ID_BTN_ZERO_VANE, "Zero Vane");
        vane_box->Add(m_btn_zero_vane, 0, wxLEFT | wxRIGHT | wxBOTTOM, kPad);

        auto* trim_row = new wxBoxSizer(wxHORIZONTAL);
        trim_row->Add(new wxStaticText(this, wxID_ANY, "Trim:"), 0,
                      wxALIGN_CENTER_VERTICAL | wxRIGHT, kPad);
        m_btn_vane_m10 = new wxButton(this, ID_BTN_VANE_M10, "-10\xc2\xb0");
        m_btn_vane_m1  = new wxButton(this, ID_BTN_VANE_M1,  "-1\xc2\xb0");
        m_btn_vane_p1  = new wxButton(this, ID_BTN_VANE_P1,  "+1\xc2\xb0");
        m_btn_vane_p10 = new wxButton(this, ID_BTN_VANE_P10, "+10\xc2\xb0");
        trim_row->Add(m_btn_vane_m10, 0, wxRIGHT, 2);
        trim_row->Add(m_btn_vane_m1,  0, wxRIGHT, 2);
        trim_row->Add(m_btn_vane_p1,  0, wxRIGHT, 2);
        trim_row->Add(m_btn_vane_p10, 0);
        vane_box->Add(trim_row, 0, wxLEFT | wxRIGHT | wxBOTTOM, kPad);
    }
    root->Add(vane_box, 0, wxEXPAND | wxALL, kPad);

    // ── Wind Speed Calibration ───────────────────────────────────────────
    auto* speed_box = new wxStaticBoxSizer(wxVERTICAL, this, "Wind Speed Calibration (advanced)");
    {
        auto* grid = new wxFlexGridSizer(2, 2, 4, kPad);
        grid->Add(new wxStaticText(this, wxID_ANY, "Slope:"), 0, wxALIGN_CENTER_VERTICAL);
        m_txt_slope = new wxTextCtrl(this, wxID_ANY, "1.0");
        grid->Add(m_txt_slope, 0);
        grid->Add(new wxStaticText(this, wxID_ANY, "Offset (m/s):"), 0, wxALIGN_CENTER_VERTICAL);
        m_txt_offset = new wxTextCtrl(this, wxID_ANY, "0.0");
        grid->Add(m_txt_offset, 0);
        speed_box->Add(grid, 0, wxALL, kPad);

        m_btn_apply_cal = new wxButton(this, ID_BTN_APPLY_CAL, "Apply");
        speed_box->Add(m_btn_apply_cal, 0, wxLEFT | wxRIGHT | wxBOTTOM, kPad);

        auto* note = new wxStaticText(this, wxID_ANY,
            "Fitted off-board against a reference speed log - see the\n"
            "autopilot skill's wind sensor section. There is no way to\n"
            "read back what is currently stored on the wind board.");
        speed_box->Add(note, 0, wxLEFT | wxRIGHT | wxBOTTOM, kPad);
    }
    root->Add(speed_box, 0, wxEXPAND | wxALL, kPad);

    // ── Close ────────────────────────────────────────────────────────────
    auto* close_row = new wxBoxSizer(wxHORIZONTAL);
    close_row->AddStretchSpacer(1);
    close_row->Add(new wxButton(this, ID_BTN_CLOSE, "Close"), 0);
    root->Add(close_row, 0, wxEXPAND | wxALL, kPad);

    SetSizerAndFit(root);
}

void AutoPilotSettingsDialog::UpdateFromState(const AutoPilotState& s, bool connected)
{
    m_connected = connected;
    m_nav_enabled = s.nav_enabled;

    if (connected && s.rudder_ok) {
        double off_center = s.rudder_angle - 180.0;
        wxString dir = (off_center > 0) ? "R" : (off_center < 0) ? "L" : "";
        m_rudder_val->SetLabel(wxString::Format("%.0f\xc2\xb0 %s",
            off_center >= 0 ? off_center : -off_center, dir));
    } else {
        m_rudder_val->SetLabel("--");
    }
    m_btn_zero_rudder->Enable(connected && !s.nav_enabled);

    if (connected && s.wind_ok) {
        m_wind_val->SetLabel(wxString::Format("%.0f\xc2\xb0  %.1f kn", s.wind_angle, s.wind_speed));
    } else {
        m_wind_val->SetLabel("--");
    }
    m_btn_zero_vane->Enable(connected);
    m_btn_vane_m10->Enable(connected);
    m_btn_vane_m1->Enable(connected);
    m_btn_vane_p1->Enable(connected);
    m_btn_vane_p10->Enable(connected);
    m_btn_apply_cal->Enable(connected);
}

// Re-checked here (not just at button-enable time) for the same reason as
// AutoPilotPanel::OnZeroRudder - state can change between the button being
// enabled and the dialog closing.
void AutoPilotSettingsDialog::OnZeroRudder(wxCommandEvent&)
{
    if (!m_link || !m_connected || m_nav_enabled) return;

    wxMessageDialog dlg(this,
        "Center the rudder firmly amidships before continuing.\n\n"
        "For best results, keep the boat stationary while doing this.",
        "Zero Rudder", wxOK | wxCANCEL | wxICON_QUESTION);
    dlg.SetOKCancelLabels("Zero", "Cancel");
    if (dlg.ShowModal() == wxID_OK)
        m_link->SendZeroRudder();
}

void AutoPilotSettingsDialog::OnZeroVane(wxCommandEvent&)
{
    if (!m_link || !m_connected) return;

    wxMessageDialog dlg(this,
        "Hold the wind vane pointing directly at the bow before continuing.",
        "Zero Vane", wxOK | wxCANCEL | wxICON_QUESTION);
    dlg.SetOKCancelLabels("Zero", "Cancel");
    if (dlg.ShowModal() == wxID_OK)
        m_link->SendVaneZero();
}

void AutoPilotSettingsDialog::OnVaneNudge(int degrees)
{
    if (!m_link || !m_connected) return;
    m_link->SendVaneNudge(static_cast<float>(degrees));
}

void AutoPilotSettingsDialog::OnApplySpeedCal(wxCommandEvent&)
{
    if (!m_link || !m_connected) return;

    double slope, offset;
    if (!m_txt_slope->GetValue().ToDouble(&slope) ||
        !m_txt_offset->GetValue().ToDouble(&offset)) {
        wxMessageDialog(this, "Slope and offset must both be numbers.",
                         "Wind Speed Calibration", wxOK | wxICON_ERROR).ShowModal();
        return;
    }

    wxMessageDialog dlg(this,
        wxString::Format("Set wind speed calibration to slope=%.4f, offset=%.4f m/s?",
                          slope, offset),
        "Wind Speed Calibration", wxOK | wxCANCEL | wxICON_QUESTION);
    dlg.SetOKCancelLabels("Apply", "Cancel");
    if (dlg.ShowModal() == wxID_OK)
        m_link->SendWindSpeedCal(static_cast<float>(slope), static_cast<float>(offset));
}

void AutoPilotSettingsDialog::OnCloseButton(wxCommandEvent&)
{
    Close();
}

void AutoPilotSettingsDialog::OnClose(wxCloseEvent&)
{
    if (m_close_cb) m_close_cb();
    Destroy();
}
