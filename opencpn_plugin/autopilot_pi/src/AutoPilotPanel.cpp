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
    ID_BTN_UNDOCK,
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
    EVT_BUTTON(ID_BTN_UNDOCK,     AutoPilotPanel::OnUndock)
wxEND_EVENT_TABLE()

// ---------------------------------------------------------------------------
// TFT-matching color palette (RGB565 decoded to 24-bit)
// ---------------------------------------------------------------------------
static const wxColour kBlack   (  0,   0,   0);
static const wxColour kCyan    (  0, 255, 255);
static const wxColour kYellow  (255, 255,   0);
static const wxColour kLavender(247, 174, 255);
static const wxColour kOrange  (255, 130,  74);
static const wxColour kGreen   (123, 255,  66);
static const wxColour kWhite   (255, 255, 255);

// ---------------------------------------------------------------------------
// Pixel dimensions for the floating (3-column) layout
// ---------------------------------------------------------------------------
static const int kColW = 160;

static const int kH_Spd = 50;
static const int kH_Hdg = 37;
static const int kH_Ptc = 37;
static const int kH_Rol = 37;
static const int kH_Stb = 37;

static const int kH_Dst = 80;
static const int kH_Brg = 80;

static const int kH_Dis = 50;
static const int kH_Crs = 45;
static const int kH_Loc = 65;

static const int kH_Bar = 38;

// Right-dock layout: narrow column, taller touch buttons, tighter boxes
static const int kColW_Right = 100;  // natural sizer floor; AUI enforces this via kRightDockW
static const int kBtnH_Right = 38;   // taller than default for touch targets

// Height for compact boxes in the top/bottom docked layout
static const int kH_Compact = 71;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void MakeBox(wxWindow* parent,
                    const wxString& title, const wxColour& col,
                    wxPanel*& outer_out, wxPanel*& inner_out,
                    int w, int h)
{
    outer_out = new wxPanel(parent, wxID_ANY);
    outer_out->SetBackgroundColour(col);
    outer_out->SetMinSize(wxSize(w, h));

    auto* outer_v = new wxBoxSizer(wxVERTICAL);
    outer_out->SetSizer(outer_v);

    auto* content = new wxPanel(outer_out, wxID_ANY);
    content->SetBackgroundColour(kBlack);
    outer_v->Add(content, 1, wxEXPAND | wxALL, 2);

    auto* content_v = new wxBoxSizer(wxVERTICAL);
    content->SetSizer(content_v);

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
    content_v->Add(pill, 0, 0);

    inner_out = new wxPanel(content, wxID_ANY);
    inner_out->SetBackgroundColour(kBlack);
    content_v->Add(inner_out, 1, wxEXPAND);
}

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

