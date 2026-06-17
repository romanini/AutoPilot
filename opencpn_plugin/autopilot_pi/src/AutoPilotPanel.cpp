#include "wx/wxprec.h"
#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif
#include <wx/statline.h>

#include "AutoPilotPanel.h"

// Button IDs
enum {
    ID_BTN_COMPASS  = wxID_HIGHEST + 1,
    ID_BTN_WAYPOINT,
    ID_BTN_NAV_TOGGLE,
    ID_BTN_PORT_SHORT,
    ID_BTN_PORT_LONG,
    ID_BTN_STBD_SHORT,
    ID_BTN_STBD_LONG,
    ID_BTN_NAVIGATE,
};

// From display/button.ino
const float AutoPilotPanel::ADJUSTMENT_SHORT = 1.0f;
const float AutoPilotPanel::ADJUSTMENT_LONG  = 10.0f;

wxBEGIN_EVENT_TABLE(AutoPilotPanel, wxPanel)
    EVT_BUTTON(ID_BTN_COMPASS,    AutoPilotPanel::OnModeCompass)
    EVT_BUTTON(ID_BTN_WAYPOINT,   AutoPilotPanel::OnModeWaypoint)
    EVT_BUTTON(ID_BTN_NAV_TOGGLE, AutoPilotPanel::OnNavToggle)
    EVT_BUTTON(ID_BTN_PORT_SHORT, AutoPilotPanel::OnPortShort)
    EVT_BUTTON(ID_BTN_PORT_LONG,  AutoPilotPanel::OnPortLong)
    EVT_BUTTON(ID_BTN_STBD_SHORT, AutoPilotPanel::OnStbdShort)
    EVT_BUTTON(ID_BTN_STBD_LONG,  AutoPilotPanel::OnStbdLong)
    EVT_BUTTON(ID_BTN_NAVIGATE,   AutoPilotPanel::OnNavigate)
wxEND_EVENT_TABLE()

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

AutoPilotPanel::AutoPilotPanel(wxWindow* parent, AutoPilotLink* link)
    : wxPanel(parent, wxID_ANY)
    , m_link(link)
    , m_nav_enabled(false)
    , m_navigate_available(false)
    , m_navigate_lat(0.0)
    , m_navigate_lon(0.0)
{
    BuildUI();
}

AutoPilotPanel::~AutoPilotPanel() {}

// ---------------------------------------------------------------------------
// UI construction
// ---------------------------------------------------------------------------

static wxStaticText* MakeValue(wxWindow* parent) {
    auto* t = new wxStaticText(parent, wxID_ANY, "--");
    t->SetFont(t->GetFont().MakeBold());
    return t;
}

static void AddRow(wxFlexGridSizer* sizer, wxWindow* parent,
                   const wxString& label, wxStaticText*& value_out) {
    sizer->Add(new wxStaticText(parent, wxID_ANY, label), 0, wxALIGN_LEFT | wxALL, 2);
    value_out = MakeValue(parent);
    sizer->Add(value_out, 0, wxALIGN_LEFT | wxALL, 2);
}

void AutoPilotPanel::BuildUI() {
    auto* root = new wxBoxSizer(wxVERTICAL);

    // ---- Connection indicator ----
    m_connected_label = new wxStaticText(this, wxID_ANY, "● DISCONNECTED");
    m_connected_label->SetForegroundColour(*wxRED);
    root->Add(m_connected_label, 0, wxEXPAND | wxALL, 4);

    // ---- Status grid ----
    auto* grid = new wxFlexGridSizer(2, wxSize(6, 2));
    grid->AddGrowableCol(1, 1);

    AddRow(grid, this, "Heading:",        m_heading_val);
    AddRow(grid, this, "Desired:",        m_hdg_desired_val);
    AddRow(grid, this, "Bearing:",        m_bearing_val);
    AddRow(grid, this, "Correction:",     m_bearing_corr_val);
    AddRow(grid, this, "Mode:",           m_mode_val);
    AddRow(grid, this, "Nav enabled:",    m_nav_enabled_val);
    AddRow(grid, this, "GPS fix:",        m_gps_fix_val);
    AddRow(grid, this, "Satellites:",     m_satellites_val);
    AddRow(grid, this, "Position:",       m_location_val);
    AddRow(grid, this, "Waypoint:",       m_waypoint_val);
    AddRow(grid, this, "Distance (nm):",  m_distance_val);
    AddRow(grid, this, "Speed (kn):",     m_speed_val);
    AddRow(grid, this, "Course (°):",     m_course_val);
    AddRow(grid, this, "Active leg:",     m_active_leg_val);

    root->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT, 4);

    root->AddSpacer(6);
    root->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, 4);
    root->AddSpacer(4);

    // ---- Mode controls ----
    auto* mode_row = new wxBoxSizer(wxHORIZONTAL);
    m_btn_compass  = new wxButton(this, ID_BTN_COMPASS,  "Compass hold");
    m_btn_waypoint = new wxButton(this, ID_BTN_WAYPOINT, "Waypoint nav");
    mode_row->Add(m_btn_compass,  1, wxEXPAND | wxRIGHT, 3);
    mode_row->Add(m_btn_waypoint, 1, wxEXPAND);
    root->Add(mode_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);

    // ---- Nav enable toggle ----
    m_btn_nav_toggle = new wxButton(this, ID_BTN_NAV_TOGGLE, "Enable navigation");
    root->Add(m_btn_nav_toggle, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);

    // ---- Port/Stbd adjust ----
    auto* adj_row = new wxBoxSizer(wxHORIZONTAL);
    adj_row->Add(new wxButton(this, ID_BTN_PORT_LONG,  "◀◀ 10°"), 1, wxEXPAND | wxRIGHT, 2);
    adj_row->Add(new wxButton(this, ID_BTN_PORT_SHORT, "◀ 1°"),   1, wxEXPAND | wxRIGHT, 2);
    adj_row->Add(new wxButton(this, ID_BTN_STBD_SHORT, "1° ▶"),   1, wxEXPAND | wxRIGHT, 2);
    adj_row->Add(new wxButton(this, ID_BTN_STBD_LONG,  "10° ▶▶"), 1, wxEXPAND);
    root->Add(adj_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);

    // ---- Navigate to active waypoint ----
    m_btn_navigate = new wxButton(this, ID_BTN_NAVIGATE, "Navigate to active waypoint");
    m_btn_navigate->Enable(false);
    root->Add(m_btn_navigate, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);

    SetSizerAndFit(root);
}

