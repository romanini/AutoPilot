#include "wx/wxprec.h"
#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif
#include <wx/statline.h>

#include "AutoPilotPanel.h"

// Button / control IDs
enum {
    ID_BTN_MODE       = wxID_HIGHEST + 1,
    ID_BTN_NAV_TOGGLE,
    ID_BTN_PORT_SHORT,
    ID_BTN_PORT_LONG,
    ID_BTN_STBD_SHORT,
    ID_BTN_STBD_LONG,
    ID_BTN_SEND_WP,
    ID_BTN_SEND_ROUTE,
    ID_BTN_UNDOCK,
    ID_CHK_FOLLOW,
};

const float AutoPilotPanel::ADJUSTMENT_SHORT     = 1.0f;
const float AutoPilotPanel::ADJUSTMENT_LONG      = 10.0f;
const int   AutoPilotPanel::HEARTBEAT_INTERVAL_MS = 1000;  // 1 s — well within controller's ~6 s timeout

wxBEGIN_EVENT_TABLE(AutoPilotPanel, wxScrolledWindow)
    EVT_BUTTON(ID_BTN_MODE,       AutoPilotPanel::OnMode)
    EVT_BUTTON(ID_BTN_NAV_TOGGLE, AutoPilotPanel::OnNavToggle)
    EVT_BUTTON(ID_BTN_PORT_SHORT, AutoPilotPanel::OnPortShort)
    EVT_BUTTON(ID_BTN_PORT_LONG,  AutoPilotPanel::OnPortLong)
    EVT_BUTTON(ID_BTN_STBD_SHORT, AutoPilotPanel::OnStbdShort)
    EVT_BUTTON(ID_BTN_STBD_LONG,  AutoPilotPanel::OnStbdLong)
    EVT_BUTTON(ID_BTN_SEND_WP,    AutoPilotPanel::OnSendWP)
    EVT_BUTTON(ID_BTN_SEND_ROUTE, AutoPilotPanel::OnSendRoute)
    EVT_BUTTON(ID_BTN_UNDOCK,     AutoPilotPanel::OnUndock)
    EVT_CHECKBOX(ID_CHK_FOLLOW,   AutoPilotPanel::OnFollowChanged)
    EVT_TIMER(wxID_ANY,           AutoPilotPanel::OnHeartbeat)
wxEND_EVENT_TABLE()

// ---------------------------------------------------------------------------
// TFT-matching color palette (RGB565 decoded to 24-bit). kLavender is the
// display unit's single MAGENTA (0xF57F) constant. Colors are assigned by
// data source, not by column or box, mirroring the scheme documented at the
// top of Arduino/display/screen.ino:
//   Yellow  - data from the compass (Heading)
//   Cyan    - data straight from the GPS (Speed, Location, Date/Time)
//   Lavender/magenta - data calculated on the controller (Mode, Target,
//             Correction, Distance, Track)
//   White   - general info / data sent in rather than measured or computed
//             (fix/satellites, Waypoint - a commanded destination, not
//             something read off a sensor)
// ---------------------------------------------------------------------------
static const wxColour kBlack   (  0,   0,   0);
static const wxColour kCyan    (  0, 255, 255);
static const wxColour kYellow  (255, 255,   0);
static const wxColour kLavender(247, 174, 255);
static const wxColour kWhite   (255, 255, 255);

// ---------------------------------------------------------------------------
// Pixel dimensions for the floating (3-column) layout
// ---------------------------------------------------------------------------
static const int kColW = 160;

// Left column: Heading / Track / Speed, all equal weight (matches the display
// unit giving all three the same big font/box size). Their combined height
// (150) is intentionally less than the middle/right columns (180): the
// leftover 30px becomes the "notch" row (Set WP + Follow) below them.
static const int kH_Hdg = 50;
static const int kH_Spd = 50;
static const int kH_Trk = 50;
static const int kH_Notch = 30;

// Middle column: Mode (compact) / Target+Correction / Time — Time sits right
// under Target so its bottom edge lines up with Waypoint's in the right
// column (45 + 90 + 45 = 180 = 65 + 50 + 65).
static const int kH_Mode = 45;
static const int kH_Tgt  = 90;
static const int kH_TimeFloat = 45;

