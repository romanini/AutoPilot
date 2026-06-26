#include "wx/wxprec.h"
#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif

#include "autopilot_pi.h"
#include "version.h"

#include <wx/aui/aui.h>
#include <cmath>


// ---------------------------------------------------------------------------
// Plugin factory — required entry points for OpenCPN to load/unload
// ---------------------------------------------------------------------------

extern "C" DECL_EXP opencpn_plugin* create_pi(void* ppimgr) {
    return new AutoPilotPlugin(ppimgr);
}

extern "C" DECL_EXP void destroy_pi(opencpn_plugin* p) {
    delete p;
}

// ---------------------------------------------------------------------------

AutoPilotPlugin::AutoPilotPlugin(void* ppimgr)
    : opencpn_plugin_117(ppimgr)
    , m_link(nullptr)
    , m_panel(nullptr)
    , m_aui_mgr(nullptr)
    , m_toolbar_id(-1)
    , m_panel_shown(false)
{}

AutoPilotPlugin::~AutoPilotPlugin() {}

static wxBitmap MakeToolBitmap() {
    const int SZ = 32;

    // Draw white on black so pixel brightness == desired alpha.
    // Cairo AA produces intermediate values at edges; those become
    // correctly semi-transparent with no colour contamination.
    wxBitmap bmp(SZ, SZ);
    wxMemoryDC dc(bmp);
    dc.SetBackground(*wxBLACK_BRUSH);
    dc.Clear();

    const int    cx = SZ / 2;
    const int    cy = SZ / 2;
    const wxColour W(255, 255, 255);
    const double PI = 3.14159265358979;

    // Outer rim
    dc.SetPen(wxPen(W, 2));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawCircle(cx, cy, 11);

    // 8 spokes (hub → inner edge of rim) + handle knobs beyond rim
    for (int i = 0; i < 8; i++) {
        double a = i * PI / 4.0;
        dc.SetPen(wxPen(W, 1));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawLine(
            cx + (int)(3 * cos(a)), cy + (int)(3 * sin(a)),
            cx + (int)(10 * cos(a)), cy + (int)(10 * sin(a))
        );
        dc.SetBrush(wxBrush(W));
        dc.DrawCircle(
            cx + (int)(13 * cos(a)), cy + (int)(13 * sin(a)), 2
        );
    }

    // Center hub
    dc.SetPen(wxPen(W, 1));
    dc.SetBrush(wxBrush(W));
    dc.DrawCircle(cx, cy, 3);

    dc.SelectObject(wxNullBitmap);

    // Use brightness as alpha, recolour every pixel to the target gray.
    // Anti-aliased edge pixels get proportional alpha — no colour fringing.
    wxImage img = bmp.ConvertToImage();
    img.InitAlpha();
    const unsigned char TARGET = 170;
    for (int y = 0; y < SZ; y++) {
        for (int x = 0; x < SZ; x++) {
            unsigned char a = img.GetRed(x, y);  // white=255 → opaque, black=0 → transparent
            img.SetRGB(x, y, TARGET, TARGET, TARGET);
            img.SetAlpha(x, y, a);
        }
    }
    return wxBitmap(img);
}

int AutoPilotPlugin::Init() {
    m_aui_mgr = GetFrameAuiManager();
    if (!m_aui_mgr) {
        wxLogError("AutoPilot plugin: GetFrameAuiManager() returned null");
        return 0;
    }

    wxWindow* canvas = GetOCPNCanvasWindow();
    if (!canvas) {
        wxLogError("AutoPilot plugin: GetOCPNCanvasWindow() returned null");
        return 0;
    }

    m_link = new AutoPilotLink(nullptr);
    m_panel = new AutoPilotPanel(canvas, m_link);
    m_link->SetPanel(m_panel);

    // Capture the floating-layout content size once (after BuildUI_Float).
    // Used to resize the floating frame whenever the panel is floating.
    m_content_size = m_panel->GetVirtualSize();

    // Undock callback: float the pane and let UpdateAuiStatus rebuild the layout.
    m_panel->SetUndockCallback([this]() {
        if (!m_aui_mgr) return;
        wxAuiPaneInfo& pane = m_aui_mgr->GetPane("AutoPilot");
        pane.Float().FloatingSize(m_content_size.x + 4, m_content_size.y + 30);
        m_aui_mgr->Update();
    });
    wxSize content = m_content_size;
    wxAuiPaneInfo pane;
    pane.Name("AutoPilot")
        .Caption("AutoPilot")
        .Float()
        .FloatingSize(wxSize(content.x + 4, content.y + 30))
        .MinSize  (wxSize(200, 150))
        .CloseButton(true)
        .Hide();
    m_aui_mgr->AddPane(m_panel, pane);
    m_aui_mgr->Update();

    wxBitmap bmp = MakeToolBitmap();
    m_toolbar_id = InsertPlugInTool(
        "AutoPilot", &bmp, &bmp,
        wxITEM_NORMAL,
        "AutoPilot",
        "Toggle AutoPilot status panel",
        nullptr, -1, 0, this);

    // wxEVT_AUI_RENDER fires after every AUI layout change, including
    // drag-to-dock and drag-to-float.  OpenCPN's UpdateAuiStatus() is only
    // called on specific events (pane close) and misses live dock operations.
    m_aui_mgr->Bind(wxEVT_AUI_RENDER, &AutoPilotPlugin::OnAuiRender, this);

    m_link->Start();

    return WANTS_TOOLBAR_CALLBACK
         | INSTALLS_TOOLBAR_TOOL
         | USES_AUI_MANAGER
         | WANTS_PLUGIN_MESSAGING
         | WANTS_NMEA_EVENTS;
}

