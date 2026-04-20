#pragma once

#include <vdr/plugin.h>
#include <vdr/menuitems.h>
#include "config.hpp"

#ifdef PLUGIN_NAME_I18N
#undef tr
#define tr(s) I18nTranslate(s, "vdr-" PLUGIN_NAME_I18N)
#else
#define PLUGIN_NAME_I18N "websocket"
#endif

class cWebsocketSetupPage : public cMenuSetupPage
{
private:
    int tempShowDebug;

public:
    cWebsocketSetupPage(void)
    {
        tempShowDebug = WebsocketConfig.ShowDebug;
        Add(new cMenuEditBoolItem(tr("Show Debug"), &tempShowDebug));
    }

    virtual void Store(void)
    {
        WebsocketConfig.ShowDebug = tempShowDebug;
        cPlugin *p = cPluginManager::GetPlugin(PLUGIN_NAME_I18N);
        if (p)
        {
            p->SetupStore("ShowDebug", WebsocketConfig.ShowDebug);
        }
    }
};