#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <string>

enum class eEventType
{
    ChannelChange,
    ReplayStart,
    ReplayStop,
    PluginStop,
    TimerChange,
    Recording,
    VolumeChange,
    AudioChannelChange,
    AudioTrackChange,
    SubtitleChange,
    OsdMessage,
    OsdChannel,
    OsdClear,
};

struct DeviceEvent
{
    eEventType type;
    std::string name;
    std::string fileName;
    int number;
    bool status;

    DeviceEvent(eEventType t, std::string n = "", std::string f = "", int num = 0, bool s = false)
        : type(t), name(std::move(n)), fileName(std::move(f)), number(num), status(s) {}
};

class EventQueue
{
private:
    std::queue<DeviceEvent> queue;
    std::mutex mutex;
    std::condition_variable cv;

public:
    void push(DeviceEvent ev)
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            queue.push(std::move(ev));
        }
        cv.notify_one();
    }

    DeviceEvent pop()
    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [this]
                { return !queue.empty(); });
        DeviceEvent ev = std::move(queue.front());
        queue.pop();
        return ev;
    }
    std::optional<DeviceEvent> pop_with_timeout(int milliseconds)
    {
        std::unique_lock<std::mutex> lock(mutex);
        // wait for an event or timeout
        if (cv.wait_for(lock, std::chrono::milliseconds(milliseconds), [this]
                        { return !queue.empty(); }))
        {
            DeviceEvent ev = std::move(queue.front());
            queue.pop();
            return ev;
        }
        return std::nullopt; // return no event on timeout
    }
};
