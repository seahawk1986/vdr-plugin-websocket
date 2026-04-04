#pragma once

#include <vdr/tools.h>
#include <stdarg.h>
#include <vector>
#include <string>
#include "config.hpp"

static constexpr auto safeStr = [](const char *s)
{ return s ? s : ""; };

inline void Debug(const char *Format, ...)
{
    if (WebsocketConfig.ShowDebug)
    {
        va_list args, args_copy;
        va_start(args, Format);
        va_copy(args_copy, args);

        int size = vsnprintf(nullptr, 0, Format, args);
        va_end(args);

        if (size > 0)
        {
            std::vector<char> buffer(size + 1);
            vsnprintf(buffer.data(), buffer.size(), Format, args_copy);

            dsyslog("websocket: %s", buffer.data());
        }

        va_end(args_copy);
    }
}