// Initial-disable helper shared by all layouts
static void DisableNavButtons(wxButton* port_long, wxButton* port_short,
                               wxButton* stbd_short, wxButton* stbd_long,
                               wxButton* mode, wxButton* nav_toggle)
{
    port_long->Enable(false);
    port_short->Enable(false);
    stbd_short->Enable(false);
    stbd_long->Enable(false);
    mode->Enable(false);
    nav_toggle->Enable(false);
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

AutoPilotPanel::AutoPilotPanel(wxWindow* parent, AutoPilotLink* link)
    : wxScrolledWindow(parent, wxID_ANY)
    , m_link(link)
    , m_dock_mode(DockMode::FLOAT)
    , m_btn_undock(nullptr)
    , m_navigate_available(false)
    , m_navigate_lat(0.0)
    , m_navigate_lon(0.0)
{
    BuildUI_Float();
}

AutoPilotPanel::~AutoPilotPanel() {}

// ---------------------------------------------------------------------------
// Dock-mode switching — tears down the current layout and rebuilds.
// ---------------------------------------------------------------------------

bool AutoPilotPanel::SetDockMode(DockMode mode) {
    if (mode == m_dock_mode) return false;
    m_dock_mode = mode;

    Freeze();
    DestroyChildren();  // schedules all child windows for deletion
    m_btn_undock = nullptr;

    switch (mode) {
        case DockMode::FLOAT:      BuildUI_Float();    break;
        case DockMode::RIGHT:      BuildUI_Right();    break;
        case DockMode::TOP_BOTTOM: BuildUI_TopBottom();break;
    }
    Layout();
    Thaw();
    return true;
}

// ---------------------------------------------------------------------------
// Floating layout — fixed 3-column 480px grid (unchanged from original)
// ---------------------------------------------------------------------------

void AutoPilotPanel::BuildUI_Float()
{
    SetBackgroundColour(kBlack);

    auto* root = new wxBoxSizer(wxVERTICAL);

    wxPanel *outer, *inner;

    // ── 3-column data grid ─────────────────────────────────────────────────
    auto* cols = new wxBoxSizer(wxHORIZONTAL);

    // ── Left column ────────────────────────────────────────────────────────
    auto* left = new wxBoxSizer(wxVERTICAL);

    MakeBox(this, "Speed kn", kCyan, outer, inner, kColW, kH_Spd);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_speed_val = MakeVal(inner, kCyan, 17);
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

    MakeBox(this, "Destination", kLavender, outer, inner, kColW, kH_Dst);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_destination_val = MakeVal(inner, kLavender, 12);
        s->Add(m_destination_val, 1, wxEXPAND | wxALL, 2); }
    mid->Add(outer, 0, wxEXPAND);

    MakeBox(this, "Bearing", kOrange, outer, inner, kColW, kH_Brg);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_bearing_val = MakeVal(inner, kOrange, 17);
        s->Add(m_bearing_val, 1, wxEXPAND | wxALL, 2);
        m_bearing_corr_val = MakeVal(inner, kOrange, 17);
        s->Add(m_bearing_corr_val, 1, wxEXPAND | wxALL, 2); }
    mid->Add(outer, 0, wxEXPAND);

    mid_right->Add(mid, 0, wxEXPAND);

    // ── Right column ───────────────────────────────────────────────────────
    auto* right = new wxBoxSizer(wxVERTICAL);

    MakeBox(this, "Distance nm", kCyan, outer, inner, kColW, kH_Dis);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_distance_val = MakeVal(inner, kCyan, 17);
        s->Add(m_distance_val, 1, wxEXPAND | wxALL, 2); }
    right->Add(outer, 0, wxEXPAND);

    MakeBox(this, "Course", kGreen, outer, inner, kColW, kH_Crs);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_course_val = MakeVal(inner, kGreen, 14);
        s->Add(m_course_val, 1, wxEXPAND | wxALL, 2); }
    right->Add(outer, 0, wxEXPAND);

    MakeBox(this, "Location", kGreen, outer, inner, kColW, kH_Loc);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_location_val = MakeVal(inner, kGreen, 12, wxALIGN_CENTER);
        s->Add(m_location_val, 1, wxEXPAND | wxALL, 2); }
    right->Add(outer, 0, wxEXPAND);

    mid_right->Add(right, 0, wxEXPAND);
    right_block->Add(mid_right, 0, 0);

    // ── Bottom of right_block: Time bar + Set WP + Follow ──────────────────
    const int kTimeW  = kColW;
    const int kWpBtnW = 75;
    const int kBtnPad = 4;

    auto* bottom_row = new wxBoxSizer(wxHORIZONTAL);

    MakeBox(this, "Time", kWhite, outer, inner, kTimeW, kH_Bar);
    {   auto* s = new wxBoxSizer(wxHORIZONTAL);
        inner->SetSizer(s);
        m_datetime_val = MakeVal(inner, kWhite, 11);
        m_gpsfix_val   = MakeVal(inner, kWhite, 10);
        s->AddStretchSpacer(1);
        s->Add(m_datetime_val, 0, wxALL | wxALIGN_CENTER_VERTICAL, 1);
        s->Add(m_gpsfix_val,   0, wxALL | wxALIGN_CENTER_VERTICAL, 1);
        s->AddStretchSpacer(1); }
    bottom_row->Add(outer, 0, wxEXPAND);

    bottom_row->AddSpacer(5);

    m_btn_send_wp = new wxButton(this, ID_BTN_SEND_WP, "Set WP");
    m_btn_send_wp->SetMinSize(wxSize(kWpBtnW - 2 * kBtnPad, kH_Bar - 2 * kBtnPad));
    m_btn_send_wp->Enable(false);
    bottom_row->Add(m_btn_send_wp, 0, wxTOP | wxLEFT | wxRIGHT, kBtnPad);

    m_chk_follow = new wxCheckBox(this, wxID_ANY, "Follow");
    m_chk_follow->SetForegroundColour(kWhite);
    m_chk_follow->SetBackgroundColour(kBlack);
    bottom_row->Add(m_chk_follow, 1, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, kBtnPad);

    right_block->Add(bottom_row, 0, 0);
    cols->Add(right_block, 0, wxEXPAND);
    root->Add(cols, 0, 0);

    // ── Controls ───────────────────────────────────────────────────────────
    root->Add(new wxStaticLine(this, wxID_ANY, wxDefaultPosition,
                               wxSize(kPanelW, 1)),
              0, wxTOP, 6);
    root->AddSpacer(4);

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

    m_btn_undock = nullptr;  // no undock button in float mode

    DisableNavButtons(m_btn_port_long, m_btn_port_short,
                      m_btn_stbd_short, m_btn_stbd_long,
                      m_btn_mode, m_btn_nav_toggle);

    SetSizer(root);
    SetScrollRate(5, 5);
    FitInside();
}

