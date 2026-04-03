#ifndef OSDSTATE_H
#define OSDSTATE_H

#include <string>
#include <vector>
#include <array>
#include <mutex> // Neu: Für Thread-Sicherheit
#include <algorithm>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct tOsdItemData
{
    std::string text;
    bool selectable;
};

class cOsdState
{
private:
    mutable std::recursive_mutex stateMutex;

    std::string title;
    std::vector<tOsdItemData> items;
    std::string textContent;
    std::array<std::string, 4> helpKeys;

    std::string statusMessage;
    bool hasStatusMessage = false;

    bool isTextMode = false;
    bool scrollDirection = false;
    bool shouldScroll = false;
    int itemsWrittenThisCycle = 0;
    int currentIndex = -1;
    json lastJson = json::object(); // Initial als Objekt starten

public:
    void Clear()
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        itemsWrittenThisCycle = 0;
        statusMessage.clear();
        hasStatusMessage = false;

        currentIndex = -1;
        items.clear();
        lastJson = json::object();
        for (auto &k : helpKeys)
            k.clear();
        isTextMode = false;
        textContent.clear();
        dsyslog("websocket-plugin: OSD State cleared");
    }

    void ClearStatusMessage()
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        statusMessage.clear();
        hasStatusMessage = false;
    }

    void SetStatusMessage(const char *Message)
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        statusMessage = Message ? Message : "";
        hasStatusMessage = (Message != nullptr);
    }

    void SetTitle(const char *Text)
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        title = Text ? Text : "";
    }

    void SetHelpKeys(const char *Red, const char *Green, const char *Yellow, const char *Blue)
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        helpKeys[0] = Red ? Red : "";
        helpKeys[1] = Green ? Green : "";
        helpKeys[2] = Yellow ? Yellow : "";
        helpKeys[3] = Blue ? Blue : "";
    }

    void UpdateItem(const char *Text, int Index, bool Selectable)
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        dsyslog("websocket-plugin: Update item: '%s' at idx %d, selectable: %d", Text, Index, Selectable);
        isTextMode = false;
        if (Index >= (int)items.size())
            items.resize(Index + 1);
        items[Index] = {Text ? Text : "", Selectable};
        itemsWrittenThisCycle = std::max(itemsWrittenThisCycle, Index + 1);
    }

    void SetTextItem(const char *Text, const bool Scroll)
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        isTextMode = true;
        if (Text)
        {
            shouldScroll = false;
            textContent = Text;
        }
        else
        {
            shouldScroll = true;
            scrollDirection = Scroll;
        }
    }

    void SetCurrentItem(const char *Text, int Index)
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        currentIndex = Index;
        if (Index >= 0)
        {
            if (Index >= (int)items.size())
                items.resize(Index + 1);
            items[Index] = {Text ? Text : "", true};
        }
    }

    json GetUpdate()
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);

        // 1. Aktuelles JSON bauen
        if (!isTextMode && (int)items.size() > itemsWrittenThisCycle)
            items.resize(itemsWrittenThisCycle);

        json j;
        j["type"] = "osd";
        j["sub"] = "full";
        j["title"] = title;
        j["index"] = currentIndex;
        j["mode"] = isTextMode ? "text" : "list";
        j["keys"] = {{"red", helpKeys[0]}, {"green", helpKeys[1]}, {"yellow", helpKeys[2]}, {"blue", helpKeys[3]}};

        if (isTextMode)
        {
            j["content"] = textContent;
            j["scroll"] = shouldScroll ? (scrollDirection ? 1 : -1) : 0;
        }
        else
        {
            json itemArray = json::array();
            for (const auto &item : items)
                itemArray.push_back({{"value", item.text}, {"selectable", item.selectable}});
            j["items"] = itemArray;
        }

        // 2. Vergleich mit letztem Stand (Basis-Check)
        if (lastJson.is_object() &&
            j.value("title", "") == lastJson.value("title", "") &&
            j.value("items", json::array()) == lastJson.value("items", json::array()) &&
            j.value("mode", "") == lastJson.value("mode", "") &&
            j.value("content", "") == lastJson.value("content", ""))
        {
            // Basis ist gleich, prüfen ob Details (Index, Keys, Scroll) anders sind
            json u;
            bool changed = false;

            if (j["keys"] != lastJson.value("keys", json::object()))
            {
                u["keys"] = j["keys"];
                changed = true;
            }
            if (j.value("scroll", 0) != lastJson.value("scroll", 0))
            {
                u["scroll"] = j["scroll"];
                changed = true;
            }
            if (j["index"] != lastJson.value("index", -2))
            {
                u["index"] = currentIndex;
                changed = true;
            }

            lastJson = j;
            if (changed)
            {
                u["type"] = "osd";
                u["sub"] = "update";
                return u;
            }
            return json(); // Absolut keine Änderung
        }

        // 3. Wenn Basis anders: Full Update senden
        lastJson = j;
        return j;
    }

    json GetCurrentStateAsJson()
    {
        // Falls im Listen-Modus: Auf tatsächlich geschriebene Items kürzen
        if (!isTextMode && (int)items.size() > itemsWrittenThisCycle)
        {
            items.resize(itemsWrittenThisCycle);
        }

        json j;
        j["t"] = "osd_update";
        j["title"] = title;
        j["current"] = currentIndex;
        j["mode"] = isTextMode ? "text" : "list";
        j["keys"] = {helpKeys[0], helpKeys[1], helpKeys[2], helpKeys[3]};

        if (isTextMode)
        {
            j["content"] = textContent;
        }
        else
        {
            json itemArray = json::array();
            for (const auto &item : items)
            {
                itemArray.push_back({{"v", item.text}, {"s", item.selectable}});
            }
            j["items"] = itemArray;
        }
        j["status_msg"] = hasStatusMessage ? json(statusMessage) : json(nullptr);
        return j;
    }

private:
    // Interne Helper ohne eigenen Lock (vermeidet Deadlocks)
    void ClearStatusMessageInternal()
    {
        statusMessage.clear();
        hasStatusMessage = false;
    }
};
#endif
