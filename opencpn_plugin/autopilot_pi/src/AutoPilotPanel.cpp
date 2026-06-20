#include "wx/wxprec.h"
#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif
#include <wx/statline.h>

#include "AutoPilotPanel.h"

// Button IDs
enum {
    ID_BTN_MODE       = wxID_HIGHEST + 1,
    ID_BTN_NAV_TOGGLE,
    ID_BTN_PORT_SHORT,
    ID_BTN_PORT_LONG,
    ID_BTN_STBD_SHORT,
    ID_BTN_STBD_LONG,
    ID_BTN_SEND_WP,
};

const float AutoPilotPanel::ADJUSTMENT_SHORT = 1.0f;
const float AutoPilotPanel::ADJUSTMENT_LONG  = 10.0f;

wxBEGIN_EVENT_TABLE(AutoPilotPanel, wxScrolledWindow)
    EVT_BUTTON(ID_BTN_MODE,       AutoPilotPanel::OnMode)
    EVT_BUTTON(ID_BTN_NAV_TOGGLE, AutoPilotPanel::OnNavToggle)
    EVT_BUTTON(ID_BTN_PORT_SHORT, AutoPilotPanel::OnPortShort)
    EVT_BUTTON(ID_BTN_PORT_LONG,  AutoPilotPanel::OnPortLong)
    EVT_BUTTON(ID_BTN_STBD_SHORT, AutoPilotPanel::OnStbdShort)
    EVT_BUTTON(ID_BTN_STBD_LONG,  AutoPilotPanel::OnStbdLong)
    EVT_BUTTON(ID_BTN_SEND_WP,    AutoPilotPanel::OnSendWP)
wxEND_EVENT_TABLE()

// ---------------------------------------------------------------------------
// TFT-matching color palette (RGB565 decoded to 24-bit)
// ---------------------------------------------------------------------------
static const wxColour kBlack   (  0,   0,   0);
static const wxColour kCyan    (  0, 255, 255);  // Speed, Distance
static const wxColour kYellow  (255, 255,   0);  // Heading/Pitch/Roll/Stab
static const wxColour kLavender(247, 174, 255);  // 0xF57F — Destination
static const wxColour kOrange  (255, 130,  74);  // 0xFC09 — Bearing
static const wxColour kGreen   (123, 255,  66);  // 0x7FE8 — Course, Location
static const wxColour kWhite   (255, 255, 255);  // Date/Time, Fix

// ---------------------------------------------------------------------------
// Fixed pixel dimensions.
//
// Derived from the actual TFT box sizes in screen.ino, then scaled so the
// total column height is 200px (roughly half the TFT's 320px column).  At
// typical Linux GTK font/DPI scaling the 200px renders visually similar to
// the TFT's 320px column.  All three columns are the same total height so
// their tops and bottoms stay aligned.
//
// TFT left col: Speed(80) Heading(60) Pitch(60) Roll(60) Stability(59) = 319
// TFT mid  col: Destination(105) Bearing(110) [Volts omitted] → ext Bearing
// TFT right col: Distance(80) Course(94) Location(94) = 268 → ext Location
//
// Scale factor ≈ 200/319 = 0.627; applied proportionally, rounded.
// ---------------------------------------------------------------------------
static const int kColW = 160;

static const int kH_Spd = 50;   // left col — total 198 (date bar shifts to right_block)
static const int kH_Hdg = 37;
static const int kH_Ptc = 37;
static const int kH_Rol = 37;
static const int kH_Stb = 37;   // 50+37+37+37+37 = 198

static const int kH_Dst = 80;   // mid col — total 160
static const int kH_Brg = 80;   // 80+80 = 160

static const int kH_Dis = 50;   // right col — total 160
static const int kH_Crs = 45;
static const int kH_Loc = 65;   // 50+45+65 = 160