// ---------------------------------------------------------------------------
// State update (called from AutoPilotLink's timer on every poll)
// ---------------------------------------------------------------------------

void AutoPilotPanel::UpdateFromState(const AutoPilotState& s, bool connected) {
    if (connected) {
        m_connected_label->SetLabel("● CONNECTED");
        m_connected_label->SetForegroundColour(wxColour(0, 160, 0));
    } else {
        m_connected_label->SetLabel("● DISCONNECTED");
        m_connected_label->SetForegroundColour(*wxRED);
    }

    m_heading_val->SetLabel(wxString::Format("%.1f°", s.heading));
    m_hdg_desired_val->SetLabel(wxString::Format("%.1f°", s.heading_desired));
    m_bearing_val->SetLabel(wxString::Format("%.1f°", s.bearing));
    m_bearing_corr_val->SetLabel(wxString::Format("%.1f°", s.bearing_correction));

    wxString mode_str;
    switch (s.mode) {
        case 0: mode_str = "Off";              break;
        case 1: mode_str = "Compass hold";     break;
        case 2: mode_str = "Waypoint navigate"; break;
        default: mode_str = wxString::Format("%d", s.mode); break;
    }
    m_mode_val->SetLabel(mode_str);

    m_nav_enabled_val->SetLabel(s.nav_enabled ? "Yes" : "No");
    m_nav_enabled = s.nav_enabled;
    m_btn_nav_toggle->SetLabel(s.nav_enabled ? "Disable navigation" : "Enable navigation");

    m_gps_fix_val->SetLabel(s.fix ? wxString::Format("Yes (Q%d)", s.fixquality) : "No");
    m_satellites_val->SetLabel(wxString::Format("%d", s.satellites));

    if (s.fix) {
        m_location_val->SetLabel(wxString::Format("%.5f, %.5f", s.location_lat, s.location_lon));
    } else {
        m_location_val->SetLabel("--");
    }

    if (s.waypoint_set) {
        m_waypoint_val->SetLabel(wxString::Format("%.5f, %.5f", s.wp_lat, s.wp_lon));
    } else {
        m_waypoint_val->SetLabel("--");
    }

    m_distance_val->SetLabel(wxString::Format("%.2f", s.distance));
    m_speed_val->SetLabel(wxString::Format("%.1f", s.speed));
    m_course_val->SetLabel(wxString::Format("%.1f°", s.course));

    Layout();
}

void AutoPilotPanel::SetActiveLeg(double btw, double dtw, const wxString& wp_name) {
    if (wp_name.IsEmpty()) {
        m_active_leg_val->SetLabel("--");
    } else {
        m_active_leg_val->SetLabel(
            wxString::Format("%s  Btw %.1f°  Dtw %.2fnm", wp_name, btw, dtw));
    }
}

void AutoPilotPanel::SetNavigateTarget(bool available, double lat, double lon) {
    m_navigate_available = available;
    m_navigate_lat = lat;
    m_navigate_lon = lon;
    m_btn_navigate->Enable(available);
}

// ---------------------------------------------------------------------------
// Button handlers
// ---------------------------------------------------------------------------

void AutoPilotPanel::OnModeCompass(wxCommandEvent&) {
    if (m_link) m_link->SendMode(1);
}

void AutoPilotPanel::OnModeWaypoint(wxCommandEvent&) {
    if (m_link) m_link->SendMode(2);
}

void AutoPilotPanel::OnNavToggle(wxCommandEvent&) {
    if (m_link) m_link->SendNavEnable(!m_nav_enabled);
}

void AutoPilotPanel::OnPortShort(wxCommandEvent&) {
    if (m_link) m_link->SendAdjust(-ADJUSTMENT_SHORT);
}

void AutoPilotPanel::OnPortLong(wxCommandEvent&) {
    if (m_link) m_link->SendAdjust(-ADJUSTMENT_LONG);
}

void AutoPilotPanel::OnStbdShort(wxCommandEvent&) {
    if (m_link) m_link->SendAdjust(ADJUSTMENT_SHORT);
}

void AutoPilotPanel::OnStbdLong(wxCommandEvent&) {
    if (m_link) m_link->SendAdjust(ADJUSTMENT_LONG);
}

void AutoPilotPanel::OnNavigate(wxCommandEvent&) {
    if (!m_link || !m_navigate_available) return;
    m_link->SendWaypoint(m_navigate_lat, m_navigate_lon);
    m_link->SendMode(2);  // switch to waypoint-navigate
}
