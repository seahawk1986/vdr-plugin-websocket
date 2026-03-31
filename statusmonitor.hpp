#pragma once

#include <vdr/status.h>
#include <vdr/channels.h>
#include <vdr/device.h>
#include "events.hpp"

class cWebsocketStatusMonitor : public cStatus
{
private:
    EventQueue &queue;

protected:
    void TimerChange(const cTimer *Timer, eTimerChange Change) override;
    void ChannelSwitch(const cDevice *Device, int ChannelNumber, bool LiveView) override;
    void Recording(const cDevice *Device, const char *Name, const char *FileName, bool On) override;
    void Replaying(const cControl *Control, const char *Name, const char *FileName, bool On) override;
    void SetVolume(int Volume, bool Absolute) override;
    void SetAudioTrack(int Index, const char *const *Tracks) override;
    void SetAudioChannel(int AudioChannel) override;
    void SetSubtitleTrack(int Index, const char *const *Tracks) override;
    void OsdStatusMessage(const char *Message) override;
    void OsdChannel(const char *Text) override;

    void OsdClear(void);

public:
    explicit cWebsocketStatusMonitor(EventQueue &q);
    virtual ~cWebsocketStatusMonitor();
};