static const int kH_Bar = 38;   // bottom date/time bar — sized for 11pt text

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Creates a box matching the TFT layout:
//   outer_out  — colored; visible as a 2px frame border
//   black content fills the interior
//   a small colored pill top-left, only as wide as the title text (no wxEXPAND)
//   inner_out  — the black value area below the pill; caller populates this
static void MakeBox(wxWindow* parent,
                    const wxString& title, const wxColour& col,
                    wxPanel*& outer_out, wxPanel*& inner_out,
                    int w, int h)
{
    // Colored outer acts as the border frame
    outer_out = new wxPanel(parent, wxID_ANY);
    outer_out->SetBackgroundColour(col);
    outer_out->SetMinSize(wxSize(w, h));

    auto* outer_v = new wxBoxSizer(wxVERTICAL);
    outer_out->SetSizer(outer_v);

    // Black interior (2px inset → colored border visible around it)
    auto* content = new wxPanel(outer_out, wxID_ANY);
    content->SetBackgroundColour(kBlack);
    outer_v->Add(content, 1, wxEXPAND | wxALL, 2);

    auto* content_v = new wxBoxSizer(wxVERTICAL);
    content->SetSizer(content_v);

    // Title pill: colored panel, NOT expanded → only as wide as the text.
    // This matches the TFT fillRect that covers only the title string bounds.
    auto* pill = new wxPanel(content, wxID_ANY);
    pill->SetBackgroundColour(col);
    {   auto* ph = new wxBoxSizer(wxHORIZONTAL);
        pill->SetSizer(ph);
        auto* lbl = new wxStaticText(pill, wxID_ANY, title);
        lbl->SetForegroundColour(kBlack);
        {   wxFont f = lbl->GetFont().Scale(0.65);
            f.SetPointSize(f.GetPointSize() + 1);
            f.SetWeight(wxFONTWEIGHT_BOLD);
            lbl->SetFont(f); }
        ph->Add(lbl, 0, wxLEFT | wxRIGHT, 2); }
    content_v->Add(pill, 0, 0);   // no wxEXPAND — stays at text width

    // Value area returned to caller
    inner_out = new wxPanel(content, wxID_ANY);
    inner_out->SetBackgroundColour(kBlack);
    content_v->Add(inner_out, 1, wxEXPAND);
}