// Right column: Location / Distance / Waypoint
static const int kH_Loc = 65;
static const int kH_Dis = 50;
static const int kH_Way = 65;

static const int kH_Bar = 38;

// Right-dock layout: narrow column, taller touch buttons, tighter boxes
static const int kColW_Right = 100;  // natural sizer floor; AUI enforces this via kRightDockW
static const int kBtnH_Right = 38;   // taller than default for touch targets

// Height for compact boxes in the top/bottom docked layout. Kept low because
// every value in this layout is a single line (lat/lon, target/correction,
// and time/fix are each packed onto one row) to minimize vertical chart
// coverage when docked to the top or bottom.
static const int kH_Compact = 50;

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
    , m_heartbeat_timer(this)
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

    bool follow_checked = m_chk_follow->IsChecked();  // preserve across rebuild

    Freeze();
    DestroyChildren();  // schedules all child windows for deletion
    m_btn_undock = nullptr;

    switch (mode) {
        case DockMode::FLOAT:      BuildUI_Float();    break;
        case DockMode::RIGHT:      BuildUI_Right();    break;
        case DockMode::TOP_BOTTOM: BuildUI_TopBottom();break;
    }
    m_chk_follow->SetValue(follow_checked);
    Layout();
    Thaw();
    return true;
}

// ---------------------------------------------------------------------------
// Floating layout — fixed 3-column 480px grid, mirroring the display unit's
// Heading/Track/Speed | Mode/Target+Correction/Time | Location/Distance/
// Waypoint column arrangement (Arduino/display/screen.ino), plus the
// plugin-only "notch" row (Set WP + Follow) and control row (Send Rte, Mode,
// adjust, Enable/Disable) that don't exist on the physical display.
// ---------------------------------------------------------------------------