// ---------------------------------------------------------------------------
// Right-dock layout — single column: col1 → col2 → col3 → controls
// ---------------------------------------------------------------------------

void AutoPilotPanel::BuildUI_Right()
{
    SetBackgroundColour(kBlack);

    auto* root = new wxBoxSizer(wxVERTICAL);
    wxPanel *outer, *inner;
    const int kPad = 2;
    const int kBP  = 1;

    // Right-dock-specific box heights that differ from the float layout
    const int kH_Spd_R = 38;   // speed: shorter single value
    const int kH_Dis_R = 38;   // distance: shorter single value
    const int kH_Crs_R = 34;   // course: shorter single value
    const int kH_Dst_R = 70;   // destination: shorter
    const int kH_Brg_R = 60;   // bearing: shorter + centered
    const int kH_Loc_R = 50;   // location: shorter
    const int kH_Bar_R = 52;   // time: taller for two stacked lines

    // Width=1 overrides GTK's label-based button minimum so they shrink freely.
    auto BtnRow = [&](wxButton* a, wxButton* b) {
        auto* r = new wxBoxSizer(wxHORIZONTAL);
        a->SetMinSize(wxSize(1, kBtnH_Right));
        b->SetMinSize(wxSize(1, kBtnH_Right));
        r->Add(a, 1, wxEXPAND | wxRIGHT, 2);
        r->Add(b, 1, wxEXPAND);
        root->Add(r, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, kPad);
    };

    // ── Col 1: Speed, Heading, Pitch, Roll, Stability ──────────────────────
    MakeBox(this, "Speed kn", kCyan, outer, inner, 0, kH_Spd_R);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_speed_val = MakeVal(inner, kCyan, 13);
        s->Add(m_speed_val, 1, wxEXPAND | wxALL, kBP); }
    root->Add(outer, 0, wxEXPAND);

    MakeBox(this, "Heading", kYellow, outer, inner, 0, kH_Hdg);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_heading_val = MakeVal(inner, kYellow, 11);
        s->AddStretchSpacer(1);
        s->Add(m_heading_val, 0, wxEXPAND | wxLEFT | wxRIGHT, kBP);
        s->AddStretchSpacer(1); }
    root->Add(outer, 0, wxEXPAND);

    MakeBox(this, "Pitch", kYellow, outer, inner, 0, kH_Ptc);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_pitch_val = MakeVal(inner, kYellow, 11);
        s->AddStretchSpacer(1);
        s->Add(m_pitch_val, 0, wxEXPAND | wxLEFT | wxRIGHT, kBP);
        s->AddStretchSpacer(1); }
    root->Add(outer, 0, wxEXPAND);

    MakeBox(this, "Roll", kYellow, outer, inner, 0, kH_Rol);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_roll_val = MakeVal(inner, kYellow, 11);
        s->AddStretchSpacer(1);
        s->Add(m_roll_val, 0, wxEXPAND | wxLEFT | wxRIGHT, kBP);
        s->AddStretchSpacer(1); }
    root->Add(outer, 0, wxEXPAND);

    MakeBox(this, "Stability", kYellow, outer, inner, 0, kH_Stb);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_stability_val = MakeVal(inner, kYellow, 8);
        s->AddStretchSpacer(1);
        s->Add(m_stability_val, 0, wxEXPAND | wxLEFT | wxRIGHT, kBP);
        s->AddStretchSpacer(1); }
    root->Add(outer, 0, wxEXPAND);

    // ── Col 2: Destination, Bearing ────────────────────────────────────────
    MakeBox(this, "Destination", kLavender, outer, inner, 0, kH_Dst_R);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_destination_val = MakeVal(inner, kLavender, 8);
        s->Add(m_destination_val, 1, wxEXPAND | wxALL, kBP); }
    root->Add(outer, 0, wxEXPAND);

    MakeBox(this, "Bearing", kOrange, outer, inner, 0, kH_Brg_R);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_bearing_val = MakeVal(inner, kOrange, 11);
        m_bearing_corr_val = MakeVal(inner, kOrange, 11);
        s->AddStretchSpacer(1);
        s->Add(m_bearing_val,      0, wxEXPAND | wxLEFT | wxRIGHT, kBP);
        s->AddStretchSpacer(1);
        s->Add(m_bearing_corr_val, 0, wxEXPAND | wxLEFT | wxRIGHT, kBP);
        s->AddStretchSpacer(1); }
    root->Add(outer, 0, wxEXPAND);

    // ── Col 3: Distance, Course, Location ──────────────────────────────────
    MakeBox(this, "Distance", kCyan, outer, inner, 0, kH_Dis_R);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_distance_val = MakeVal(inner, kCyan, 11);
        s->Add(m_distance_val, 1, wxEXPAND | wxALL, kBP); }
    root->Add(outer, 0, wxEXPAND);

    MakeBox(this, "Course", kGreen, outer, inner, 0, kH_Crs_R);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_course_val = MakeVal(inner, kGreen, 11);
        s->Add(m_course_val, 1, wxEXPAND | wxALL, kBP); }
    root->Add(outer, 0, wxEXPAND);

    MakeBox(this, "Location", kGreen, outer, inner, 0, kH_Loc_R);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_location_val = MakeVal(inner, kGreen, 8, wxALIGN_CENTER);
        s->Add(m_location_val, 1, wxEXPAND | wxALL, kBP); }
    root->Add(outer, 0, wxEXPAND);

    // ── Time bar ───────────────────────────────────────────────────────────
    MakeBox(this, "Time", kWhite, outer, inner, 0, kH_Bar_R);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_datetime_val = MakeVal(inner, kWhite, 9);
        s->AddStretchSpacer(1);
        s->Add(m_datetime_val, 0, wxEXPAND | wxLEFT | wxRIGHT, kBP);
        s->AddStretchSpacer(1);
        m_gpsfix_val = MakeVal(inner, kWhite, 8);
        s->Add(m_gpsfix_val, 0, wxEXPAND | wxLEFT | wxRIGHT, kBP);
        s->AddStretchSpacer(1); }
    root->Add(outer, 0, wxEXPAND);

    root->AddSpacer(3);

    // ── WP + Follow ────────────────────────────────────────────────────────
    m_btn_send_wp = new wxButton(this, ID_BTN_SEND_WP, "WP");
    m_btn_send_wp->SetMinSize(wxSize(1, kBtnH_Right));
    m_btn_send_wp->Enable(false);
    m_chk_follow = new wxCheckBox(this, wxID_ANY, "Follow");
    m_chk_follow->SetForegroundColour(kWhite);
    m_chk_follow->SetBackgroundColour(kBlack);
    m_chk_follow->SetMinSize(wxSize(1, -1));
    {   auto* r = new wxBoxSizer(wxHORIZONTAL);
        r->Add(m_btn_send_wp, 1, wxEXPAND | wxRIGHT, 2);
        r->Add(m_chk_follow,  1, wxALIGN_CENTER_VERTICAL | wxLEFT, 2);
        root->Add(r, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, kPad); }

    // ── <  |  > ────────────────────────────────────────────────────────────
    m_btn_port_short = new wxButton(this, ID_BTN_PORT_SHORT, "<");
    m_btn_stbd_short = new wxButton(this, ID_BTN_STBD_SHORT, ">");
    BtnRow(m_btn_port_short, m_btn_stbd_short);

    // ── <<  |  >> ──────────────────────────────────────────────────────────
    m_btn_port_long = new wxButton(this, ID_BTN_PORT_LONG, "<<");
    m_btn_stbd_long = new wxButton(this, ID_BTN_STBD_LONG, ">>");
    BtnRow(m_btn_port_long, m_btn_stbd_long);

    // ── Mode  |  Enable/Disable ────────────────────────────────────────────
    m_btn_mode       = new wxButton(this, ID_BTN_MODE,       "Mode");
    m_btn_nav_toggle = new wxButton(this, ID_BTN_NAV_TOGGLE, "Enable");
    BtnRow(m_btn_mode, m_btn_nav_toggle);

    // ── Undock ─────────────────────────────────────────────────────────────
    m_btn_undock = new wxButton(this, ID_BTN_UNDOCK, "Undock");
    m_btn_undock->SetMinSize(wxSize(1, kBtnH_Right));
    root->Add(m_btn_undock, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, kPad);

    DisableNavButtons(m_btn_port_long, m_btn_port_short,
                      m_btn_stbd_short, m_btn_stbd_long,
                      m_btn_mode, m_btn_nav_toggle);

    SetSizer(root);
    SetScrollRate(5, 5);
    FitInside();
}

