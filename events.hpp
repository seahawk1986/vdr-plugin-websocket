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
    // OsdTitle,
    // OsdItem,
    // OsdTextItem,
    OsdCurrentItem,
    // OsdHelpKeys,
    OsdClear,
    JsonString,
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
    bool empty()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return queue.empty();
    }
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
    std::optional<DeviceEvent> try_pop()
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (queue.empty())
            return std::nullopt;
        DeviceEvent ev = std::move(queue.front());
        queue.pop();
        return ev;
    }

    std::optional<DeviceEvent> pop_with_timeout(int milliseconds)
    {
        std::unique_lock<std::mutex> lock(mutex);
        // Wenn milliseconds 0 ist, prüfen wir nur, ob etwas da ist, ohne zu warten
        if (milliseconds == 0)
        {
            if (queue.empty())
                return std::nullopt;
        }
        else
        {
            // Nur warten, wenn milliseconds > 0
            if (!cv.wait_for(lock, std::chrono::milliseconds(milliseconds), [this]
                             { return !queue.empty(); }))
            {
                return std::nullopt;
            }
        }

        DeviceEvent ev = std::move(queue.front());
        queue.pop();
        return ev;
    }
};
