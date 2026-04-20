#pragma once

#include <vdr/tools.h>
#include <stdarg.h>
#include <string>
#include <nlohmann/json.hpp>
#include "config.hpp"

using json = nlohmann::json;

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

inline std::string safeStr(const char *s)
{
    if (!s || !*s)
        return "";

    // use a 'thread_local' converter to avoid reinstantiation
    thread_local cCharSetConv conv(NULL, "UTF-8");

    const char *source = conv.Convert(s);
    if (!source)
        source = s;

    std::string result;
    // minimize reallocations for string concetenation
    result.reserve(strlen(source));

    const unsigned char *p = reinterpret_cast<const unsigned char *>(source);

    while (*p)
    {
        unsigned char c = *p;

        if (c < 0x80)
        {
            result += static_cast<char>(c);
            p++;
        }
        else if ((c & 0xE0) == 0xC0)
        { // 2-Byte
            if (p[1] && (p[1] & 0xC0) == 0x80)
            {
                result.append(reinterpret_cast<const char *>(p), 2);
                p += 2;
            }
            else
                goto replace;
        }
        else if ((c & 0xF0) == 0xE0)
        { // 3-Byte
            if (p[1] && (p[1] & 0xC0) == 0x80 && p[2] && (p[2] & 0xC0) == 0x80)
            {
                result.append(reinterpret_cast<const char *>(p), 3);
                p += 3;
            }
            else
                goto replace;
        }
        else if ((c & 0xF8) == 0xF0)
        { // 4-Byte
            if (p[1] && (p[1] & 0xC0) == 0x80 && p[2] && (p[2] & 0xC0) == 0x80 && p[3] && (p[3] & 0xC0) == 0x80)
            {
                result.append(reinterpret_cast<const char *>(p), 4);
                p += 4;
            }
            else
                goto replace;
        }
        else
        {
        replace:
            result += "\xEF\xBF\xBD"; // Unicode Replacement Character
            p++;
        }
    }
    return result;
}