// ---------------------------------------------------------------------------
// Top/bottom-dock layout — single row of compact boxes + controls row
// ---------------------------------------------------------------------------

void AutoPilotPanel::BuildUI_TopBottom()
{
    SetBackgroundColour(kBlack);

    auto* root = new wxBoxSizer(wxVERTICAL);
    wxPanel *outer, *inner;
    const int kBtnPad = 3;

    // ── Data row: all boxes in a single horizontal band ────────────────────
    // proportion=1 for simple fields, proportion=2 for multi-line fields
    // (Destination, Location) so they get a bit more horizontal space.
    auto* data = new wxBoxSizer(wxHORIZONTAL);

    MakeBox(this, "Spd kn", kCyan, outer, inner, 0, kH_Compact);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_speed_val = MakeVal(inner, kCyan, 13);
        s->Add(m_speed_val, 1, wxEXPAND | wxALL, 2); }
    data->Add(outer, 1, wxEXPAND);

    MakeBox(this, "Hdg", kYellow, outer, inner, 0, kH_Compact);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_heading_val = MakeVal(inner, kYellow, 13);
        s->Add(m_heading_val, 1, wxEXPAND | wxALL, 2); }
    data->Add(outer, 1, wxEXPAND);

    MakeBox(this, "Pitch", kYellow, outer, inner, 0, kH_Compact);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_pitch_val = MakeVal(inner, kYellow, 13);
        s->Add(m_pitch_val, 1, wxEXPAND | wxALL, 2); }
    data->Add(outer, 1, wxEXPAND);

    MakeBox(this, "Roll", kYellow, outer, inner, 0, kH_Compact);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_roll_val = MakeVal(inner, kYellow, 13);
        s->Add(m_roll_val, 1, wxEXPAND | wxALL, 2); }
    data->Add(outer, 1, wxEXPAND);

    MakeBox(this, "Stab", kYellow, outer, inner, 0, kH_Compact);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_stability_val = MakeVal(inner, kYellow, 9);
        s->Add(m_stability_val, 1, wxEXPAND | wxALL, 1); }
    data->Add(outer, 1, wxEXPAND);

    MakeBox(this, "Destination", kLavender, outer, inner, 0, kH_Compact);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_destination_val = MakeVal(inner, kLavender, 10);
        s->Add(m_destination_val, 1, wxEXPAND | wxALL, 2); }
    data->Add(outer, 2, wxEXPAND);

    MakeBox(this, "Brg", kOrange, outer, inner, 0, kH_Compact);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_bearing_val = MakeVal(inner, kOrange, 11);
        s->Add(m_bearing_val, 1, wxEXPAND | wxALL, 2);
        m_bearing_corr_val = MakeVal(inner, kOrange, 10);
        s->Add(m_bearing_corr_val, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 2); }
    data->Add(outer, 1, wxEXPAND);

    MakeBox(this, "Dist nm", kCyan, outer, inner, 0, kH_Compact);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_distance_val = MakeVal(inner, kCyan, 13);
        s->Add(m_distance_val, 1, wxEXPAND | wxALL, 2); }
    data->Add(outer, 1, wxEXPAND);

    MakeBox(this, "Crs", kGreen, outer, inner, 0, kH_Compact);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_course_val = MakeVal(inner, kGreen, 13);
        s->Add(m_course_val, 1, wxEXPAND | wxALL, 2); }
    data->Add(outer, 1, wxEXPAND);

    MakeBox(this, "Location", kGreen, outer, inner, 0, kH_Compact);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_location_val = MakeVal(inner, kGreen, 10, wxALIGN_CENTER);
        s->Add(m_location_val, 1, wxEXPAND | wxALL, 2); }
    data->Add(outer, 2, wxEXPAND);

    MakeBox(this, "Time", kWhite, outer, inner, 0, kH_Compact);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_datetime_val = MakeVal(inner, kWhite, 11);
        s->Add(m_datetime_val, 0, wxEXPAND | wxALL, 1);
        m_gpsfix_val = MakeVal(inner, kWhite, 9);
        s->Add(m_gpsfix_val, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 1); }
    data->Add(outer, 1, wxEXPAND);

    root->Add(data, 0, wxEXPAND);

    // ── Controls row ───────────────────────────────────────────────────────
    auto* ctrl = new wxBoxSizer(wxHORIZONTAL);

    m_btn_send_wp = new wxButton(this, ID_BTN_SEND_WP, "Set WP");
    m_btn_send_wp->Enable(false);
    ctrl->Add(m_btn_send_wp, 0, wxALL | wxALIGN_CENTER_VERTICAL, kBtnPad);

    m_chk_follow = new wxCheckBox(this, wxID_ANY, "Follow");
    m_chk_follow->SetForegroundColour(kWhite);
    m_chk_follow->SetBackgroundColour(kBlack);
    ctrl->Add(m_chk_follow, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, kBtnPad);

    ctrl->AddStretchSpacer(1);

    m_btn_port_long  = new wxButton(this, ID_BTN_PORT_LONG,  "<< 10");
    m_btn_port_short = new wxButton(this, ID_BTN_PORT_SHORT, "< 1");
    m_btn_stbd_short = new wxButton(this, ID_BTN_STBD_SHORT, "1 >");
    m_btn_stbd_long  = new wxButton(this, ID_BTN_STBD_LONG,  "10 >>");
    ctrl->Add(m_btn_port_long,  0, wxALIGN_CENTER_VERTICAL | wxRIGHT, kBtnPad);
    ctrl->Add(m_btn_port_short, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, kBtnPad);
    ctrl->Add(m_btn_stbd_short, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, kBtnPad);
    ctrl->Add(m_btn_stbd_long,  0, wxALIGN_CENTER_VERTICAL | wxRIGHT, kBtnPad);

    ctrl->AddStretchSpacer(1);

    m_btn_mode       = new wxButton(this, ID_BTN_MODE,       "Mode");
    m_btn_nav_toggle = new wxButton(this, ID_BTN_NAV_TOGGLE, "Enable");
    ctrl->Add(m_btn_mode,       0, wxALIGN_CENTER_VERTICAL | wxRIGHT, kBtnPad);
    ctrl->Add(m_btn_nav_toggle, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, kBtnPad);

    ctrl->AddStretchSpacer(1);

    m_btn_undock = new wxButton(this, ID_BTN_UNDOCK, "Undock");
    ctrl->Add(m_btn_undock, 0, wxALL | wxALIGN_CENTER_VERTICAL, kBtnPad);

    root->Add(ctrl, 0, wxEXPAND | wxALL, 2);

    DisableNavButtons(m_btn_port_long, m_btn_port_short,
                      m_btn_stbd_short, m_btn_stbd_long,
                      m_btn_mode, m_btn_nav_toggle);

    SetSizer(root);
    SetScrollRate(5, 5);
    FitInside();
}

// ---------------------------------------------------------------------------
// State update — works for all three layouts; m_btn_undock may be null (float)
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

    // Middle column
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
    {
        int dest_pt = (connected && s.nav_enabled && s.mode == 1) ? 17 : 12;
        if (m_dock_mode != DockMode::FLOAT) dest_pt = (dest_pt > 12) ? 13 : 10;
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
        m_datetime_val->SetLabel(wxString::Format("%d:%02d", s.hour, s.minute));
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

    if (available && m_chk_follow->IsChecked() && m_link->IsConnected())
        m_link->SendWaypoint(lat, lon);
}

// ---------------------------------------------------------------------------
// Button handlers
// ---------------------------------------------------------------------------

void AutoPilotPanel::OnMode(wxCommandEvent&)
{
    if (!m_link || !m_link->IsConnected()) return;
    const AutoPilotState& s = m_link->State();
    if (!s.nav_enabled) return;
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

void AutoPilotPanel::OnUndock(wxCommandEvent&)
{
    // Defer to avoid re-entrancy: the callback calls AUI Update() which may
    // fire UpdateAuiStatus (and SetDockMode) before this handler returns.
    if (m_undock_cb)
        CallAfter([this]() { m_undock_cb(); });
}
