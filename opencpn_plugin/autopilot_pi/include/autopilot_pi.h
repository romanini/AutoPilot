#pragma once

#include "ocpn_plugin.h"
#include "AutoPilotLink.h"
#include "AutoPilotPanel.h"

#ifndef DECL_EXP
#ifdef __WXMSW__
#define DECL_EXP __declspec(dllexport)
#else
#define DECL_EXP
#endif
#endif

class AutoPilotPlugin : public opencpn_plugin_117 {
public:
    explicit AutoPilotPlugin(void* ppimgr);
    ~AutoPilotPlugin() override;

    int     Init() override;
    bool    DeInit() override;

    int     GetAPIVersionMajor() override;
    int     GetAPIVersionMinor() override;
    int     GetPlugInVersionMajor() override;
    int     GetPlugInVersionMinor() override;
    wxString GetShortDescription() override;
    wxString GetLongDescription() override;
    wxString GetCommonName() override;

    int     GetToolbarToolCount() override;
    void    OnToolbarToolCallback(int id) override;
    void    UpdateAuiStatus() override;

    // opencpn_plugin_117 additions
    void SetActiveLegInfo(Plugin_Active_Leg_Info& leg_info) override;

    // opencpn_plugin_16 addition (WANTS_PLUGIN_MESSAGING)
    void SetPluginMessage(wxString& message_id, wxString& message_body) override;

private:
    AutoPilotLink*  m_link;
    AutoPilotPanel* m_panel;
    wxAuiManager*   m_aui_mgr;
    int             m_toolbar_id;
    bool            m_panel_shown;
    wxSize          m_content_size;  // panel virtual size measured once at Init()
};