// Bold centred value label on black background.
static wxStaticText* MakeVal(wxPanel* inner, const wxColour& col, int pt,
                              long style = wxALIGN_CENTER)
{
    auto* t = new wxStaticText(inner, wxID_ANY, "--",
                               wxDefaultPosition, wxDefaultSize, style);
    t->SetForegroundColour(col);
    t->SetBackgroundColour(kBlack);
    wxFont f = t->GetFont();
    f.SetPointSize(pt);
    f.SetWeight(wxFONTWEIGHT_BOLD);
    t->SetFont(f);
    return t;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

AutoPilotPanel::AutoPilotPanel(wxWindow* parent, AutoPilotLink* link)
    : wxScrolledWindow(parent, wxID_ANY)
    , m_link(link)
    , m_navigate_available(false)
    , m_navigate_lat(0.0)
    , m_navigate_lon(0.0)
{
    BuildUI();
}

AutoPilotPanel::~AutoPilotPanel() {}

// ---------------------------------------------------------------------------
// UI layout — fixed-pixel 3-column grid + controls.
//
// All data boxes use proportion=0 and explicit SetMinSize so the virtual
// size is exactly kPanelW × kPanelH.  wxScrolledWindow shows scrollbars if
// the AUI pane is dragged smaller; MaxSize in the AUI pane prevents growing.
// ---------------------------------------------------------------------------

void AutoPilotPanel::BuildUI()
{
    SetBackgroundColour(kBlack);

    auto* root = new wxBoxSizer(wxVERTICAL);

    wxPanel *outer, *inner;

    // ── 3-column data grid (480 × 310) ────────────────────────────────────
    auto* cols = new wxBoxSizer(wxHORIZONTAL);

    // ── Left column ────────────────────────────────────────────────────────
    auto* left = new wxBoxSizer(wxVERTICAL);

    MakeBox(this, "Speed kn", kCyan, outer, inner, kColW, kH_Spd);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_speed_val = MakeVal(inner, kCyan, 17);  // +20%
        s->Add(m_speed_val, 1, wxEXPAND | wxALL, 2); }
    left->Add(outer, 0, wxEXPAND);

    MakeBox(this, "Heading", kYellow, outer, inner, kColW, kH_Hdg);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_heading_val = MakeVal(inner, kYellow, 14);
        s->AddStretchSpacer(1);
        s->Add(m_heading_val, 0, wxEXPAND | wxLEFT | wxRIGHT, 2);
        s->AddStretchSpacer(1); }
    left->Add(outer, 0, wxEXPAND);

    MakeBox(this, "Pitch", kYellow, outer, inner, kColW, kH_Ptc);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_pitch_val = MakeVal(inner, kYellow, 14);
        s->AddStretchSpacer(1);
        s->Add(m_pitch_val, 0, wxEXPAND | wxLEFT | wxRIGHT, 2);
        s->AddStretchSpacer(1); }
    left->Add(outer, 0, wxEXPAND);

    MakeBox(this, "Roll", kYellow, outer, inner, kColW, kH_Rol);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_roll_val = MakeVal(inner, kYellow, 14);
        s->AddStretchSpacer(1);
        s->Add(m_roll_val, 0, wxEXPAND | wxLEFT | wxRIGHT, 2);
        s->AddStretchSpacer(1); }
    left->Add(outer, 0, wxEXPAND);

    MakeBox(this, "Stability", kYellow, outer, inner, kColW, kH_Stb);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_stability_val = MakeVal(inner, kYellow, 10);
        s->AddStretchSpacer(1);
        s->Add(m_stability_val, 0, wxEXPAND | wxLEFT | wxRIGHT, 1);
        s->AddStretchSpacer(1); }
    left->Add(outer, 0, wxEXPAND);

    cols->Add(left, 0, wxEXPAND);

    // ── Right block: mid + right columns stacked above date bar ───────────
    auto* right_block = new wxBoxSizer(wxVERTICAL);
    auto* mid_right   = new wxBoxSizer(wxHORIZONTAL);

    // ── Middle column ──────────────────────────────────────────────────────
    auto* mid = new wxBoxSizer(wxVERTICAL);

    // Destination — "No link" when disconnected; mirrors TFT display_mode()+display_destination()
    MakeBox(this, "Destination", kLavender, outer, inner, kColW, kH_Dst);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_destination_val = MakeVal(inner, kLavender, 12);  // +30%
        s->Add(m_destination_val, 1, wxEXPAND | wxALL, 2); }
    mid->Add(outer, 0, wxEXPAND);

    MakeBox(this, "Bearing", kOrange, outer, inner, kColW, kH_Brg);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_bearing_val = MakeVal(inner, kOrange, 17);      // same as speed/distance
        s->Add(m_bearing_val, 1, wxEXPAND | wxALL, 2);
        m_bearing_corr_val = MakeVal(inner, kOrange, 17); // same, no "Correction:" label
        s->Add(m_bearing_corr_val, 1, wxEXPAND | wxALL, 2); }
    mid->Add(outer, 0, wxEXPAND);

    mid_right->Add(mid, 0, wxEXPAND);

    // ── Right column ───────────────────────────────────────────────────────
    auto* right = new wxBoxSizer(wxVERTICAL);

    MakeBox(this, "Distance nm", kCyan, outer, inner, kColW, kH_Dis);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_distance_val = MakeVal(inner, kCyan, 17);  // +20%, matches speed
        s->Add(m_distance_val, 1, wxEXPAND | wxALL, 2); }
    right->Add(outer, 0, wxEXPAND);

    MakeBox(this, "Course", kGreen, outer, inner, kColW, kH_Crs);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_course_val = MakeVal(inner, kGreen, 14);   // slightly larger than destination
        s->Add(m_course_val, 1, wxEXPAND | wxALL, 2); }
    right->Add(outer, 0, wxEXPAND);

    MakeBox(this, "Location", kGreen, outer, inner, kColW, kH_Loc);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_location_val = MakeVal(inner, kGreen, 12, wxALIGN_CENTER);  // destination size
        s->Add(m_location_val, 1, wxEXPAND | wxALL, 2); }
    right->Add(outer, 0, wxEXPAND);

    mid_right->Add(right, 0, wxEXPAND);
    right_block->Add(mid_right, 0, 0);

    // ── Bottom of right_block: narrower Date bar + Send WP button ─────────
    // Date bar: mid col (160px) + 1/3 of right col (53px) = 213px
    // Send WP:  remaining 2/3 of right col = 107px
    const int kDateW  = kColW + kColW / 3;       // 213
    const int kWpBtnW = kColW * 2 - kDateW;      // 107

    auto* bottom_row = new wxBoxSizer(wxHORIZONTAL);

    MakeBox(this, "Date / Time", kWhite, outer, inner, kDateW, kH_Bar);
    {   auto* s = new wxBoxSizer(wxHORIZONTAL);
        inner->SetSizer(s);
        m_datetime_val = MakeVal(inner, kWhite, 11);
        m_gpsfix_val   = MakeVal(inner, kWhite, 10);
        s->Add(m_datetime_val, 1, wxEXPAND | wxALL, 1);
        s->Add(m_gpsfix_val,   0, wxALL, 1); }
    bottom_row->Add(outer, 0, wxEXPAND);

    m_btn_send_wp = new wxButton(this, ID_BTN_SEND_WP, "Set WP");
    const int kBtnPad = 4;
    m_btn_send_wp->SetMinSize(wxSize(kWpBtnW - 2 * kBtnPad, kH_Bar - 2 * kBtnPad));
    m_btn_send_wp->Enable(false);
    bottom_row->Add(m_btn_send_wp, 0, wxTOP | wxLEFT | wxRIGHT, kBtnPad);

    right_block->Add(bottom_row, 0, 0);

    cols->Add(right_block, 0, wxEXPAND);
    root->Add(cols, 0, 0);

    // ── Active leg line ─────────────────────────────────────────────────────
    // ── Controls ───────────────────────────────────────────────────────────
    root->Add(new wxStaticLine(this, wxID_ANY, wxDefaultPosition,
                               wxSize(kPanelW, 1)),
              0, wxTOP, 6);
    root->AddSpacer(4);

    // All 6 buttons share the same minimum width so they look identical in size.
    // Stretch spacers between them distribute them evenly without full-width stretching.
    // Single button row: Mode  << 10  < 1  1 >  10 >>  Enable/Disable
    // 6 buttons at 62px min each = 372px in a 480px panel; stretch spacers fill the rest evenly.
    const wxSize kBtnSz(62, -1);
    m_btn_mode       = new wxButton(this, ID_BTN_MODE,       "Mode");
    m_btn_port_long  = new wxButton(this, ID_BTN_PORT_LONG,  "<< 10");
    m_btn_port_short = new wxButton(this, ID_BTN_PORT_SHORT, "< 1");
    m_btn_stbd_short = new wxButton(this, ID_BTN_STBD_SHORT, "1 >");
    m_btn_stbd_long  = new wxButton(this, ID_BTN_STBD_LONG,  "10 >>");
    m_btn_nav_toggle = new wxButton(this, ID_BTN_NAV_TOGGLE, "Enable");
    m_btn_mode->SetMinSize(kBtnSz);
    m_btn_port_long->SetMinSize(kBtnSz);
    m_btn_port_short->SetMinSize(kBtnSz);
    m_btn_stbd_short->SetMinSize(kBtnSz);
    m_btn_stbd_long->SetMinSize(kBtnSz);
    m_btn_nav_toggle->SetMinSize(kBtnSz);

    auto* btn_row = new wxBoxSizer(wxHORIZONTAL);
    btn_row->AddStretchSpacer(1);
    btn_row->Add(m_btn_mode,       0, wxRIGHT, 4);
    btn_row->AddStretchSpacer(1);
    btn_row->Add(m_btn_port_long,  0, wxRIGHT, 4);
    btn_row->AddStretchSpacer(1);
    btn_row->Add(m_btn_port_short, 0, wxRIGHT, 4);
    btn_row->AddStretchSpacer(1);
    btn_row->Add(m_btn_stbd_short, 0, wxRIGHT, 4);
    btn_row->AddStretchSpacer(1);
    btn_row->Add(m_btn_stbd_long,  0, wxRIGHT, 4);
    btn_row->AddStretchSpacer(1);
    btn_row->Add(m_btn_nav_toggle, 0);
    btn_row->AddStretchSpacer(1);
    root->Add(btn_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 3);

    // Initial state: all disabled until first telemetry confirms connection
    m_btn_port_long->Enable(false);
    m_btn_port_short->Enable(false);
    m_btn_stbd_short->Enable(false);
    m_btn_stbd_long->Enable(false);
    m_btn_mode->Enable(false);
    m_btn_nav_toggle->Enable(false);

    // Virtual size = sizer's natural fit; scrollbars appear if pane is shrunk below that.
    SetSizer(root);
    SetScrollRate(5, 5);
    FitInside();
}

