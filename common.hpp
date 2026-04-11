#pragma once

#include <vdr/tools.h>
#include <stdarg.h>
#include <vector>
#include <string>
#include <nlohmann/json.hpp>
#include "config.hpp"

using json = nlohmann::json;

// static constexpr auto safeStr = [](const char *s)
// {
//     if (!s)
//         return std::string{""};
//     return std::string(json(s, nlohmann::json::error_handler_t::replace));
// };

inline void
Debug(const char *Format, ...)
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

inline std::string safeStr(const char *s)
{
    if (!s)
        return "";

    std::string result;
    const unsigned char *p = reinterpret_cast<const unsigned char *>(s);

    while (*p)
    {
        unsigned char c = *p;

        if (c < 0x80)
        {
            // ASCII & Steuerzeichen (Tab, LF, etc.) einfach übernehmen
            result += static_cast<char>(c);
            p++;
        }
        else if ((c & 0xE0) == 0xC0)
        { // 2-Byte UTF-8
            if ((p[1] & 0xC0) == 0x80)
            {
                result.append(reinterpret_cast<const char *>(p), 2);
                p += 2;
            }
            else
            {
                goto replace;
            }
        }
        else if ((c & 0xF0) == 0xE0)
        { // 3-Byte UTF-8
            if ((p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80)
            {
                result.append(reinterpret_cast<const char *>(p), 3);
                p += 3;
            }
            else
            {
                goto replace;
            }
        }
        else if ((c & 0xF8) == 0xF0)
        { // 4-Byte UTF-8
            if ((p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80)
            {
                result.append(reinterpret_cast<const char *>(p), 4);
                p += 4;
            }
            else
            {
                goto replace;
            }
        }
        else
        {
        replace:
            // Ungültiges Byte (wie dein 0x80) durch Unicode-Replacement-Char (UTF-8) ersetzen
            result += "\xEF\xBF\xBD";
            p++;
        }
    }
    return result;
}