#pragma once

#include <vdr/thread.h>
#include <vdr/channels.h>
#include <vdr/epg.h>
#include <vdr/player.h>
#include <vdr/recording.h>
#include <nlohmann/json.hpp>
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
    std::map<std::string, std::string> logoCache; // Key: Kleingeschriebener Name/ID, Value: Echter Dateiname
    std::mutex cacheMutex;
    void UpdateLogoCache();
    std::string toLower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c)
                       { return std::tolower(c); });
        return s;
    }
    EventQueue &queue;
    int port;
    std::string logoDir;
    struct mg_mgr mgr;
    std::string GetLogoPath(const cChannel *channel);
    void BroadcastJson(const DeviceEvent &ev);
    void BroadcastTimerStatus(void);
    json BuildStatusJson(const DeviceEvent &ev);
    std::string FindLogoInCache(const std::string &request);
    static void on_connect_callback(struct mg_connection *c, int ev, void *ev_data);

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
