#pragma once

#include <vdr/thread.h>
#include <vdr/channels.h>
#include <vdr/epg.h>
#include <vdr/menu.h>
#include <vdr/player.h>
#include <vdr/recording.h>
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include "mongoose/mongoose.h"
#include "events.hpp"
#include "osdState.hpp"
#include "statusmonitor.hpp"

using json = nlohmann::json;

class cWebsocketThread : public cThread
{
private:
    std::atomic<bool> clearPending{false};
    // system and networdk
    EventQueue &queue;
    cWebsocketStatusMonitor *statusMonitor;
    int port;
    std::string logoDir;
    std::string webDir;
    struct mg_mgr mgr;

    // logo cache and updates
    std::map<std::string, std::string> logoCache;
    std::mutex cacheMutex;

    std::atomic<bool> isReplaying{false};

    // helper methods
    std::string toLower(std::string s);
    void UpdateLogoCache();
    std::string GetLogoPath(const cChannel *channel);
    void BroadcastJson(const DeviceEvent &ev);
    void BroadcastJson(const nlohmann::json &j);
    void BroadcastTimerStatus(void);
    json BuildStatusJson(const DeviceEvent &ev);
    std::string FindLogoInCache(const std::string &request);
    static void on_connect_callback(struct mg_connection *c, int ev, void *ev_data);
    void processEvent(const DeviceEvent &ev);

protected:
    virtual void Action() override;

public:
    cWebsocketThread(EventQueue &q, cWebsocketStatusMonitor *sm, int p, std::string ld, std::string wd);
    virtual ~cWebsocketThread();

    void SendInitialState(struct mg_connection *c);
    const std::string &getLogoDir() const { return logoDir; }
    void ReloadLogos() { UpdateLogoCache(); }
    int GetClientCount();
    void StopThread();
};
