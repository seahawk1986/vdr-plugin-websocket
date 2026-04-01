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
#include "events.hpp"
#include "mongoose/mongoose.h"

using json = nlohmann::json;

class cWebsocketThread : public cThread
{
private:
    std::atomic<bool> clearPending{false};
    // system and networdk
    EventQueue &queue;
    int port;
    std::string logoDir;
    struct mg_mgr mgr;

    // logo cache and updates
    std::map<std::string, std::string> logoCache;
    std::mutex cacheMutex;

    // osd status and flags
    std::chrono::steady_clock::time_point lastOsdActivity;
    std::chrono::steady_clock::time_point lastQueueActivity;
    std::chrono::steady_clock::time_point lastListSent;
    std::atomic<bool> osdChanged{false};
    std::atomic<bool> osdItemsChanged{false};
    std::atomic<bool> osdHelpChanged{false};
    std::atomic<bool> focusChanged{false};
    int currentFocusIndex{-1};

    // osd cache (data)
    std::string osdTitle;
    std::vector<std::string> osdItems;
    std::string osdHelp[4]; // red, green, yellow, blue

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
    cWebsocketThread(EventQueue &q, int p, std::string ld);
    virtual ~cWebsocketThread();

    void SendInitialState(struct mg_connection *c);
    const std::string &getLogoDir() const { return logoDir; }
    void ReloadLogos() { UpdateLogoCache(); }
    int GetClientCount();
    void StopThread();
};
