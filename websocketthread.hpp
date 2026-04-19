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
#include "common.hpp"
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

    json collectTimers(const cTimers *Timers)
    {
        json timersJsonArray = json::array();
        for (int i = 0; i < Timers->Count(); i++)
        {
            json timerJson = json::object();
            const cTimer *timer = Timers->Get(i);
            if (timer)
            {
                timerJson["raw"] = timer->ToText(true);
                auto event = timer->Event();
                if (event)
                {
                    timerJson["title"] = safeStr(event->Title());
                }
                const cChannel *channel = timer->Channel();
                if (channel)
                {
                    auto id = channel->GetChannelID();
                    timerJson["channel_id"] = safeStr(id.ToString());
                }
                timerJson["start"] = timer->StartTime();
                timerJson["index"] = timer->Index();
                timerJson["stop"] = timer->StopTime();
                timerJson["aux"] = safeStr(timer->Aux());
            }
            timersJsonArray.push_back(timerJson);
        }
        return timersJsonArray;
    }

    json collectRecording(const cRecording *recording)
    {
        json j = json::object();
        if (recording)
        {
            const cRecordingInfo *info = recording->Info();
            if (info)
            {

                Debug("building recording json");
                j = {
                    {"index", recording->Id()},
                    {"title", safeStr(info->Title())},
                    {"subtitle", safeStr(info->ShortText())},
                    {"name", safeStr(recording->Name())},
                    {"description", safeStr(info->Description())},
                    {"start", recording->Start()},
                    {"duration", recording->LengthInSeconds()},
                    {"fps", info->FramesPerSecond()},
                    {"sizeMB", recording->FileSizeMB()},
                    {"is_new", recording->IsNew()},
                    {"is_edited", recording->IsEdited()},
                    {"components", json::object()},
                };
                Debug("building component json");

                // LOCK_THREAD;
                const cComponents *components = info->Components();
                if ((components) && (components->NumComponents() > 0))
                {
                    for (int comp = 0; comp < components->NumComponents(); comp++)
                    {
                        tComponent *component = components->Component(comp);
                        if (component)
                        {
                            std::string key = std::to_string(comp);
                            json cObj = json::object();
                            cObj["stream"] = (int)component->stream;
                            cObj["type"] = (int)component->type;

                            if (component->language[0] != '\0')
                                cObj["language"] = std::string(component->language, strnlen(component->language, 4));

                            if (component->description && component->description[0] != '\0')
                            {
                                try
                                {
                                    cObj["description"] = std::string(component->description);
                                }
                                catch (...)
                                {
                                    Debug("String error in component %d", comp);
                                    cObj["description"] = "encoding error";
                                }
                            }

                            j["components"][key] = cObj;
                        }
                    }
                }
            }
        }
        return j;
    };

    json collectRecordings(const cRecordings *Recordings)
    {
        json recordingsJsonArray = json::array();
        for (const cRecording *recording = Recordings->First(); recording; recording = Recordings->Next(recording))
        {
            json j = collectRecording(recording);
            recordingsJsonArray.push_back(j);
        }
        return recordingsJsonArray;
    };

protected:
    virtual void
    Action() override;

public:
    cWebsocketThread(EventQueue &q, cWebsocketStatusMonitor *sm, int p, std::string ld, std::string wd);
    virtual ~cWebsocketThread();

    void SendInitialState(struct mg_connection *c);
    const std::string &getLogoDir() const { return logoDir; }
    void ReloadLogos() { UpdateLogoCache(); }
    int GetClientCount();
    void StopThread();
};
