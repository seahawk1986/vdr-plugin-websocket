#include "websocketthread.hpp"
#include <vdr/plugin.h>
#include <vdr/menu.h>

cWebsocketThread::cWebsocketThread(EventQueue &q, int p)
    : cThread("websocket-worker"), queue(q), port(p) {}

cWebsocketThread::~cWebsocketThread()
{
    StopThread();
}

void cWebsocketThread::StopThread()
{
    Cancel();
}

void cWebsocketThread::on_connect_callback(struct mg_connection *c, int ev, void *ev_data)
{
    if (ev == MG_EV_HTTP_MSG)
    {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;
        if (mg_match(hm->uri, mg_str("/#"), NULL))
        {
            mg_ws_upgrade(c, hm, NULL);
        }
    }
    else if (ev == MG_EV_WS_OPEN)
    {
        auto *self = static_cast<cWebsocketThread *>(c->mgr->userdata);
        if (self)
            self->SendInitialState(c);
    }
}

json cWebsocketThread::BuildStatusJson(const DeviceEvent &ev)
{
    json j;
    switch (ev.type)
    {
    case eEventType::OsdMessage:
        j["type"] = "osdmessage";
        j["message"] = ev.name;
        return j;
    case eEventType::ChannelChange:
    {
        j["type"] = "channel";
        j["number"] = ev.number;
        LOCK_CHANNELS_READ;
        const cChannel *channel = Channels->GetByNumber(ev.number);

        if (channel)
        {
            j["name"] = channel->Name();
            LOCK_SCHEDULES_READ;
            const cSchedule *schedule = Schedules->GetSchedule(channel);
            if (schedule)
            {
                auto setEpg = [&](const std::string &key, const cEvent *e)
                {
                    if (e)
                    {
                        int progress = 0;
                        int duration = e->Duration();

                        if (key == "present" && duration > 0)
                        {
                            time_t now = time(NULL);
                            int elapsed = (int)(now - e->StartTime());
                            progress = (elapsed * 100) / duration;

                            if (progress < 0)
                                progress = 0;
                            if (progress > 100)
                                progress = 100;
                        }

                        j["epg"][key] = {
                            {"title", e->Title() ? e->Title() : ""},
                            {"short_text", (e->ShortText() && *e->ShortText()) ? e->ShortText() : ""},
                            {"start", (long)e->StartTime()},
                            {"duration", duration},
                            {"progress", progress}};
                    }
                };

                setEpg("present", schedule->GetPresentEvent());
                setEpg("following", schedule->GetFollowingEvent());
            }
        }
        break;
    }
    case eEventType::ReplayStart:
    {
        if (!ev.fileName.empty())
        {
            j["name"] = ev.name;
            j["type"] = "replay";
            j["status"] = "started";
            LOCK_RECORDINGS_READ;
            const cRecording *recording = Recordings->GetByName(ev.fileName.c_str());
            if (recording)
            {
                const cRecordingInfo *info = recording->Info();
                j["recording"] = {
                    {"title", recording->Name()},
                    {"description", (info->Description() && *info->Description()) ? info->Description() : ""},
                    {"duration", recording->LengthInSeconds()}};
            }
        }
        break;
    }
    case eEventType::ReplayStop:
        j["type"] = "replay";
        j["status"] = "stopped";
        return j;

    case eEventType::VolumeChange:
        j["type"] = "volume";
        j["level"] = ev.number;
        return j;

    default:
        j["type"] = "unknown";
        break;
    }
    return j;
}

void cWebsocketThread::SendInitialState(struct mg_connection *c)
{
    json j;
    j["type"] = "initial_full_state";

    cDevice *primary = cDevice::PrimaryDevice();
    if (primary)
    {
        j["volume"] = cDevice::CurrentVolume();
    }

    LOCK_CHANNELS_READ;
    const cChannel *channel = Channels->GetByNumber(cDevice::PrimaryDevice()->CurrentChannel());
    if (channel)
    {
        DeviceEvent ev(eEventType::ChannelChange, channel->Name(), "", channel->Number());
        j["current_display"] = BuildStatusJson(ev); // Nutzt deine EPG-Logik
    }

    bool isAnythingRecording = false;
    int32_t nRecordings = 0;
    int32_t nTimers = 0;
    {
        LOCK_TIMERS_READ;
        for (const cTimer *timer = Timers->First(); timer; timer = Timers->Next(timer))
        {
            if (timer->Recording())
            {
                isAnythingRecording = true;
                nRecordings++;
            }
            nTimers++;
        }
    }
    j["is_recording"] = isAnythingRecording;
    j["active_recordings"] = nRecordings;
    j["n_timer"] = nTimers;

    {
        cMutexLock ControlMutexLock;
        cControl *control = cControl::Control(ControlMutexLock);

        if (control)
        {
            j["replaying"] = true;

            // Wir versuchen den Pfad der Aufnahme zu bekommen, damit BuildStatusJson
            // die .info Datei lesen kann.
            const char *fileName = nullptr;
            cReplayControl *replayControl = dynamic_cast<cReplayControl *>(control);
            if (replayControl)
            {
                const cRecording *recording = replayControl->GetRecording();
                if (recording)
                {
                    fileName = recording->FileName();
                }
            }
            std::string name = "Wiedergabe";
            std::string fName = "";

            if (fileName)
            {
                fName = fileName;
                cRecording rec(fileName);
                name = rec.Name();
            }

            DeviceEvent ev(eEventType::ReplayStart, name, fName, 0);
            j["current_display"] = BuildStatusJson(ev);
            int current = 0, total = 0;
            if (control->GetIndex(current, total))
            {
                double fps = control->FramesPerSecond();
                if (fps <= 0)
                    fps = 25.0; // Fallback

                j["current_display"]["recording"]["current"] = (int)(current / fps);
                j["current_display"]["recording"]["total"] = (int)(total / fps);
                j["current_display"]["recording"]["progress"] = (total > 0) ? (current * 100 / total) : 0;
            }
        }
        else
        {
            j["replaying"] = false;
            LOCK_CHANNELS_READ;
            const cChannel *channel = Channels->GetByNumber(cDevice::PrimaryDevice()->CurrentChannel());
            if (channel)
            {
                DeviceEvent ev(eEventType::ChannelChange, channel->Name(), "", channel->Number());
                j["current_display"] = BuildStatusJson(ev);
            }
        }
    }

    std::string s = j.dump();
    mg_ws_send(c, s.c_str(), s.size(), WEBSOCKET_OP_TEXT);
}

