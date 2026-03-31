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
#include <vdr/svdrp.h>
#include <vdr/thread.h>
#include <algorithm>
#include <getopt.h>

#include <nlohmann/json.hpp>
using nlohmann::json;
#include "mongoose/mongoose.h"

#include "events.hpp"
#include "statusmonitor.hpp"
#include "websocketthread.hpp"

static const char *VERSION = "0.0.1";
static const char *DESCRIPTION = "Send VDR status via websocket";
static const char *MAINMENUENTRY = "Websocket";

class cPluginWebsocket : public cPlugin
{
private:
  EventQueue eventQueue;
  std::unique_ptr<cWebsocketStatusMonitor> statusMonitor;
  int port{6742};
  std::string logoDir{"/var/lib/vdr/channellogos/"};
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
      std::string("  -p PORT,  --port=PORT    Port für den WebSocket-Server (Standard: ") + std::to_string(port) + ")\n" +
      "  -l DIR,   --logodir=DIR     Pfad zu den Kanallogos (Standard: " + logoDir + ")\n";

  return helpText.c_str();
}

bool cPluginWebsocket::ProcessArgs(int argc, char *argv[])
{
  static const struct option long_options[] = {
      {"port", required_argument, nullptr, 'p'},
      {"logodir", required_argument, nullptr, 'l'},
      {nullptr, 0, nullptr, 0}};

  int c;
  while ((c = getopt_long(argc, argv, "p:l:", long_options, nullptr)) != -1)
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
    case 'l':
      logoDir = optarg;
      if (logoDir.back() != '/')
        logoDir += '/'; // make sure the path ends with a slash
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
  dsyslog("websocket-plugin: Starting Server on port %d", port);
  workerThread = std::make_unique<cWebsocketThread>(eventQueue, port, logoDir);
  workerThread->Start();
  statusMonitor = std::make_unique<cWebsocketStatusMonitor>(eventQueue);
  return true;
}

void cPluginWebsocket::Stop(void)
{
  statusMonitor.reset();
  if (workerThread)
  {
    workerThread->StopThread();

    eventQueue.push({eEventType::PluginStop, "SHUTDOWN", 0});

    workerThread.reset(); // Wait for the end of thread using the destructor
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
  static const char *HelpPages[] = {
      "RELOAD",
      "    Reloads the channel logo cache from the configured directory.",
      "LIST",
      "    Shows the number of currently connected WebSocket clients.",
      NULL // This array has to end with a NULL
  };
  return HelpPages;
}

cString cPluginWebsocket::SVDRPCommand(const char *Command, const char *Option, int &ReplyCode)
{
  if (strcasecmp(Command, "RELOAD") == 0)
  {
    if (workerThread)
    {
      workerThread->ReloadLogos();
      ReplyCode = 900;
      return "Logo cache reloaded successfully";
    }
    ReplyCode = 554;
    return "Worker thread not running";
  }

  else if (strcasecmp(Command, "LIST") == 0)
  {
    if (workerThread)
    {
      int count = workerThread->GetClientCount(); // Musst du im Thread implementieren
      ReplyCode = 900;
      return cString::sprintf("Currently %d clients connected", count);
    }
    ReplyCode = 554;
    return "Worker thread not running";
  }

  return NULL; // VDR shows the standard help in this case
}

VDRPLUGINCREATOR(cPluginWebsocket); // Don't touch this!
