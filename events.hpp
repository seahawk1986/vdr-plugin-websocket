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
};

struct DeviceEvent
{
    eEventType type;
    std::string name;
    std::string fileName;
    int number;

    DeviceEvent(eEventType t, std::string n, std::string f = "", int num = 0)
        : type(t),
          name(std::move(n)),
          fileName(std::move(f)),
          number(num)
    {
    }
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
        // Wartet bis ein Event da ist ODER die Zeit abläuft
        if (cv.wait_for(lock, std::chrono::milliseconds(milliseconds), [this]
                        { return !queue.empty(); }))
        {
            DeviceEvent ev = std::move(queue.front());
            queue.pop();
            return ev;
        }
        return std::nullopt; // Zeit abgelaufen, kein Event
    }
};
