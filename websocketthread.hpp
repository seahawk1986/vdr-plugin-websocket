#pragma once

#include <vdr/thread.h>
#include <vdr/channels.h>
#include <vdr/epg.h>
#include <vdr/player.h>
#include <vdr/recording.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include "events.hpp"
#include "mongoose/mongoose.h"

using json = nlohmann::json;

class cWebsocketThread : public cThread
{
private:
    EventQueue &queue;
    int port;
    struct mg_mgr mgr;

    void BroadcastJson(const DeviceEvent &ev);
    json BuildStatusJson(const DeviceEvent &ev);
    static void on_connect_callback(struct mg_connection *c, int ev, void *ev_data);

protected:
    virtual void Action() override;

public:
    cWebsocketThread(EventQueue &q, int p);
    virtual ~cWebsocketThread();

    void SendInitialState(struct mg_connection *c);
    void StopThread();
};