bool AutoPilotPlugin::DeInit() {
    if (m_aui_mgr)
        m_aui_mgr->Unbind(wxEVT_AUI_RENDER, &AutoPilotPlugin::OnAuiRender, this);

    if (m_link) {
        m_link->Stop();
        delete m_link;
        m_link = nullptr;
    }
    if (m_panel && m_aui_mgr) {
        m_aui_mgr->DetachPane(m_panel);
        m_aui_mgr->Update();
        m_panel->Destroy();
        m_panel = nullptr;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Version / description
// ---------------------------------------------------------------------------

int     AutoPilotPlugin::GetAPIVersionMajor()    { return MY_API_VERSION_MAJOR; }
int     AutoPilotPlugin::GetAPIVersionMinor()    { return MY_API_VERSION_MINOR; }
int     AutoPilotPlugin::GetPlugInVersionMajor() { return PLUGIN_VERSION_MAJOR; }
int     AutoPilotPlugin::GetPlugInVersionMinor() { return PLUGIN_VERSION_MINOR; }

wxString AutoPilotPlugin::GetShortDescription() {
    return "AutoPilot panel — live controller status and commands via UDP";
}

wxString AutoPilotPlugin::GetLongDescription() {
    return "Displays real-time status from the AutoPilot controller "
           "(heading, bearing, mode, GPS fix, waypoint) and provides "
           "controls for mode selection, navigation enable/disable, and "
           "port/starboard heading adjust. The 'Navigate' button sends "
           "the currently active OpenCPN waypoint to the controller.";
}

wxString AutoPilotPlugin::GetCommonName() {
    return "AutoPilot";
}

int AutoPilotPlugin::GetToolbarToolCount() { return 1; }

// ---------------------------------------------------------------------------
// Toolbar
// ---------------------------------------------------------------------------

// Detect current dock position and rebuild the panel layout if it changed.
// Resizes the floating frame when switching back to float mode.
// Safe to call any time; SetDockMode() is a no-op if mode hasn't changed.
void AutoPilotPlugin::ApplyDockLayout() {
    if (!m_aui_mgr || !m_panel) return;
    wxAuiPaneInfo& pane = m_aui_mgr->GetPane("AutoPilot");

    DockMode mode;
    if (pane.IsFloating()) {
        mode = DockMode::FLOAT;
    } else if (pane.dock_direction == wxAUI_DOCK_LEFT ||
               pane.dock_direction == wxAUI_DOCK_RIGHT) {
        mode = DockMode::RIGHT;
    } else {
        mode = DockMode::TOP_BOTTOM;
    }

    bool rebuilt = m_panel->SetDockMode(mode);
    if (!rebuilt) return;

    if (mode == DockMode::FLOAT) {
        wxWindow* frame = m_panel->GetParent();
        while (frame && !frame->IsTopLevel())
            frame = frame->GetParent();
        if (frame)
            frame->SetClientSize(m_content_size);
    } else if (mode == DockMode::RIGHT) {
        // Push the narrow width onto the pane so AUI actually resizes the dock.
        // Only done when layout was just rebuilt (rebuilt==true) to avoid looping.
        const int w = AutoPilotPanel::kRightDockW;
        pane.BestSize(w, -1).MinSize(w, -1);
        m_aui_mgr->Update();
    } else if (mode == DockMode::TOP_BOTTOM) {
        // Use the virtual size measured by FitInside() — exact content height.
        const int h = m_panel->GetVirtualSize().y;
        pane.BestSize(-1, h).MinSize(-1, h);
        m_aui_mgr->Update();
    }
}

void AutoPilotPlugin::OnToolbarToolCallback(int id) {
    if (id != m_toolbar_id || !m_aui_mgr) return;
    m_panel_shown = !m_panel_shown;
    wxAuiPaneInfo& pane = m_aui_mgr->GetPane("AutoPilot");

    pane.Show(m_panel_shown);
    m_aui_mgr->Update();

    // Always sync layout when the panel becomes visible.  UpdateAuiStatus is
    // not guaranteed to fire at startup after the perspective is restored, so
    // we check here as well.
    if (m_panel_shown)
        ApplyDockLayout();
}

// Fires after every wxAuiManager layout update (including drag-to-dock).
// Defer the rebuild with CallAfter so we don't destroy children during a paint.
void AutoPilotPlugin::OnAuiRender(wxAuiManagerEvent& evt) {
    evt.Skip();
    if (m_panel && m_panel_shown)
        m_panel->CallAfter([this]() { ApplyDockLayout(); });
}

// Called by OpenCPN when the AUI layout changes (panel docked/undocked/closed).
void AutoPilotPlugin::UpdateAuiStatus() {
    if (!m_aui_mgr || !m_panel) return;
    m_panel_shown = m_aui_mgr->GetPane("AutoPilot").IsShown();
    if (m_panel_shown)
        ApplyDockLayout();
}

// ---------------------------------------------------------------------------
// Phase 4: active waypoint → Navigate button
//
// OpenCPN calls SetActiveLegInfo whenever the active route leg changes.
// Plugin_Active_Leg_Info carries Btw/Dtw/wp_name but NOT lat/lon.
// We resolve lat/lon directly via GetActiveWaypointGUID() + GetSingleWaypoint()
// — confirmed available in ocpn_plugin.h at Release_5.14.0.
// ---------------------------------------------------------------------------

void AutoPilotPlugin::SetActiveLegInfo(Plugin_Active_Leg_Info& leg_info) {
    wxLogMessage("AutoPilot: SetActiveLegInfo called, wp_name='%s' Btw=%.1f Dtw=%.2f",
                 leg_info.wp_name, leg_info.Btw, leg_info.Dtw);
    if (!m_panel) return;

    // Try 1: direct active-waypoint GUID (works for standalone marks)
    wxString guid = GetActiveWaypointGUID();
    wxLogMessage("AutoPilot: GetActiveWaypointGUID='%s'", guid);
    if (!guid.IsEmpty()) {
        PlugIn_Waypoint wp;
        if (GetSingleWaypoint(guid, &wp)) {
            wxLogMessage("AutoPilot: found via GUID lat=%.6f lon=%.6f", wp.m_lat, wp.m_lon);
            m_panel->SetNavigateTarget(true, wp.m_lat, wp.m_lon);
            return;
        }
    }

    // Try 2: active route GUID → scan waypoints for the one matching wp_name.
    wxString routeGUID = GetActiveRouteGUID();
    wxLogMessage("AutoPilot: GetActiveRouteGUID='%s'", routeGUID);
    if (!routeGUID.IsEmpty()) {
        auto route = GetRoute_Plugin(routeGUID);
        if (route && route->pWaypointList) {
            Plugin_WaypointList::Node* node = route->pWaypointList->GetFirst();
            while (node) {
                PlugIn_Waypoint* wp = node->GetData();
                wxLogMessage("AutoPilot: route wp name='%s' lat=%.6f lon=%.6f",
                             wp ? wp->m_MarkName : "null", wp ? wp->m_lat : 0, wp ? wp->m_lon : 0);
                if (wp && wp->m_MarkName == leg_info.wp_name) {
                    m_panel->SetNavigateTarget(true, wp->m_lat, wp->m_lon);
                    return;
                }
                node = node->GetNext();
            }
        }
    }

    wxLogMessage("AutoPilot: SetNavigateTarget(false)");
    m_panel->SetNavigateTarget(false);
}

// ---------------------------------------------------------------------------
// Plugin messaging — not currently used (active waypoint comes from the
// direct API in SetActiveLegInfo above). Kept for future extensibility.
// ---------------------------------------------------------------------------

void AutoPilotPlugin::SetPluginMessage(wxString& /*message_id*/,
                                       wxString& /*message_body*/) {}
