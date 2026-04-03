#pragma once

#include <atomic>
#include <vdr/status.h>
#include <vdr/channels.h>
#include <vdr/device.h>
#include "events.hpp"
#include "osdState.hpp"

class cWebsocketStatusMonitor : public cStatus
{
private:
    EventQueue &queue;
    cOsdState osdState;

    std::mutex osdMutex; // Schützt den Zugriff auf osd und timer-Flags
    cTimeMs timeoutTimer;
    std::atomic<bool> timerActive = false;

    // Hilfsmethode für den Versand
    void PushJson(json &&j)
    {
        if (!j.empty())
        {
            dsyslog("websocket-plugin: OSD Update: %s", j.dump().c_str());
            queue.push(DeviceEvent(eEventType::JsonString, j.dump()));
        }
    }

    void CheckAndSend(bool force = false)
    {
        PushJson(osdState.GetUpdate());
        timerActive = false;
    }

protected:
    void
    TimerChange(const cTimer *Timer, eTimerChange Change) override;
    void ChannelSwitch(const cDevice *Device, int ChannelNumber, bool LiveView) override;
    void Recording(const cDevice *Device, const char *Name, const char *FileName, bool On) override;
    void Replaying(const cControl *Control, const char *Name, const char *FileName, bool On) override;
    void SetVolume(int Volume, bool Absolute) override;
    void SetAudioTrack(int Index, const char *const *Tracks) override;
    void SetAudioChannel(int AudioChannel) override;
    void SetSubtitleTrack(int Index, const char *const *Tracks) override;
    // OSD messages
    void OsdStatusMessage(const char *Message);
    void OsdStatusMessage(eMessageType Type, const char *Message) override;
    // OSD channel/epg info (on channel switch resp. if the user presses OK during Live-TV)
    void OsdChannel(const char *Text) override;
    // OSD menu methods
    void OsdTitle(const char *Title) override;
    void OsdItem(const char *Text, int Index, bool Selectable) override;
    void OsdTextItem(const char *Text, bool Scroll) override; // Mainly used for scrollable Text fields
    void OsdCurrentItem(const char *Text, int Index) override;
    void OsdHelpKeys(const char *Red, const char *Green, const char *Yellow, const char *Blue) override;

    // for all OSD operations
    void OsdClear(void) override;

public:
    explicit cWebsocketStatusMonitor(EventQueue &q);
    virtual ~cWebsocketStatusMonitor();

    void CheckTimer()
    {
        std::lock_guard<std::mutex> lock(osdMutex);
        if (timerActive && timeoutTimer.TimedOut())
        {
            // force empty menu (if no OsdCurrentItem was received within the timeout)
            PushJson(osdState.GetUpdate());
            timerActive = false;
        }
    }

    json GetCurrentOsdJson()
    {
        std::lock_guard<std::mutex> lock(osdMutex);
        return osdState.GetCurrentStateAsJson();
    }
};