void cWebsocketThread::BroadcastJson(const DeviceEvent &ev)
{
    std::string s = BuildStatusJson(ev).dump();
    for (struct mg_connection *c = mgr.conns; c != NULL; c = c->next)
    {
        if (c->is_websocket)
            mg_ws_send(c, s.c_str(), s.size(), WEBSOCKET_OP_TEXT);
    }
}

void cWebsocketThread::Action()
{
    mg_mgr_init(&mgr);
    mgr.userdata = this;
    std::string url = "ws://0.0.0.0:" + std::to_string(port);
    if (!mg_http_listen(&mgr, url.c_str(), on_connect_callback, NULL))
        return;

    bool isReplaying = false;
    auto lastPosUpdate = std::chrono::steady_clock::now();
    auto lastEpgUpdate = std::chrono::steady_clock::now();

    while (Running())
    {
        mg_mgr_poll(&mgr, 10);
        auto optEv = queue.pop_with_timeout(40);

        if (optEv)
        {
            if (optEv->type == eEventType::PluginStop)
                break;
            if (optEv->type == eEventType::ReplayStart)
                isReplaying = true;
            else if (optEv->type == eEventType::ReplayStop || optEv->type == eEventType::ChannelChange)
                isReplaying = false;
            BroadcastJson(*optEv);
        }

        auto now = std::chrono::steady_clock::now();

        if (isReplaying)
        {
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPosUpdate).count() >= 1000)
            {
                int current = 0, total = 0;
                bool play = true;
                bool forward = true;
                int speed = 0;
                bool hasData = false;

                {
                    cMutexLock ControlMutexLock;
                    cControl *control = cControl::Control(ControlMutexLock);
                    if (control && control->GetIndex(current, total))
                    {
                        // Abfrage des Wiedergabemodus
                        // Play: true = läuft, false = Pause
                        // Speed: >0 vorwärts, <0 rückwärts, 0 normal
                        control->GetReplayMode(play, forward, speed);
                        hasData = true;
                    }
                }

                if (hasData)
                {
                    json jpos;
                    jpos["type"] = "pos";
                    jpos["current"] = current / 25; // Vereinfacht 25fps
                    jpos["total"] = total / 25;
                    jpos["play"] = play; // true/false
                    jpos["forward"] = forward;
                    jpos["speed"] = speed; // Geschwindigkeit (0, 1, 2...)

                    std::string s = jpos.dump();
                    for (struct mg_connection *c = mgr.conns; c != NULL; c = c->next)
                        if (c->is_websocket)
                            mg_ws_send(c, s.c_str(), s.size(), WEBSOCKET_OP_TEXT);
                }
                lastPosUpdate = now;
            }
        }
        else
        {
            if (std::chrono::duration_cast<std::chrono::seconds>(now - lastEpgUpdate).count() >= 60)
            {
                LOCK_CHANNELS_READ;
                const cDevice *primary = cDevice::PrimaryDevice();
                if (primary)
                {
                    const cChannel *channel = Channels->GetByNumber(primary->CurrentChannel());
                    if (channel)
                    {
                        // Wir fingieren ein ChannelChange Event, um BuildStatusJson zu triggern
                        DeviceEvent ev(eEventType::ChannelChange, channel->Name(), "", channel->Number());
                        BroadcastJson(ev);
                        isyslog("websocket-plugin: Automatisches EPG-Update gesendet");
                    }
                }
                lastEpgUpdate = now;
            }
        }
    }
    mg_mgr_free(&mgr);
}