void AutoPilotPanel::BuildUI_Float()
{
    SetBackgroundColour(kBlack);

    auto* root = new wxBoxSizer(wxVERTICAL);

    wxPanel *outer, *inner;

    // ── 3-column data grid ─────────────────────────────────────────────────
    auto* cols = new wxBoxSizer(wxHORIZONTAL);

    // ── Left column: Heading / Track / Speed, then the "notch" row (Set WP +
    // Follow) that exactly fills the 30px this column is shorter than the
    // middle/right columns (150 vs. 180) — see kH_Notch.
    auto* left = new wxBoxSizer(wxVERTICAL);

    MakeBox(this, "Heading", kYellow, outer, inner, kColW, kH_Hdg);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_heading_val = MakeVal(inner, kYellow, 17);
        s->Add(m_heading_val, 1, wxEXPAND | wxALL, 2); }
    left->Add(outer, 0, wxEXPAND);

    // Track before Speed, matching the display unit's column 1 order.
    // Damped/trust-gated GPS track (not raw course, which is noisy below
    // ~1kn) — see AutoPilotLink::ParseApdat's cog_damped/cog_damped_valid.
    // Lavender/magenta: it's a controller-computed value, not a raw GPS read.
    MakeBox(this, "Track", kLavender, outer, inner, kColW, kH_Trk);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_track_val = MakeVal(inner, kLavender, 17);
        s->Add(m_track_val, 1, wxEXPAND | wxALL, 2); }
    left->Add(outer, 0, wxEXPAND);

    MakeBox(this, "Speed kn", kCyan, outer, inner, kColW, kH_Spd);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_speed_val = MakeVal(inner, kCyan, 17);
        s->Add(m_speed_val, 1, wxEXPAND | wxALL, 2); }
    left->Add(outer, 0, wxEXPAND);

    {   auto* notch = new wxPanel(this, wxID_ANY);
        notch->SetBackgroundColour(kBlack);
        notch->SetMinSize(wxSize(kColW, kH_Notch));
        auto* s = new wxBoxSizer(wxHORIZONTAL);
        notch->SetSizer(s);

        // Shorter than the notch itself (unlike the other control-row
        // buttons, which just take their natural size) so a gap opens up
        // between it and the Speed box above; bottom-aligned and capped
        // under kH_Notch so the notch's own height — and this button's
        // bottom edge — doesn't move. Same width as Send Rte (kBtnSz, 62px)
        // below, with a 5px leading spacer so their left edges line up: the
        // control row below starts at the root sizer's 3px left margin plus
        // one (474-458)/8 = 2px stretch-spacer unit = 5px from the panel's
        // left edge, and this "left"/"cols"/"notch" chain has no left
        // padding of its own, so the two start from the same x origin.
        s->AddSpacer(5);
        m_btn_send_wp = new wxButton(notch, ID_BTN_SEND_WP, "Set WP");
        m_btn_send_wp->SetMinSize(wxSize(62, 24));
        m_btn_send_wp->Enable(false);
        s->Add(m_btn_send_wp, 0, wxALIGN_BOTTOM | wxRIGHT, 3);

        // Matched to the button's height and also bottom-aligned (rather than
        // centered in the whole 30px notch row) so the two share the same
        // vertical band and end up centered on each other, not just on the
        // taller row.
        m_chk_follow = new wxCheckBox(notch, ID_CHK_FOLLOW, "Follow");
        m_chk_follow->SetForegroundColour(kWhite);
        m_chk_follow->SetBackgroundColour(kBlack);
        m_chk_follow->SetMinSize(wxSize(-1, 24));
        s->Add(m_chk_follow, 0, wxALIGN_BOTTOM);

        left->Add(notch, 0, wxEXPAND); }

    cols->Add(left, 0, wxEXPAND);

    // ── Middle column: Mode / Target + Correction / Time ───────────────────
    auto* mid = new wxBoxSizer(wxVERTICAL);

    // Folds in nav_source (who's steering) the same way display_mode() does,
    // so there is no separate "Following" box any more.
    MakeBox(this, "Mode", kLavender, outer, inner, kColW, kH_Mode);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_mode_val = MakeVal(inner, kLavender, 12);
        s->AddStretchSpacer(1);
        s->Add(m_mode_val, 0, wxEXPAND | wxLEFT | wxRIGHT, 2);
        s->AddStretchSpacer(1); }
    mid->Add(outer, 0, wxEXPAND);

    MakeBox(this, "Target", kLavender, outer, inner, kColW, kH_Tgt);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_target_val = MakeVal(inner, kLavender, 17);
        s->Add(m_target_val, 1, wxEXPAND | wxALL, 2);
        m_correction_val = MakeVal(inner, kLavender, 14);
        s->Add(m_correction_val, 1, wxEXPAND | wxALL, 2); }
    mid->Add(outer, 0, wxEXPAND);

    // Sits right under Target so its bottom edge lines up with Waypoint's,
    // in the right column below (kH_Mode + kH_Tgt + kH_TimeFloat = kH_Loc +
    // kH_Dis + kH_Way = 180).
    MakeBox(this, "Time", kCyan, outer, inner, kColW, kH_TimeFloat);
    {   auto* s = new wxBoxSizer(wxHORIZONTAL);
        inner->SetSizer(s);
        m_datetime_val = MakeVal(inner, kCyan, 11);
        m_gpsfix_val   = MakeVal(inner, kCyan, 10);
        s->AddStretchSpacer(1);
        s->Add(m_datetime_val, 0, wxALL | wxALIGN_CENTER_VERTICAL, 1);
        s->Add(m_gpsfix_val,   0, wxALL | wxALIGN_CENTER_VERTICAL, 1);
        s->AddStretchSpacer(1); }
    mid->Add(outer, 0, wxEXPAND);

    cols->Add(mid, 0, wxEXPAND);

    // ── Right column: Location / Distance / Waypoint ───────────────────────
    auto* right = new wxBoxSizer(wxVERTICAL);

    MakeBox(this, "Location", kCyan, outer, inner, kColW, kH_Loc);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_location_val = MakeVal(inner, kCyan, 12, wxALIGN_CENTER);
        s->Add(m_location_val, 1, wxEXPAND | wxALL, 2); }
    right->Add(outer, 0, wxEXPAND);

    MakeBox(this, "Distance nm", kLavender, outer, inner, kColW, kH_Dis);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_distance_val = MakeVal(inner, kLavender, 17);
        s->Add(m_distance_val, 1, wxEXPAND | wxALL, 2); }
    right->Add(outer, 0, wxEXPAND);

    // Commanded waypoint lat/lon — new box, mirrors display_waypoint_lat/lon.
    MakeBox(this, "Waypoint", kWhite, outer, inner, kColW, kH_Way);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_waypoint_val = MakeVal(inner, kWhite, 12, wxALIGN_CENTER);
        s->Add(m_waypoint_val, 1, wxEXPAND | wxALL, 2); }
    right->Add(outer, 0, wxEXPAND);

    cols->Add(right, 0, wxEXPAND);
    root->Add(cols, 0, 0);

    // ── Controls: Send Rte, then the mode/adjust/enable buttons ────────────
    // Set WP and Follow now live in the notch above, so this row holds
    // everything else, starting with Send Rte.
    root->Add(new wxStaticLine(this, wxID_ANY, wxDefaultPosition,
                               wxSize(kPanelW, 1)),
              0, wxTOP, 6);
    root->AddSpacer(4);

    const wxSize kBtnSz(62, -1);
    m_btn_send_route = new wxButton(this, ID_BTN_SEND_ROUTE, "Send Rte");
    m_btn_mode       = new wxButton(this, ID_BTN_MODE,       "Mode");
    m_btn_port_long  = new wxButton(this, ID_BTN_PORT_LONG,  "<< 10");
    m_btn_port_short = new wxButton(this, ID_BTN_PORT_SHORT, "< 1");
    m_btn_stbd_short = new wxButton(this, ID_BTN_STBD_SHORT, "1 >");
    m_btn_stbd_long  = new wxButton(this, ID_BTN_STBD_LONG,  "10 >>");
    m_btn_nav_toggle = new wxButton(this, ID_BTN_NAV_TOGGLE, "Enable");
    m_btn_send_route->SetMinSize(kBtnSz);
    m_btn_mode->SetMinSize(kBtnSz);
    m_btn_port_long->SetMinSize(kBtnSz);
    m_btn_port_short->SetMinSize(kBtnSz);
    m_btn_stbd_short->SetMinSize(kBtnSz);
    m_btn_stbd_long->SetMinSize(kBtnSz);
    m_btn_nav_toggle->SetMinSize(kBtnSz);
    m_btn_send_route->Enable(false);

    auto* btn_row = new wxBoxSizer(wxHORIZONTAL);
    btn_row->AddStretchSpacer(1);
    btn_row->Add(m_btn_send_route, 0, wxRIGHT, 4);
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
    const int kH_Hdg_R = 45;   // heading: tall enough the value isn't clipped
    const int kH_Spd_R = 45;   // speed: tall enough the value isn't clipped
    const int kH_Trk_R = 45;   // track: matches heading/speed
    const int kH_Mode_R = 42;  // mode: shorter
    const int kH_Tgt_R  = 59;  // target+correction: trimmed more than Track grew
    const int kH_Loc_R = 50;   // location: shorter
    const int kH_Dis_R = 38;   // distance: shorter single value
    const int kH_Way_R = 50;   // waypoint: shorter
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

    // ── Col 1: Heading, Track, Speed ────────────────────────────────────────
    MakeBox(this, "Heading", kYellow, outer, inner, 0, kH_Hdg_R);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_heading_val = MakeVal(inner, kYellow, 13);
        s->Add(m_heading_val, 1, wxEXPAND | wxALL, kBP); }
    root->Add(outer, 0, wxEXPAND);

    // Track before Speed, matching the display unit's column 1 order.
    MakeBox(this, "Track", kLavender, outer, inner, 0, kH_Trk_R);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_track_val = MakeVal(inner, kLavender, 13);
        s->Add(m_track_val, 1, wxEXPAND | wxALL, kBP); }
    root->Add(outer, 0, wxEXPAND);

    MakeBox(this, "Speed", kCyan, outer, inner, 0, kH_Spd_R);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_speed_val = MakeVal(inner, kCyan, 13);
        s->Add(m_speed_val, 1, wxEXPAND | wxALL, kBP); }
    root->Add(outer, 0, wxEXPAND);

    // ── Col 2: Mode, Target + Correction ────────────────────────────────────
    MakeBox(this, "Mode", kLavender, outer, inner, 0, kH_Mode_R);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_mode_val = MakeVal(inner, kLavender, 8);
        s->Add(m_mode_val, 1, wxEXPAND | wxALL, kBP); }
    root->Add(outer, 0, wxEXPAND);

    // Target and correction share one wxStaticText (two lines, "\n"-joined —
    // see the DockMode::RIGHT branch in UpdateFromState) instead of being two
    // separate controls with a manually-tuned gap between them: two widgets
    // each carry their own top/bottom padding on top of kH_Tgt_R, which kept
    // clipping correction no matter how the spacers were tuned. One widget
    // with an embedded newline gets the same tight, natural inter-line gap
    // Location already uses for lat/lon.
    MakeBox(this, "Target", kLavender, outer, inner, 0, kH_Tgt_R);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_target_val = MakeVal(inner, kLavender, 11, wxALIGN_CENTER);
        s->Add(m_target_val, 1, wxEXPAND | wxALL, kBP);
        // Kept alive (UpdateFromState always writes to it) but unused in
        // this layout — not added to the sizer, so it takes no space.
        m_correction_val = MakeVal(inner, kLavender, 11);
        m_correction_val->Hide(); }
    root->Add(outer, 0, wxEXPAND);

    // ── Col 3: Location, Distance, Waypoint ─────────────────────────────────
    MakeBox(this, "Location", kCyan, outer, inner, 0, kH_Loc_R);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_location_val = MakeVal(inner, kCyan, 8, wxALIGN_CENTER);
        s->Add(m_location_val, 1, wxEXPAND | wxALL, kBP); }
    root->Add(outer, 0, wxEXPAND);

    MakeBox(this, "Distance", kLavender, outer, inner, 0, kH_Dis_R);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_distance_val = MakeVal(inner, kLavender, 11);
        s->Add(m_distance_val, 1, wxEXPAND | wxALL, kBP); }
    root->Add(outer, 0, wxEXPAND);

    MakeBox(this, "Waypoint", kWhite, outer, inner, 0, kH_Way_R);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_waypoint_val = MakeVal(inner, kWhite, 8, wxALIGN_CENTER);
        s->Add(m_waypoint_val, 1, wxEXPAND | wxALL, kBP); }
    root->Add(outer, 0, wxEXPAND);

    // ── Time bar ───────────────────────────────────────────────────────────
    MakeBox(this, "Time", kCyan, outer, inner, 0, kH_Bar_R);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_datetime_val = MakeVal(inner, kCyan, 9);
        s->AddStretchSpacer(1);
        s->Add(m_datetime_val, 0, wxEXPAND | wxLEFT | wxRIGHT, kBP);
        s->AddStretchSpacer(1);
        m_gpsfix_val = MakeVal(inner, kCyan, 8);
        s->Add(m_gpsfix_val, 0, wxEXPAND | wxLEFT | wxRIGHT, kBP);
        s->AddStretchSpacer(1); }
    root->Add(outer, 0, wxEXPAND);

    root->AddSpacer(3);

    // ── WP + Rte row ───────────────────────────────────────────────────────
    m_btn_send_wp = new wxButton(this, ID_BTN_SEND_WP, "WP");
    m_btn_send_wp->SetMinSize(wxSize(1, kBtnH_Right));
    m_btn_send_wp->Enable(false);
    m_btn_send_route = new wxButton(this, ID_BTN_SEND_ROUTE, "Rte");
    m_btn_send_route->SetMinSize(wxSize(1, kBtnH_Right));
    m_btn_send_route->Enable(false);
    {   auto* r = new wxBoxSizer(wxHORIZONTAL);
        r->Add(m_btn_send_wp,    1, wxEXPAND | wxRIGHT, 2);
        r->Add(m_btn_send_route, 1, wxEXPAND);
        root->Add(r, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, kPad); }

    // ── Follow ─────────────────────────────────────────────────────────────
    m_chk_follow = new wxCheckBox(this, ID_CHK_FOLLOW, "Follow");
    m_chk_follow->SetForegroundColour(kWhite);
    m_chk_follow->SetBackgroundColour(kBlack);
    root->Add(m_chk_follow, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, kPad);

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
    // proportion=2 for simple single-value fields, =4 for two-value fields
    // (Location, Waypoint) that need more horizontal space. Target and Time
    // are both two-value fields too, but sit at 3 (not 4): Target was too
    // wide relative to Time, so one unit was moved from Target to Time.
    auto* data = new wxBoxSizer(wxHORIZONTAL);

    // All values in this layout share one font size (kPt_Compact, matching
    // Heading) so nothing looks emphasized/de-emphasized relative to the rest,
    // and so paired values (Target/Correction, Time/Fix) share a baseline.
    const int kPt_Compact = 13;

    MakeBox(this, "Heading", kYellow, outer, inner, 0, kH_Compact);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_heading_val = MakeVal(inner, kYellow, kPt_Compact);
        s->Add(m_heading_val, 1, wxEXPAND | wxALL, 2); }
    data->Add(outer, 2, wxEXPAND);

    // Track before Speed here, matching the display unit's column 1 order
    // (Heading / Track / Speed) after its latest layout change.
    MakeBox(this, "Track", kLavender, outer, inner, 0, kH_Compact);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_track_val = MakeVal(inner, kLavender, kPt_Compact);
        s->Add(m_track_val, 1, wxEXPAND | wxALL, 2); }
    data->Add(outer, 2, wxEXPAND);

    MakeBox(this, "Speed", kCyan, outer, inner, 0, kH_Compact);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_speed_val = MakeVal(inner, kCyan, kPt_Compact);
        s->Add(m_speed_val, 1, wxEXPAND | wxALL, 2); }
    data->Add(outer, 2, wxEXPAND);

    MakeBox(this, "Mode", kLavender, outer, inner, 0, kH_Compact);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_mode_val = MakeVal(inner, kLavender, kPt_Compact);
        s->Add(m_mode_val, 1, wxEXPAND | wxALL, 2); }
    data->Add(outer, 2, wxEXPAND);

    // Target + Correction side by side on one line (not stacked) so this box
    // stays as short as everything else in this layout.
    MakeBox(this, "Target", kLavender, outer, inner, 0, kH_Compact);
    {   auto* s = new wxBoxSizer(wxHORIZONTAL);
        inner->SetSizer(s);
        m_target_val = MakeVal(inner, kLavender, kPt_Compact);
        m_correction_val = MakeVal(inner, kLavender, kPt_Compact);
        s->Add(m_target_val,     1, wxEXPAND | wxALL, 2);
        s->Add(m_correction_val, 1, wxEXPAND | wxALL, 2); }
    data->Add(outer, 3, wxEXPAND);

    // Lat/lon on one line ("lat, lon") rather than stacked — see the
    // dock-mode branch in UpdateFromState.
    MakeBox(this, "Location", kCyan, outer, inner, 0, kH_Compact);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_location_val = MakeVal(inner, kCyan, kPt_Compact, wxALIGN_CENTER);
        s->Add(m_location_val, 1, wxEXPAND | wxALL, 2); }
    data->Add(outer, 4, wxEXPAND);

    MakeBox(this, "Distance", kLavender, outer, inner, 0, kH_Compact);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_distance_val = MakeVal(inner, kLavender, kPt_Compact);
        s->Add(m_distance_val, 1, wxEXPAND | wxALL, 2); }
    data->Add(outer, 2, wxEXPAND);

    MakeBox(this, "Waypoint", kWhite, outer, inner, 0, kH_Compact);
    {   auto* s = new wxBoxSizer(wxVERTICAL);
        inner->SetSizer(s);
        m_waypoint_val = MakeVal(inner, kWhite, kPt_Compact, wxALIGN_CENTER);
        s->Add(m_waypoint_val, 1, wxEXPAND | wxALL, 2); }
    data->Add(outer, 4, wxEXPAND);

    // Time + fix side by side on one line (not stacked). Same font/padding on
    // both so they share a baseline instead of the fix text sitting a few
    // pixels higher than the time.
    MakeBox(this, "Time", kCyan, outer, inner, 0, kH_Compact);
    {   auto* s = new wxBoxSizer(wxHORIZONTAL);
        inner->SetSizer(s);
        m_datetime_val = MakeVal(inner, kCyan, kPt_Compact);
        m_gpsfix_val = MakeVal(inner, kCyan, kPt_Compact);
        s->Add(m_datetime_val, 1, wxEXPAND | wxALL, 1);
        s->Add(m_gpsfix_val,   1, wxEXPAND | wxALL, 1); }
    data->Add(outer, 3, wxEXPAND);

    root->Add(data, 0, wxEXPAND);

    // ── Controls row ───────────────────────────────────────────────────────
    auto* ctrl = new wxBoxSizer(wxHORIZONTAL);

    m_btn_send_wp = new wxButton(this, ID_BTN_SEND_WP, "Set WP");
    m_btn_send_wp->Enable(false);
    ctrl->Add(m_btn_send_wp, 0, wxALL | wxALIGN_CENTER_VERTICAL, kBtnPad);

    m_btn_send_route = new wxButton(this, ID_BTN_SEND_ROUTE, "Send Rte");
    m_btn_send_route->Enable(false);
    ctrl->Add(m_btn_send_route, 0, wxALL | wxALIGN_CENTER_VERTICAL, kBtnPad);

    m_chk_follow = new wxCheckBox(this, ID_CHK_FOLLOW, "Follow");
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
    // Left column: Heading / Speed / Track
    m_heading_val->SetLabel(connected ? wxString::Format("%.1f\xc2\xb0", s.heading) : "--");
    m_speed_val->SetLabel(s.fix ? wxString::Format("%.2f", s.speed) : "--");
    // Damped/trust-gated GPS track — blank until cog_damped_valid, matching
    // display_track()'s treatment of raw course as noisy below ~1kn.
    m_track_val->SetLabel(s.cog_damped_valid ? wxString::Format("%.1f\xc2\xb0", s.cog_damped) : "--");

    // Middle column: Mode (folds in nav_source, mirrors display_mode()) /
    // Target + Correction
    wxString mode;
    if (!connected) {
        mode = "No link";
    } else if (!s.nav_enabled) {
        mode = "Disabled";
    } else if (s.mode == 2) {
        switch (s.nav_source) {
            case 1:  mode = "Garmin";   break;
            case 2:  mode = "OpenCPN";  break;
            default: mode = "Waypoint"; break;
        }
    } else if (s.mode == 1) {
        mode = "Compass";
    } else {
        mode = wxString::Format("Mode %d", s.mode);
    }
    m_mode_val->SetLabel(mode);

    if (connected && s.nav_enabled) {
        wxString tgt = wxString::Format("%.1f\xc2\xb0", s.heading_desired);
        double bc = s.bearing_correction;
        wxString corr = wxString::Format("%.1f\xc2\xb0 %s", bc >= 0 ? bc : -bc, bc >= 0 ? "R" : "L");
        // Right-dock combines both into m_target_val (see BuildUI_Right —
        // m_correction_val is hidden and unused there).
        if (m_dock_mode == DockMode::RIGHT) {
            m_target_val->SetLabel(tgt + "\n" + corr);
        } else {
            m_target_val->SetLabel(tgt);
            m_correction_val->SetLabel(corr);
        }
    } else {
        m_target_val->SetLabel(m_dock_mode == DockMode::RIGHT ? "--\n--" : "--");
        m_correction_val->SetLabel("--");
    }

    // Right column: Location / Distance / Waypoint. Top/bottom-docked keeps
    // lat/lon on one line ("lat, lon") to minimize vertical chart coverage;
    // the other layouts have room to stack them.
    const wxString latlon_sep = (m_dock_mode == DockMode::TOP_BOTTOM) ? ", " : "\n";
    m_location_val->SetLabel(
        s.fix ? wxString::Format("%.6f%s%.6f", s.location_lat, latlon_sep, s.location_lon) : "--");
    m_distance_val->SetLabel(
        (s.waypoint_set && s.fix) ? wxString::Format("%.2f", s.distance) : "--");
    m_waypoint_val->SetLabel(
        s.waypoint_set ? wxString::Format("%.6f%s%.6f", s.wp_lat, latlon_sep, s.wp_lon) : "--");

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
    m_btn_send_route->Enable(connected && !m_route_guid.IsEmpty());

    Layout();
}