// ---------------------------------------------------------------------------
// State update
// ---------------------------------------------------------------------------

void AutoPilotPanel::UpdateFromState(const AutoPilotState& s, bool connected)
{
    // Left column
    m_speed_val->SetLabel(s.fix ? wxString::Format("%.2f", s.speed) : "--");

    if (connected) {
        m_heading_val->SetLabel(wxString::Format("%.1f\xc2\xb0", s.heading));
        m_pitch_val->SetLabel  (wxString::Format("%.1f\xc2\xb0", s.pitch));
        m_roll_val->SetLabel   (wxString::Format("%.1f\xc2\xb0", s.roll));
        wxString stab;
        switch (s.stability) {
            case 0: stab = "Unknown";    break;
            case 1: stab = "On Table";   break;
            case 2: stab = "Stationary"; break;
            case 3: stab = "Stable";     break;
            case 4: stab = "In Motion";  break;
            default: stab = wxString::Format("%d", s.stability);
        }
        m_stability_val->SetLabel(stab);
    } else {
        m_heading_val->SetLabel("--");
        m_pitch_val->SetLabel("--");
        m_roll_val->SetLabel("--");
        m_stability_val->SetLabel("--");
    }

    // Middle column — Destination mirrors TFT display_mode()+display_destination()
    wxString dest;
    if (!connected) {
        dest = "No link";
    } else if (!s.nav_enabled) {
        dest = "Disabled";
    } else if (s.mode == 0) {
        dest = "Off";
    } else if (s.mode == 1) {
        dest = wxString::Format("Compass\n%.1f\xc2\xb0", s.heading_desired);
    } else if (s.mode == 2 && s.waypoint_set) {
        dest = wxString::Format("Waypoint\n%.6f\n%.6f", s.wp_lat, s.wp_lon);
    } else {
        dest = wxString::Format("Mode %d", s.mode);
    }
    // Mode 1 shows just a heading number — use the same large font as speed/distance.
    // Mode 2 shows coordinates — keep the smaller font so they fit.
    {
        int dest_pt = (connected && s.nav_enabled && s.mode == 1) ? 17 : 12;
        wxFont f = m_destination_val->GetFont();
        if (f.GetPointSize() != dest_pt) {
            f.SetPointSize(dest_pt);
            m_destination_val->SetFont(f);
        }
    }
    m_destination_val->SetLabel(dest);

    if (connected && s.nav_enabled) {
        m_bearing_val->SetLabel(wxString::Format("%.1f\xc2\xb0", s.bearing));
        double bc = s.bearing_correction;
        m_bearing_corr_val->SetLabel(
            wxString::Format("%.1f\xc2\xb0 %s", bc >= 0 ? bc : -bc, bc >= 0 ? "R" : "L"));
    } else {
        m_bearing_val->SetLabel("--");
        m_bearing_corr_val->SetLabel("--");
    }

    // Right column
    m_distance_val->SetLabel(
        (s.waypoint_set && s.fix) ? wxString::Format("%.2f", s.distance) : "--");
    m_course_val->SetLabel(
        s.fix ? wxString::Format("%.1f\xc2\xb0", s.course) : "--");
    m_location_val->SetLabel(
        s.fix ? wxString::Format("%.6f\n%.6f", s.location_lat, s.location_lon) : "--");

    // Bottom bar
    if (s.fix) {
        m_datetime_val->SetLabel(wxString::Format("%d/%d/%02d  %d:%02d",
            s.month, s.day, s.year % 100, s.hour, s.minute));
        wxString fix_type;
        switch (s.fixquality) {
            case 1: fix_type = "GPS";  break;
            case 2: fix_type = "DGPS"; break;
            default: fix_type = wxString::Format("Q%d", s.fixquality);
        }
        m_gpsfix_val->SetLabel(wxString::Format("%s(%d)", fix_type, s.satellites));
    } else {
        m_datetime_val->SetLabel("--");
        m_gpsfix_val->SetLabel("No fix");
    }

    // Refresh button enable/label based on connection and nav state
    bool nav_on = connected && s.nav_enabled;
    m_btn_nav_toggle->Enable(connected);
    m_btn_nav_toggle->SetLabel(nav_on ? "Disable" : "Enable");
    m_btn_mode->Enable(nav_on);
    m_btn_port_long->Enable(nav_on);
    m_btn_port_short->Enable(nav_on);
    m_btn_stbd_short->Enable(nav_on);
    m_btn_stbd_long->Enable(nav_on);
    m_btn_send_wp->Enable(connected && m_navigate_available);

    Layout();
}


