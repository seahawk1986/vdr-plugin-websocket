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
            dsyslog("websocket-plugin: switch to channel '%s'", channel->Name());
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
    queue.push(DeviceEvent(eEventType::TimerChange, Timer->File(), "", Timer->Id(), (Change == tcAdd) ? true : false));
}

void cWebsocketStatusMonitor::Recording(const cDevice *Device, const char *Name, const char *FileName, bool On)
{
    queue.push(DeviceEvent(eEventType::Recording, Name ? Name : "", FileName ? FileName : "", Device->DeviceNumber(), On));
}

void cWebsocketStatusMonitor::SetVolume(int Volume, bool Absolute)
{
    queue.push(DeviceEvent(eEventType::VolumeChange, "", "", Volume));
}

void cWebsocketStatusMonitor::SetAudioTrack(int Index, const char *const *Tracks)
{
    queue.push(DeviceEvent(eEventType::AudioTrackChange, Tracks && Tracks[Index] ? Tracks[Index] : "", "", Index));
}

void cWebsocketStatusMonitor::SetAudioChannel(int AudioChannel)
{
    queue.push(DeviceEvent(eEventType::AudioChannelChange, "", "", AudioChannel));
}

void cWebsocketStatusMonitor::SetSubtitleTrack(int Index, const char *const *Tracks)
{
    queue.push(DeviceEvent(eEventType::SubtitleChange, Tracks && Tracks[Index] ? Tracks[Index] : "", "", Index));
}

void cWebsocketStatusMonitor::OsdStatusMessage(eMessageType Type, const char *Message)
{
    if (Message)
        queue.push(DeviceEvent(eEventType::OsdMessage, Message, "", Type));
    else
        queue.push(DeviceEvent(eEventType::OsdMessage, "", "", -1));
}

void cWebsocketStatusMonitor::OsdChannel(const char *Text)
{
    queue.push(DeviceEvent(eEventType::OsdChannel, Text ? Text : ""));
}

// Called if the menu is opened or updated
void cWebsocketStatusMonitor::OsdTitle(const char *Title)
{
    queue.push(DeviceEvent(eEventType::OsdTitle, Title ? Title : ""));
}

// for each menu item
void cWebsocketStatusMonitor::OsdItem(const char *Text, int Index)
{
    queue.push(DeviceEvent(eEventType::OsdItem, Text ? Text : "", "", Index));
}

// the item with the focus
void cWebsocketStatusMonitor::OsdCurrentItem(const char *Text, int Index)
{
    queue.push(DeviceEvent(eEventType::OsdCurrentItem, Text ? Text : "", "", Index));
}

// multiline text
void cWebsocketStatusMonitor::OsdTextItem(const char *Text, bool Scroll)
{
    // we don't care about scroll events, so let's dump all of those events
    if (!Text)
    {
        return;
    }
    queue.push(DeviceEvent(eEventType::OsdTextItem, Text));
}

void cWebsocketStatusMonitor::OsdHelpKeys(const char *Red, const char *Green, const char *Yellow, const char *Blue)
{
    // build up a string: "red|green|yellow|blue"
    std::string keys = std::string(Red ? Red : "") + "|" +
                       (Green ? Green : "") + "|" +
                       (Yellow ? Yellow : "") + "|" +
                       (Blue ? Blue : "");

    isyslog("websocket-plugin: OSD HelpKeys: %s", keys.c_str());
    queue.push(DeviceEvent(eEventType::OsdHelpKeys, keys));
}

void cWebsocketStatusMonitor::OsdClear()
{
    dsyslog("osd cleared");
    queue.push(DeviceEvent(eEventType::OsdClear));
}