void AutoPilotPanel::SetNavigateTarget(bool available, double lat, double lon)
{
    m_navigate_available = available;
    m_navigate_lat = lat;
    m_navigate_lon = lon;
    m_btn_send_wp->Enable(m_link->IsConnected() && available);

    if (!available) {
        // Active leg cleared — kill the heartbeat stream and reflect in UI
        m_heartbeat_timer.Stop();
        m_chk_follow->SetValue(false);
        if (m_link->IsConnected())
            m_link->SendStopFollow();
    } else if (m_chk_follow->IsChecked() && m_link->IsConnected()) {
        // New target while Follow is active: send immediately and (re)start the timer
        m_link->SendWaypoint(lat, lon);
        if (!m_heartbeat_timer.IsRunning())
            m_heartbeat_timer.Start(HEARTBEAT_INTERVAL_MS);
    }
}

// ---------------------------------------------------------------------------
// Follow checkbox + heartbeat timer
// ---------------------------------------------------------------------------

void AutoPilotPanel::OnFollowChanged(wxCommandEvent&)
{
    if (m_chk_follow->IsChecked()) {
        if (m_navigate_available && m_link->IsConnected()) {
            m_link->SendWaypoint(m_navigate_lat, m_navigate_lon);
            m_heartbeat_timer.Start(HEARTBEAT_INTERVAL_MS);
        }
    } else {
        m_heartbeat_timer.Stop();
        if (m_link->IsConnected())
            m_link->SendStopFollow();
    }
}

