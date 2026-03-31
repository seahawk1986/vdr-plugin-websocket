#include "statusmonitor.hpp"
#include <vdr/plugin.h>

cWebsocketStatusMonitor::cWebsocketStatusMonitor(EventQueue &q)
    : cStatus(), queue(q)
{
    isyslog("websocket-plugin: Monitor beim VDR registriert");
}

cWebsocketStatusMonitor::~cWebsocketStatusMonitor()
{
    isyslog("websocket-plugin: Monitor abgemeldet");
}

void cWebsocketStatusMonitor::ChannelSwitch(const cDevice *Device, int ChannelNumber, bool LiveView)
{
    if (LiveView && Device && Device->IsPrimaryDevice())
    {
        LOCK_CHANNELS_READ;
        const cChannel *channel = Channels->GetByNumber(ChannelNumber);
        if (channel)
        {
            isyslog("websocket-plugin: Kanalwechsel auf %s", channel->Name());
            queue.push(DeviceEvent(eEventType::ChannelChange, channel->Name(), "", channel->Number()));
        }
    }
}

void cWebsocketStatusMonitor::Replaying(const cControl *Control, const char *Name, const char *FileName, bool On)
{
    if (On && Name)
    {
        isyslog("websocket-plugin: Replay Start: %s", Name);
        queue.push(DeviceEvent(eEventType::ReplayStart, Name, FileName ? FileName : "", 0));
    }
    else
    {
        isyslog("websocket-plugin: Replay Stop");
        queue.push(DeviceEvent(eEventType::ReplayStop, "", "", 0));
    }
}

void cWebsocketStatusMonitor::TimerChange(const cTimer *Timer, eTimerChange Change)
{
    queue.push(DeviceEvent(eEventType::TimerChange, Timer->File(), "", (int)Change));
}

void cWebsocketStatusMonitor::Recording(const cDevice *Device, const char *Name, const char *FileName, bool On)
{
    queue.push(DeviceEvent(eEventType::Recording, Name ? Name : "", FileName ? FileName : "", 0, On));
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

void cWebsocketStatusMonitor::OsdStatusMessage(const char *Message)
{
    if (Message)
        queue.push(DeviceEvent(eEventType::OsdMessage, Message));
}

void cWebsocketStatusMonitor::OsdChannel(const char *Text)
{
    if (Text)
    {
        queue.push(DeviceEvent(eEventType::OsdChannel, Text));
    }
}