void AutoPilotPanel::SetNavigateTarget(bool available, double lat, double lon)
{
    m_navigate_available = available;
    m_navigate_lat = lat;
    m_navigate_lon = lon;
    m_btn_send_wp->Enable(m_link->IsConnected() && available);
}

// ---------------------------------------------------------------------------
// Button handlers
// ---------------------------------------------------------------------------

void AutoPilotPanel::OnMode(wxCommandEvent&)
{
    if (!m_link || !m_link->IsConnected()) return;
    const AutoPilotState& s = m_link->State();
    if (!s.nav_enabled) return;
    // 1→2 only if a waypoint is set; 2→1 always; 1→1 when no waypoint
    int new_mode = (s.mode == 2) ? 1 : (s.waypoint_set ? 2 : 1);
    m_link->SendMode(new_mode);
}

void AutoPilotPanel::OnNavToggle(wxCommandEvent&)
{
    if (!m_link || !m_link->IsConnected()) return;
    m_link->SendNavEnable(!m_link->State().nav_enabled);
}

void AutoPilotPanel::OnPortShort(wxCommandEvent&)
{
    if (!m_link || !m_link->IsConnected() || !m_link->State().nav_enabled) return;
    m_link->SendAdjust(-ADJUSTMENT_SHORT);
}

void AutoPilotPanel::OnPortLong(wxCommandEvent&)
{
    if (!m_link || !m_link->IsConnected() || !m_link->State().nav_enabled) return;
    m_link->SendAdjust(-ADJUSTMENT_LONG);
}

void AutoPilotPanel::OnStbdShort(wxCommandEvent&)
{
    if (!m_link || !m_link->IsConnected() || !m_link->State().nav_enabled) return;
    m_link->SendAdjust(ADJUSTMENT_SHORT);
}

void AutoPilotPanel::OnStbdLong(wxCommandEvent&)
{
    if (!m_link || !m_link->IsConnected() || !m_link->State().nav_enabled) return;
    m_link->SendAdjust(ADJUSTMENT_LONG);
}

void AutoPilotPanel::OnSendWP(wxCommandEvent&)
{
    if (!m_link || !m_link->IsConnected() || !m_navigate_available) return;
    m_link->SendWaypoint(m_navigate_lat, m_navigate_lon);
}