void AutoPilotPanel::OnHeartbeat(wxTimerEvent&)
{
    if (!m_navigate_available || !m_link->IsConnected()) {
        m_heartbeat_timer.Stop();
        return;
    }
    // OpenCPN does not call SetActiveLegInfo when a route is simply deactivated
    // from the route manager — it only fires on leg transitions.  Poll here so
    // the follow state clears within one heartbeat interval of deactivation.
    if (GetActiveWaypointGUID().IsEmpty()) {
        SetNavigateTarget(false);
        return;
    }
    m_link->SendWaypoint(m_navigate_lat, m_navigate_lon);
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

void AutoPilotPanel::SetActiveRoute(const wxString& guid)
{
    m_route_guid = guid;
    m_btn_send_route->Enable(m_link->IsConnected() && !guid.IsEmpty());
}

void AutoPilotPanel::OnSendRoute(wxCommandEvent&)
{
    if (!m_link || !m_link->IsConnected() || m_route_guid.IsEmpty()) return;
    auto route = GetRoute_Plugin(m_route_guid);
    if (!route || !route->pWaypointList || route->pWaypointList->IsEmpty()) return;
    m_link->SendRoute(route.get(), "OCPN01");

    // Auto-enable Follow so the controller gets a sustained w heartbeat (Scenario 1).
    m_chk_follow->SetValue(true);
    if (m_navigate_available && m_link->IsConnected()) {
        m_link->SendWaypoint(m_navigate_lat, m_navigate_lon);
        if (!m_heartbeat_timer.IsRunning())
            m_heartbeat_timer.Start(HEARTBEAT_INTERVAL_MS);
    }
}

void AutoPilotPanel::OnUndock(wxCommandEvent&)
{
    // Defer to avoid re-entrancy: the callback calls AUI Update() which may
    // fire UpdateAuiStatus (and SetDockMode) before this handler returns.
    if (m_undock_cb)
        CallAfter([this]() { m_undock_cb(); });
}
