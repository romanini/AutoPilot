#include "wx/wxprec.h"
#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif

#include "autopilot_pi.h"
#include "version.h"

#include <wx/aui/aui.h>

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

int AutoPilotPlugin::Init() {
    m_aui_mgr = GetFrameAuiManager();

    // Create link first (panel pointer set after panel construction).
    m_link = new AutoPilotLink(nullptr);

    // Create panel parented to the chart canvas.
    m_panel = new AutoPilotPanel(GetOCPNCanvasWindow(), m_link);
    m_link->SetPanel(m_panel);

    // Register the dockable AUI pane.
    wxAuiPaneInfo pane;
    pane.Name("AutoPilot")
        .Caption("AutoPilot")
        .Float()
        .FloatingSize(wxSize(240, 460))
        .CloseButton(true)
        .MinSize(wxSize(200, 340))
        .Hide();  // hidden until toolbar button is pressed
    m_aui_mgr->AddPane(m_panel, pane);
    m_aui_mgr->Update();

    // Toolbar button: a plain text label (no icon yet).
    m_toolbar_id = InsertPlugInTool(
        "AP", nullptr, nullptr,
        wxITEM_NORMAL,
        "AutoPilot",
        "Toggle AutoPilot status panel",
        nullptr, -1, 0, this);

    m_link->Start();

    return WANTS_TOOLBAR_CALLBACK
         | INSTALLS_TOOLBAR_TOOL
         | USES_AUI_MANAGER
         | WANTS_PLUGIN_MESSAGING;
}

bool AutoPilotPlugin::DeInit() {
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
int     AutoPilotPlugin::GetPluginVersionMajor() { return PLUGIN_VERSION_MAJOR; }
int     AutoPilotPlugin::GetPluginVersionMinor() { return PLUGIN_VERSION_MINOR; }

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

void AutoPilotPlugin::OnToolbarToolCallback(int id) {
    if (id != m_toolbar_id || !m_aui_mgr) return;
    m_panel_shown = !m_panel_shown;
    wxAuiPaneInfo& pane = m_aui_mgr->GetPane("AutoPilot");
    pane.Show(m_panel_shown);
    m_aui_mgr->Update();
}

// Called by OpenCPN when the AUI layout changes (e.g. panel dragged/closed).
void AutoPilotPlugin::UpdateAuiStatus() {
    if (m_aui_mgr) {
        m_panel_shown = m_aui_mgr->GetPane("AutoPilot").IsShown();
    }
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
    if (!m_panel) return;

    m_panel->SetActiveLeg(leg_info.Btw, leg_info.Dtw, leg_info.wp_name);

    wxString guid = GetActiveWaypointGUID();
    if (guid.IsEmpty()) {
        m_panel->SetNavigateTarget(false);
        return;
    }

    PlugIn_Waypoint wp;
    if (GetSingleWaypoint(guid, &wp)) {
        m_panel->SetNavigateTarget(true, wp.m_lat, wp.m_lon);
    } else {
        m_panel->SetNavigateTarget(false);
    }
}

// ---------------------------------------------------------------------------
// Plugin messaging — not currently used (active waypoint comes from the
// direct API in SetActiveLegInfo above). Kept for future extensibility.
// ---------------------------------------------------------------------------

void AutoPilotPlugin::SetPluginMessage(wxString& /*message_id*/,
                                       wxString& /*message_body*/) {}
