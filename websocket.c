/*
 * websocket.c: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#include <vdr/channels.h>
#include <vdr/epg.h>
#include <vdr/player.h>
#include <vdr/plugin.h>
#include <vdr/status.h>
#include <vdr/thread.h>
#include <algorithm>
#include <getopt.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include "events.hpp"
#include "mongoose/mongoose.h"

static const char *VERSION = "0.0.1";
static const char *DESCRIPTION = "Send VDR status via websocket";
static const char *MAINMENUENTRY = "Websocket";

class cWebsocketStatusMonitor : public cStatus
{
private:
  EventQueue & queue;
protected:
  void SendInitialState(struct mg_connection * c, const DeviceEvent &ev);
  virtual void ChannelSwitch(const cDevice *Device, int ChannelNumber, bool LiveView) override
  {
    if (LiveView && Device && Device->IsPrimaryDevice())
    {
      LOCK_CHANNELS_READ;
      const cChannel *channel = Channels->GetByNumber(ChannelNumber);
      if (channel)
      {
        isyslog("websocket-plugin: Kanal gewechselt auf %s", channel->Name());
        queue.push(DeviceEvent(eEventType::ChannelChange, channel->Name(), "", channel->Number()));
      }
    }
  }

  virtual void Replaying(const cControl *Control, const char *Name, const char *FileName, bool On) override
  {
    if (On && Name)
    {
      isyslog("websocket-plugin: Wiedergabe gestartet: %s", Name);
      queue.push({eEventType::ReplayStart, Name, FileName ? FileName : "", 0});
    }
    else
    {
      // Wenn On == false oder Name == NULL, wurde die Wiedergabe beendet
      isyslog("websocket-plugin: Wiedergabe beendet");
      queue.push({eEventType::ReplayStop, "", "", 0});
    }
  }

  void Recording(const cDevice *Device, const char *Name, const char *FileName, bool On) override
  {
    if (On)
      isyslog("websocket-plugin: Eine Aufnahme hat im Hintergrund gestartet: %s", Name);
  }

public:
  virtual ~cWebsocketStatusMonitor() { isyslog("MONITOR DESTRUKTOR GESTARTET"); }
  explicit cWebsocketStatusMonitor(EventQueue & q) : cStatus(), queue(q)
  {
    isyslog("websocket-plugin: Monitor beim VDR registriert");
  }
};

class cWebsocketThread : public cThread
{
private:
  EventQueue & queue;
  int port;
  struct mg_mgr mgr;

  static void on_connect_callback(struct mg_connection * c, int ev, void *ev_data)
  {
    if (ev == MG_EV_HTTP_MSG)
    {
      struct mg_http_message *hm = (struct mg_http_message *)ev_data;

      // Hier .buf statt .ptr nutzen!
      isyslog("websocket-plugin: Request auf URI: %.*s", (int)hm->uri.len, hm->uri.buf);

      if (mg_match(hm->uri, mg_str("/#"), NULL))
      {
        mg_ws_upgrade(c, hm, NULL);
      }
    }
    else if (ev == MG_EV_WS_OPEN)
    {
      isyslog("websocket-plugin: Client via WS verbunden");

      auto *self = static_cast<cWebsocketThread *>(c->mgr->userdata);
      if (self)
      {
        self->SendInitialState(c); // Wir rufen die Version mit 1 Argument auf
      }
    }
  }

protected:
  void SendInitialState(struct mg_connection * c)
  {
    // Aktuellen Kanal ermitteln
    LOCK_CHANNELS_READ;
    const cChannel *channel = Channels->GetByNumber(cDevice::PrimaryDevice()->CurrentChannel());

    if (channel)
    {
      // Wir nutzen einfach die vorhandene Broadcast-Logik,
      // indem wir ein lokales Event-Objekt bauen.
      DeviceEvent ev(eEventType::ChannelChange, channel->Name(), "", channel->Number());

      // Hier bauen wir das JSON (wie in BroadcastJson)
      json j;
      j["type"] = "channel";
      j["name"] = ev.name;
      j["number"] = ev.number;

      // EPG Logik (LOCK_SCHEDULES_READ etc.) hier einfügen...

      std::string s = j.dump();
      mg_ws_send(c, s.c_str(), s.size(), WEBSOCKET_OP_TEXT);
    }
  }

  void BroadcastJson(const DeviceEvent &ev)
  {
    json j;
    j["type"] = (ev.type == eEventType::ChannelChange) ? "channel" : "replay";
    j["name"] = ev.name;

    if (ev.type == eEventType::ChannelChange)
    {
      j["number"] = ev.number;
      LOCK_CHANNELS_READ;
      const cChannel *channel = Channels->GetByNumber(ev.number);
      if (channel)
      {
        LOCK_SCHEDULES_READ;
        const cSchedule *schedule = Schedules->GetSchedule(channel);
        if (schedule)
        {
          const cEvent *present = schedule->GetPresentEvent();
          if (present)
          {
            j["epg"]["present"]["title"] = present->Title();
            j["epg"]["present"]["short_text"] = (present->ShortText() && *present->ShortText()) ? present->ShortText() : "";
            j["epg"]["present"]["start"] = present->StartTime();
            j["epg"]["present"]["duration"] = present->Duration();
          }
        }
      }
    }
    else if (ev.type == eEventType::ReplayStart)
    {
      j["status"] = "started";
      LOCK_RECORDINGS_READ;
      const cRecording *recording = Recordings->GetByName(ev.fileName.c_str());
      if (recording)
      {
        const cRecordingInfo *info = recording->Info();
        j["recording"]["title"] = recording->Name();
        j["recording"]["short_text"] = (info->ShortText() && *info->ShortText()) ? info->ShortText() : "";
        j["recording"]["description"] = (info->Description() && *info->Description()) ? info->Description() : "";
        j["recording"]["duration"] = recording->LengthInSeconds();
      }
      else
      {
        // Falls cRecording nicht gefunden wurde, nutzen wir den Namen aus dem Event
        j["recording"]["title"] = ev.name;
      }
    }
    else if (ev.type == eEventType::ReplayStop)
    {
      j["status"] = "stopped";
    }

    std::string s = j.dump();
    for (struct mg_connection *c = mgr.conns; c != NULL; c = c->next)
    {
      if (c->is_websocket)
      {
        mg_ws_send(c, s.c_str(), s.size(), WEBSOCKET_OP_TEXT);
      }
    }
  }
  virtual void Action() override
  {
    mg_mgr_init(&mgr);
    std::string url = "ws://0.0.0.0:" + std::to_string(port);

    mgr.userdata = this;
    struct mg_connection *c = mg_http_listen(&mgr, url.c_str(), on_connect_callback, NULL);

    if (c == NULL)
    {
      isyslog("error - is port %d already in use?", port);
      return;
    }

    isyslog("websocket-plugin: Server erfolgreich gestartet auf %s", url.c_str());

    auto lastPosUpdate = std::chrono::steady_clock::now();
    bool isReplaying = false;

    while (Running())
    {
      // poll shortly for mogoose events (about 10ms)
      mg_mgr_poll(&mgr, 10);

      // get vdr events from queue if available within 50ms
      auto optEv = queue.pop_with_timeout(40);

      if (optEv)
      {
        isyslog("websocket-plugin: Event empfangen, Typ: %d", (int)optEv->type);
        DeviceEvent ev = *optEv;

        // first check if we need to shut down
        if (ev.type == eEventType::PluginStop)
        {
          break;
        }

        switch (ev.type)
        {
        case eEventType::ReplayStart:
          isReplaying = true;
          break;
        case eEventType::ReplayStop:
          isReplaying = false;
          break;
        case eEventType::ChannelChange:
          isReplaying = false; // Stop on channel change?
          break;
        default:
          break;
        }
        BroadcastJson(*optEv);
      }

      if (isReplaying)
      {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPosUpdate).count() >= 1000)
        {
          int current = 0, total = 0;
          double fps = 25.0; // Fallback
          bool hasData = false;

          {
            // LOCK START
            cMutexLock ControlMutexLock;
            cControl *control = cControl::Control(ControlMutexLock);
            if (control && control->GetIndex(current, total))
            {
              fps = control->FramesPerSecond();
              // Falls FPS vom VDR 0 oder ungültig geliefert wird (z.B. am Anfang)
              if (fps <= 0)
                fps = 25.0;
              hasData = true;
            }
          } // LOCK ENDE (Hier wird der VDR-Hauptthread sofort wieder freigegeben)

          if (hasData)
          {
            json jpos;
            jpos["type"] = "pos";
            jpos["current"] = (int)(current / fps);
            jpos["total"] = (int)(total / fps);

            std::string s = jpos.dump();
            for (struct mg_connection *c = mgr.conns; c != NULL; c = c->next)
            {
              if (c->is_websocket)
                mg_ws_send(c, s.c_str(), s.size(), WEBSOCKET_OP_TEXT);
            }
          }
          lastPosUpdate = now;
        }
      }
    }
    mg_mgr_free(&mgr);
    isyslog("websocket-plugin: Thread sauber beendet.");
  }

public:
  cWebsocketThread(EventQueue & q, int p)
      : cThread("websocket-worker"), queue(q), port(p)
  {
  }
  void StopThread()
  {
    Cancel();
  }
};

class cPluginWebsocket : public cPlugin
{
private:
  EventQueue eventQueue;
  std::unique_ptr<cWebsocketStatusMonitor> statusMonitor;
  int port{6742};
  std::unique_ptr<cWebsocketThread> workerThread;
public:
  cPluginWebsocket() = default;
  virtual ~cPluginWebsocket() override = default;
  virtual const char *Version(void) override { return VERSION; }
  virtual const char *Description(void) override { return DESCRIPTION; }
  virtual const char *CommandLineHelp(void) override;
  virtual bool ProcessArgs(int argc, char *argv[]) override;
  virtual bool Initialize(void) override;
  virtual bool Start(void) override;
  virtual void Stop() override;

  virtual void Housekeeping(void) override;
  virtual cString Active(void) override;
  virtual time_t WakeupTime(void) override;
  virtual const char *MainMenuEntry(void) override { return MAINMENUENTRY; }
  virtual cOsdObject *MainMenuAction(void) override;
  virtual cMenuSetupPage *SetupMenu(void) override;
  virtual bool SetupParse(const char *Name, const char *Value) override;
  virtual bool Service(const char *Id, void *Data = NULL) override;
  virtual const char **SVDRPHelpPages(void) override;
  virtual cString SVDRPCommand(const char *Command, const char *Option, int &ReplyCode) override;
};

const char *cPluginWebsocket::CommandLineHelp(void)
{
  static std::string helpText =
      std::string("  -p PORT,  --port=PORT    Port für den WebSocket-Server (Standard: ") + std::to_string(port) + ")\n";

  return helpText.c_str();
}

bool cPluginWebsocket::ProcessArgs(int argc, char *argv[])
{
  static const struct option long_options[] = {
      {"port", required_argument, nullptr, 'p'},
      {nullptr, 0, nullptr, 0}};

  int c;
  while ((c = getopt_long(argc, argv, "p:", long_options, nullptr)) != -1)
  {
    switch (c)
    {
    case 'p':
      port = std::atoi(optarg);
      if (port <= 0 || port > 65535)
      {
        fprintf(stderr, "websocket-plugin: Ungültiger Port '%s'\n", optarg);
        return false;
      }
      break;
    default:
      return false;
    }
  }
  return true;
}

bool cPluginWebsocket::Initialize(void)
{
  // Initialize any background activities the plugin shall perform.
  return true;
}

bool cPluginWebsocket::Start(void)
{
  isyslog("websocket-plugin: Starte Server auf Port %d", port);
  workerThread = std::make_unique<cWebsocketThread>(eventQueue, port);
  workerThread->Start();
  statusMonitor = std::make_unique<cWebsocketStatusMonitor>(eventQueue);
  isyslog("websocket-plugin: Raw-Pointer Test gestartet");
  return true;
}

void cPluginWebsocket::Stop(void)
{
  statusMonitor.reset();
  if (workerThread)
  {
    workerThread->StopThread();

    eventQueue.push({eEventType::PluginStop, "SHUTDOWN", 0});

    workerThread.reset(); // Wartet via Destruktor auf das Thread-Ende
  }
}

void cPluginWebsocket::Housekeeping(void)
{
  // Perform any cleanup or other regular tasks.
}

cString cPluginWebsocket::Active(void)
{
  // Return a message string if shutdown should be postponed
  return NULL;
}

time_t cPluginWebsocket::WakeupTime(void)
{
  // Return custom wakeup time for shutdown script
  return 0;
}

cOsdObject *cPluginWebsocket::MainMenuAction(void)
{
  // Perform the action when selected from the main VDR menu.
  return NULL;
}

cMenuSetupPage *cPluginWebsocket::SetupMenu(void)
{
  // Return a setup menu in case the plugin supports one.
  return NULL;
}

bool cPluginWebsocket::SetupParse(const char *Name, const char *Value)
{
  // Parse your own setup parameters and store their values.
  return false;
}

bool cPluginWebsocket::Service(const char *Id, void *Data)
{
  // Handle custom service requests from other plugins
  return false;
}

const char **cPluginWebsocket::SVDRPHelpPages(void)
{
  // Return help text for SVDRP commands this plugin implements
  return NULL;
}

cString cPluginWebsocket::SVDRPCommand(const char *Command, const char *Option, int &ReplyCode)
{
  // Process SVDRP commands this plugin implements
  return NULL;
};

VDRPLUGINCREATOR(cPluginWebsocket); // Don't touch this!
