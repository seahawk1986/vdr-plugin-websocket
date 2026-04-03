#include <magic_enum/magic_enum.hpp>
#include "statusmonitor.hpp"
#include <vdr/plugin.h>

cWebsocketStatusMonitor::cWebsocketStatusMonitor(EventQueue &q)
    : cStatus(), queue(q)
{
    dsyslog("websocket-plugin: registered vdr status monitor");
}

cWebsocketStatusMonitor::~cWebsocketStatusMonitor()
{
    dsyslog("websocket-plugin: cancelled status monitor");
}

void cWebsocketStatusMonitor::ChannelSwitch(const cDevice *Device, int ChannelNumber, bool LiveView)
{
    if (LiveView && Device && Device->IsPrimaryDevice())
    {
        LOCK_CHANNELS_READ;
        const cChannel *channel = Channels->GetByNumber(ChannelNumber);
        if (channel)
        {
            dsyslog("websocket-plugin: switch to channel %d '%s'", channel->Number(), channel->Name());
            queue.push(DeviceEvent(eEventType::ChannelChange, channel->Name(), "", channel->Number()));
        }
    }
}

void cWebsocketStatusMonitor::Replaying(const cControl *Control, const char *Name, const char *FileName, bool On)
{
    if (On && Name)
    {
        dsyslog("websocket-plugin: Replay started: %s", Name);
        queue.push(DeviceEvent(eEventType::ReplayStart, Name, FileName ? FileName : "", 0));
    }
    else
    {
        dsyslog("websocket-plugin: Replay stopped");
        queue.push(DeviceEvent(eEventType::ReplayStop, "", "", 0));
    }
}

void cWebsocketStatusMonitor::TimerChange(const cTimer *Timer, eTimerChange Change)
{
    const auto isLocal = Timer->Local() ? "local" : "remote";
    dsyslog("websocket-plugin: Timer change: id: %d, '%s': '%s' (%s)", Timer->Id(), Timer->File(), std::string(magic_enum::enum_name(Change)).c_str(), isLocal);
    queue.push(DeviceEvent(eEventType::TimerChange, Timer->File(), "", Timer->Id(), (Change == tcAdd) ? true : false));
}

void cWebsocketStatusMonitor::Recording(const cDevice *Device, const char *Name, const char *FileName, bool On)
{
    queue.push(DeviceEvent(eEventType::Recording, Name ? Name : "", FileName ? FileName : "", Device->DeviceNumber(), On));
}

void cWebsocketStatusMonitor::SetVolume(int Volume, bool Absolute)
{
    dsyslog("websocket-plugin: volume changed: %d, absolute: %s", Volume, Absolute ? "true" : "false");
    queue.push(DeviceEvent(eEventType::VolumeChange, "", "", Volume, Absolute));
}

void cWebsocketStatusMonitor::SetAudioTrack(int Index, const char *const *Tracks)
{
    const auto track = Tracks && Tracks[Index] ? Tracks[Index] : "";
    dsyslog("websocket-plugin: SetAudioTrack to %d: %s", Index, track);
    queue.push(DeviceEvent(eEventType::AudioTrackChange, track, "", Index));
}

void cWebsocketStatusMonitor::SetAudioChannel(int AudioChannel)
{
    dsyslog("websocket-plugin: SetAudioChannel to %d", AudioChannel);
    queue.push(DeviceEvent(eEventType::AudioChannelChange, "", "", AudioChannel));
}

void cWebsocketStatusMonitor::SetSubtitleTrack(int Index, const char *const *Tracks)
{
    const auto track = Tracks && Tracks[Index] ? Tracks[Index] : "";
    dsyslog("websocket-plugin: SetSubtitleTrack to %d: %s", Index, track);
    queue.push(DeviceEvent(eEventType::SubtitleChange, track, "", Index));
}

void cWebsocketStatusMonitor::OsdStatusMessage(const char *Message)
{
    dsyslog("websocket-plugin: got OSDStatusMessage without Type: '%s'", Message);
    OsdStatusMessage(mtStatus, Message);
}

void cWebsocketStatusMonitor::OsdStatusMessage(eMessageType Type, const char *Message)
{
    std::lock_guard<std::mutex> lock(osdMutex);
    dsyslog("websocket-plugin: OSD Message [%s]: %s",
            std::string(magic_enum::enum_name(Type)).c_str(),
            Message ? Message : "CLEAR");
    if (Message)
    {
        osdState.SetStatusMessage(Message);
        queue.push(DeviceEvent(eEventType::OsdMessage, Message, "", Type));
    }
    else
    {
        osdState.ClearStatusMessage();
        queue.push(DeviceEvent(eEventType::OsdMessage, "", "", -1));
    }
}

void cWebsocketStatusMonitor::OsdChannel(const char *Text)
{
    std::lock_guard<std::mutex> lock(osdMutex);
    dsyslog("websocket-plugin: OsdChannel: '%s", Text);
    queue.push(DeviceEvent(eEventType::OsdChannel, Text ? Text : ""));
}

void cWebsocketStatusMonitor::OsdTitle(const char *Title)
{
    std::lock_guard<std::mutex> lock(osdMutex);
    dsyslog("websocket-plugin: OsdTitle: '%s", Title);
    osdState.SetTitle(Title);
    timeoutTimer.Set(100);
    timerActive = true;
}

void cWebsocketStatusMonitor::OsdItem(const char *Text, int Index, bool Selectable)
{
    std::lock_guard<std::mutex> lock(osdMutex);
    const auto selectable = Selectable ? "true" : "false";
    dsyslog("websocket-plugin: OsdItem: '%s' [%d] (selectable: %s)", Text, Index, selectable);
    osdState.UpdateItem(Text, Index, Selectable);
    timerActive = false;
}

void cWebsocketStatusMonitor::OsdCurrentItem(const char *Text, int Index)
{
    std::lock_guard<std::mutex> lock(osdMutex);
    timerActive = false;
    dsyslog("websocket-plugin: OsdCurrentItem: '%s' [%d]", Text, Index);
    osdState.SetCurrentItem(Text, Index);

    CheckAndSend();
}

void cWebsocketStatusMonitor::OsdTextItem(const char *Text, bool Scroll)
{
    std::lock_guard<std::mutex> lock(osdMutex);
    timerActive = false;
    dsyslog("websocket-plugin: OsdTextItem: '%s' (Scroll: %s)", Text, Scroll ? "true" : "false");
    osdState.SetTextItem(Text, Scroll);
    CheckAndSend();
}

void cWebsocketStatusMonitor::OsdHelpKeys(const char *Red, const char *Green, const char *Yellow, const char *Blue)
{
    std::lock_guard<std::mutex> lock(osdMutex);
    dsyslog("websocket-plugin: OsdHelpKeys Red: '%s', Green: '%s', Yellow: '%s', Blue: '%s'", Red, Green, Yellow, Blue);
    osdState.SetHelpKeys(Red, Green, Yellow, Blue);
    if (!timerActive)
        CheckAndSend();
}

void cWebsocketStatusMonitor::OsdClear()
{
    std::lock_guard<std::mutex> lock(osdMutex);
    dsyslog("websocket-plugin: OsdClear");
    osdState.Clear();
    queue.push(DeviceEvent(eEventType::OsdClear));
}
