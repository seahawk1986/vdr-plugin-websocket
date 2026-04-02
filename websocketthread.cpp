#include "websocketthread.hpp"
#include <vdr/plugin.h>
#include <vdr/menu.h>

#include <algorithm>
#include <cctype>
#include <sys/stat.h>
#include <unistd.h>
#include <strings.h> // for strcasecmp
#include <dirent.h>

int cWebsocketThread::GetClientCount()
{
    int count = 0;
    // count websocket connections
    for (struct mg_connection *c = mgr.conns; c != NULL; c = c->next)
    {
        if (c->is_websocket)
        {
            count++;
        }
    }
    return count;
}

bool isEqualCaseInsensitive(const std::string &a, const std::string &b)
{
    return strcasecmp(a.c_str(), b.c_str()) == 0;
}

cWebsocketThread::cWebsocketThread(EventQueue &q, int p, std::string ld)
    : cThread("websocket-worker"),
      queue(q),
      port(p),
      logoDir(std::move(ld)),
      lastOsdActivity(std::chrono::steady_clock::now()),
      lastQueueActivity(std::chrono::steady_clock::now()),
      lastListSent(std::chrono::steady_clock::now()),
      osdChanged(false),
      osdItemsChanged(false),
      osdHelpChanged(false),
      focusChanged(false),
      osdMessageOpen(false),
      currentFocusIndex(-1),
      osdTitle(""),
      osdItems()
{
    dsyslog("websocket-plugin: Thread-Objekt erfolgreich initialisiert");
}

cWebsocketThread::~cWebsocketThread()
{
    StopThread();
}

void cWebsocketThread::StopThread()
{
    Cancel();
}

std::string cWebsocketThread::toLower(std::string s)
{
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c)
                   { return std::tolower(c); });
    return result;
}

void cWebsocketThread::UpdateLogoCache()
{
    std::lock_guard<std::mutex> lock(cacheMutex);
    logoCache.clear();
    DIR *dir = opendir(logoDir.c_str());
    if (!dir)
        return;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL)
    {
        // Ignoriere "." und ".." explizit
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        if (ent->d_type == DT_REG || ent->d_type == DT_LNK)
        {
            std::string fileName = ent->d_name;
            std::string lowFile = toLower(fileName);

            size_t lastDot = lowFile.find_last_of(".");
            if (lastDot == std::string::npos || lastDot == 0)
                continue;

            std::string ext = lowFile.substr(lastDot);
            if (ext == ".png" || ext == ".svg" || ext == ".jpg" || ext == ".jpeg")
            {
                std::string nameOnly = lowFile.substr(0, lastDot);

                // set keys
                logoCache[lowFile] = fileName;
                logoCache[nameOnly] = fileName;

                std::string sanitized = nameOnly;
                std::replace(sanitized.begin(), sanitized.end(), ' ', '_');

                if (sanitized != nameOnly)
                {
                    logoCache[sanitized] = fileName;
                    logoCache[sanitized + ext] = fileName;
                }
            }
        }
    }
    closedir(dir);
    dsyslog("websocket-plugin: Created logo cache with %zu keys", logoCache.size());
}

std::string cWebsocketThread::FindLogoInCache(const std::string &request)
{
    std::lock_guard<std::mutex> lock(cacheMutex);
    std::string search = toLower(request);

    // 1. look for the file name in lower case (finds "name.png" oder "name", if in cache)
    if (logoCache.count(search))
        return logoCache[search];

    // 2. Fallback: look for underscores instead of spaces (e.g. "das_erste.png")
    std::string sanitized = search;
    std::replace(sanitized.begin(), sanitized.end(), ' ', '_');
    if (logoCache.count(sanitized))
        return logoCache[sanitized];

    // 3. Fallback: try to add extensions if the filename hase none
    if (search.find('.') == std::string::npos)
    {
        std::vector<std::string> exts = {".png", ".svg", ".jpg", ".jpeg"};
        for (const auto &ext : exts)
        {
            if (logoCache.count(search + ext))
                return logoCache[search + ext];
            if (logoCache.count(sanitized + ext))
                return logoCache[sanitized + ext];
        }
    }

    return "";
}

std::string cWebsocketThread::GetLogoPath(const cChannel *channel)
{
    if (!channel)
        return "";

    // 1. look up by channel id
    std::string id = *channel->GetChannelID().ToString();
    std::string found = FindLogoInCache(id);
    if (!found.empty())
        return found;

    // 2. look for part without channel group
    std::string name = channel->Name();
    size_t semi = name.find(';');
    if (semi != std::string::npos)
    {
        name = name.substr(0, semi); // "Das Erste HD;ARD" -> "Das Erste HD"
    }

    found = FindLogoInCache(name);
    if (!found.empty())
        return found;

    // 4. remove comma if in name
    size_t comma = name.find(',');
    if (comma != std::string::npos)
    {
        name = name.substr(0, comma);
        found = FindLogoInCache(name);
    }

    return found; // set to default.png in BuildStatusJson
}

void cWebsocketThread::on_connect_callback(struct mg_connection *c, int ev, void *ev_data)
{
    if (ev == MG_EV_HTTP_MSG)
    {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;
        auto *self = static_cast<cWebsocketThread *>(c->mgr->userdata);

        // check if mgr.userdata is (unlikely) NULL
        if (!self)
            return;

        if (mg_match(hm->uri, mg_str("/logos/#"), NULL))
        {
            // 1. Pfad nach "/logos/" extrahieren
            // Wir prüfen, ob die URI überhaupt lang genug ist
            if (hm->uri.len <= 7)
            {
                mg_http_reply(c, 400, "", "Bad Request\n");
                return;
            }

            std::string uriPath(hm->uri.buf + 7, hm->uri.len - 7);

            // 2. URL-Decoding mit festem Puffer
            char decoded[512];
            // mg_url_decode gibt die Länge zurück. Bei Fehlern (z.B. % am Ende) ist es < 0.
            int len = mg_url_decode(uriPath.c_str(), uriPath.size(), decoded, sizeof(decoded), 0);

            // check decoded length
            if (len > 0 && len < (int)sizeof(decoded))
            {
                // limit length of non-null terminated data
                std::string requestedBaseName(decoded, len);
                std::string fileName = self->FindLogoInCache(requestedBaseName);

                if (!fileName.empty())
                {
                    std::string fullPath = self->getLogoDir() + fileName;
                    struct mg_http_serve_opts opts = {};
                    mg_http_serve_file(c, hm, fullPath.c_str(), &opts);
                    return;
                }
            }
            // Fallback to default.png oder 404
            std::string defaultPath = self->getLogoDir() + "default.png";
            if (access(defaultPath.c_str(), F_OK) == 0)
            {
                struct mg_http_serve_opts opts = {};
                mg_http_serve_file(c, hm, defaultPath.c_str(), &opts);
            }
            else
            {
                mg_http_reply(c, 404, "", "Not found\n");
            }
        }
        else if (mg_match(hm->uri, mg_str("/"), NULL))
        {
            mg_ws_upgrade(c, hm, NULL);
        }
    }
    else if (ev == MG_EV_WS_OPEN)
    {
        auto *self = static_cast<cWebsocketThread *>(c->mgr->userdata);
        if (self)
            self->SendInitialState(c);
    }
}

json cWebsocketThread::BuildStatusJson(const DeviceEvent &ev)
{
    json j;
    switch (ev.type)
    {

    case eEventType::ChannelChange:
    {
        j["type"] = "channel";
        j["number"] = ev.number;
        j["name"] = ev.name;

        LOCK_CHANNELS_READ;
        const cChannel *channel = Channels->GetByNumber(ev.number);
        // search by name if the lookup by id was not successful
        if (!channel && !ev.name.empty())
        {
            for (const cChannel *c = Channels->First(); c; c = Channels->Next(c))
            {
                // compare the name (VDR names can contain provider infos after a ';')
                if (c->Name() && strcmp(c->Name(), ev.name.c_str()) == 0)
                {
                    channel = c;
                    break;
                }
            }
        }
        if (channel)
        {
            dsyslog("websocket-plugin: Lookup logo for '%s'", channel->Name());
            j["name"] = channel->Name();

            // look up if we have a logo
            std::string logo = GetLogoPath(channel);
            if (logo.empty())
            {
                dsyslog("websocket-plugin: No logo found for '%s'. Cache size: %zu",
                        channel->Name(), logoCache.size());
            }
            std::string logoFile = GetLogoPath(channel);

            if (!logoFile.empty())
            {
                j["logo"] = "/logos/" + logoFile;
            }
            else
            {
                j["logo"] = "/logos/default.png";
            }

            // some infos about the stream
            j["tech"] = {
                {"is_encrypted", channel->Ca() >= 0x0100}, // Encrypted (CAID >= 0x0100)
                {"has_teletext", channel->Tpid() != 0},    // has Teletext PID
                {"has_dolby", false},                      // Initial value
                {"audio_tracks_count", 0},                 // Initial value
                {"can_decrypt", true}                      // Initial value
            };

            cDevice *primary = cDevice::PrimaryDevice();
            if (channel->Ca() >= 0x0100 && primary)
            {
                j["tech"]["can_decrypt"] = primary->ProvidesChannel(channel, 0);
            }

            // check audio tracks
            int audioCount = 0;
            bool foundDolby = false;

            // 1. check Audio-PIDs (MPEG)
            for (int i = 0; i < MAXAPIDS; i++)
            {
                if (channel->Apid(i) != 0)
                {
                    audioCount++;
                    // look for Dolby tracks in the ALANG-field (e.g. "deu+dd" oder "dd")
                    if (channel->Alang(i) && strstr(channel->Alang(i), "dd"))
                    {
                        foundDolby = true;
                    }
                }
            }

            // 2. look for dedicated Dolby-PIDs (AC3)
            for (int i = 0; i < MAXDPIDS; i++)
            {
                if (channel->Dpid(i) != 0)
                {
                    foundDolby = true;
                    audioCount++; // this counts as extra audio track
                }
            }

            j["tech"]["has_dolby"] = foundDolby;
            j["tech"]["audio_tracks_count"] = audioCount;

            LOCK_SCHEDULES_READ;
            const cSchedule *schedule = Schedules->GetSchedule(channel);
            if (schedule)
            {
                auto setEpg = [&](const std::string &key, const cEvent *e)
                {
                    if (e)
                    {
                        int progress = 0;
                        int duration = e->Duration();
                        if (key == "present" && duration > 0)
                        {
                            time_t now = time(NULL);
                            progress = std::clamp((int)((now - e->StartTime()) * 100 / duration), 0, 100);
                        }
                        j["epg"][key] = {
                            {"title", e->Title() ? e->Title() : ""},
                            {"short_text", (e->ShortText() && *e->ShortText()) ? e->ShortText() : ""},
                            {"start", (long)e->StartTime()},
                            {"duration", duration},
                            {"progress", progress}};
                    }
                };
                setEpg("present", schedule->GetPresentEvent());
                setEpg("following", schedule->GetFollowingEvent());
            }
        }
        else
        {
            esyslog("websocket-plugin: ERROR - channel '%s' (# %d) not in channel list!",
                    ev.name.c_str(), ev.number);
        }
        return j;
    }
    case eEventType::ReplayStart:
    {
        if (!ev.fileName.empty())
        {
            j["name"] = ev.name;
            j["type"] = "replay";
            j["status"] = "started";
            LOCK_RECORDINGS_READ;
            const cRecording *recording = Recordings->GetByName(ev.fileName.c_str());
            if (recording)
            {
                const cRecordingInfo *info = recording->Info();
                if (info)
                    j["recording"] = {
                        {"title", info->Title() ? info->Title() : recording->Title() ? recording->Title()
                                                                                     : recording->Name()},
                        {"subtitle", info->ShortText() ? info->ShortText() : nullptr},
                        {"description", (info->Description() && *info->Description()) ? info->Description() : ""},
                        {"duration", recording->LengthInSeconds()}};
            }
        }
        break;
    }
    case eEventType::ReplayStop:
        j["type"] = "replay";
        j["status"] = "stopped";
        return j;

    case eEventType::TimerChange:
        BroadcastTimerStatus();
        j["type"] = "timer";
        j["timer_name"] = ev.name;
        j["timer_id"] = ev.number;
        j["timer_change"] = ev.status;
        {
            LOCK_TIMERS_READ;
            auto Timer = Timers->GetById(ev.number);
            if (Timer)
            {
                auto channel = Timer->Channel();
                if (channel)
                {
                    tChannelID channel_id = channel->GetChannelID();
                    j["timer_channel_id"] = channel_id.ToString();
                    j["timer_channel_name"] = channel->Name() ? cString(channel->Name()) : channel_id.ToString();
                }
            }
        }
        return j;

    case eEventType::Recording:
    {
        BroadcastTimerStatus();
        j["type"] = "recording";
        j["name"] = ev.name;
        j["filename"] = ev.fileName;
        j["recording_active"] = ev.status;
        j["tuner"] = ev.number;
        return j;
    }

    case eEventType::VolumeChange:
    {

        cDevice *primary = cDevice::PrimaryDevice();
        if (primary)
        {
            j["volume"] = primary->IsMute() ? 0 : primary->CurrentVolume();
            j["type"] = "volume";
            return j;
        }
        break;
    }

    case eEventType::OsdClear:
    {
        clearPending = true;
        osdItemsChanged = false; // WICHTIG: Liste-Update für diese Runde stoppen
        lastOsdActivity = std::chrono::steady_clock::now();
        if (osdMessageOpen)
        {
            DeviceEvent ev(DeviceEvent(eEventType::OsdMessage, "", "", -1));
            osdMessageOpen = false;
        }
        return nlohmann::json();
    }

    case eEventType::OsdMessage:
        j["type"] = "osdmessage";
        j["message"] = ev.name;
        j["priority"] = ev.number;
        osdMessageOpen = true; // remember that the OSD message is open
        return j;

    case eEventType::OsdTitle:
    {
        if (ev.name.empty())
        {
            clearPending = true;
            osdItems.clear();
            osdTitle = "";
            return nlohmann::json();
        }

        // Wenn ein NEUER Titel kommt (Ebene tiefer oder neues Menü)
        if (osdTitle != ev.name)
        {
            osdTitle = ev.name;
            osdItems.clear();
            currentFocusIndex = -1;
            osdItemsChanged = true;
            clearPending = false; // Hier ist das OSD definitiv wieder "frisch" offen
        }
        else
        {
            // GLEICHER Titel (z.B. Back-Taste zurück ins Hauptmenü):
            // Wir triggern das Update der Liste NUR, wenn wir nicht
            // gerade ein OsdClear (clearPending) erhalten haben.
            if (!clearPending)
            {
                osdItemsChanged = true;
            }
        }
        lastOsdActivity = std::chrono::steady_clock::now();
        return nlohmann::json();
    }

    case eEventType::OsdItem:
    {
        clearPending = false; // Lebenszeichen! OSD ist offen.
        int idx = ev.number;
        if (idx < 0)
            return nlohmann::json();

        if (idx >= (int)osdItems.size())
        {
            osdItems.resize(idx + 1, "");
        }

        if (osdItems[idx] != ev.name)
        {
            osdItems[idx] = ev.name;
            osdItemsChanged = true;
        }
        lastOsdActivity = std::chrono::steady_clock::now();
        return nlohmann::json();
    }
    case eEventType::OsdTextItem:
    {
        // let's try to handle this like a regular osd item
        clearPending = false; // Lebenszeichen! OSD ist offen.
        int idx = ev.number;
        if (idx < 0)
            return nlohmann::json();

        if (idx >= (int)osdItems.size())
        {
            osdItems.resize(idx + 1, "");
        }

        if (osdItems[idx] != ev.name)
        {
            osdItems[idx] = ev.name;
            osdItemsChanged = true;
        }
        lastOsdActivity = std::chrono::steady_clock::now();
        return nlohmann::json();
    }

    case eEventType::OsdCurrentItem:
    {
        currentFocusIndex = ev.number;
        lastOsdActivity = std::chrono::steady_clock::now();
        focusChanged = true; // Nur Flag setzen, kein Broadcast!
        return nlohmann::json();
    }

    case eEventType::OsdHelpKeys:
    {
        clearPending = false; // Lebenszeichen!
        std::stringstream ss(ev.name);
        std::string segment;
        int i = 0;

        // Erstmal alle 4 nullen, falls der neue String kürzer ist
        for (int j = 0; j < 4; ++j)
            osdHelp[j] = "";

        while (std::getline(ss, segment, '|') && i < 4)
        {
            osdHelp[i] = segment;
            i++;
        }

        osdHelpChanged = true;
        lastOsdActivity = std::chrono::steady_clock::now();
        return nlohmann::json();
    }

    default:
        j["type"] = "unknown";
        j["name"] = ev.name;
        j["filename"] = ev.fileName;
        j["number"] = ev.number;
        j["status"] = ev.status;
        return j;
    }
    return j;
}

void cWebsocketThread::SendInitialState(struct mg_connection *c)
{
    json j;
    j["type"] = "initial_full_state";

    bool isAnythingRecording = false;
    int32_t nRecordings = 0;
    int32_t nTimers = 0;
    {
        LOCK_TIMERS_READ;
        for (const cTimer *timer = Timers->First(); timer; timer = Timers->Next(timer))
        {
            if (timer->Recording())
            {
                isAnythingRecording = true;
                nRecordings++;
            }
            nTimers++;
        }
    }
    j["is_recording"] = isAnythingRecording;
    j["active_recordings"] = nRecordings;
    j["n_timer"] = nTimers;

    {
        cDevice *primary = cDevice::PrimaryDevice();
        if (primary)
        {
            j["volume"] = primary->IsMute() ? 0 : primary->CurrentVolume();
        }
        else
        {
            j["volume"] = cDevice::CurrentVolume();
        }
    }

    {
        LOCK_CHANNELS_READ;
        const cChannel *channel = Channels->GetByNumber(cDevice::PrimaryDevice()->CurrentChannel());
        if (channel)
        {
            DeviceEvent ev(eEventType::ChannelChange, channel->Name(), "", channel->Number());
            j["current_display"] = BuildStatusJson(ev); // Nutzt deine EPG-Logik
        }
    }

    {
        cMutexLock ControlMutexLock;
        cControl *control = cControl::Control(ControlMutexLock);

        if (control)
        {
            j["replaying"] = true;

            // try to read the info file of the recording
            const char *fileName = nullptr;
            cReplayControl *replayControl = dynamic_cast<cReplayControl *>(control);
            if (replayControl)
            {
                const cRecording *recording = replayControl->GetRecording();
                if (recording)
                {
                    fileName = recording->FileName();
                }
            }
            std::string name = "Wiedergabe";
            std::string fName = "";

            if (fileName)
            {
                fName = fileName;
                cRecording rec(fileName);
                name = rec.Name();
            }

            DeviceEvent ev(eEventType::ReplayStart, name, fName, 0);
            j["current_display"] = BuildStatusJson(ev);
            int current = 0, total = 0;
            if (control->GetIndex(current, total))
            {
                double fps = control->FramesPerSecond();
                if (fps <= 0)
                    fps = 25.0; // Fallback

                j["current_display"]["recording"]["current"] = (int)(current / fps);
                j["current_display"]["recording"]["total"] = (int)(total / fps);
                j["current_display"]["recording"]["progress"] = (total > 0) ? (current * 100 / total) : 0;
            }
        }
        else
        {
            j["replaying"] = false;
            std::string cName;
            int cNum = 0;
            {
                LOCK_CHANNELS_READ;
                const cChannel *channel = Channels->GetByNumber(cDevice::PrimaryDevice()->CurrentChannel());
                if (channel)
                {
                    cName = channel->Name();
                    cNum = channel->Number();
                }
            }

            if (cNum > 0)
            {
                DeviceEvent ev(eEventType::ChannelChange, cName, "", cNum);
                j["current_display"] = BuildStatusJson(ev);
            }
        }
    }

    std::string s = j.dump();
    mg_ws_send(c, s.c_str(), s.size(), WEBSOCKET_OP_TEXT);
}

void cWebsocketThread::BroadcastTimerStatus(void)
{
    json j;
    j["type"] = "timer_status_update";

    int32_t nRecordings = 0;
    int32_t nTimers = 0;
    bool isAnythingRecording = false;

    {
        LOCK_TIMERS_READ;
        for (const cTimer *timer = Timers->First(); timer; timer = Timers->Next(timer))
        {
            if (timer->Recording())
            {
                isAnythingRecording = true;
                nRecordings++;
            }
            nTimers++;
        }
    }

    j["is_recording"] = isAnythingRecording;
    j["active_recordings"] = nRecordings;
    j["n_timer"] = nTimers;

    std::string s = j.dump();
    for (struct mg_connection *c = mgr.conns; c != NULL; c = c->next)
    {
        if (c->is_websocket)
            mg_ws_send(c, s.c_str(), s.size(), WEBSOCKET_OP_TEXT);
    }
}

void cWebsocketThread::BroadcastJson(const DeviceEvent &ev)
{
    std::string s = BuildStatusJson(ev).dump();
    for (struct mg_connection *c = mgr.conns; c != NULL; c = c->next)
    {
        if (c->is_websocket)
            mg_ws_send(c, s.c_str(), s.size(), WEBSOCKET_OP_TEXT);
    }
}

void cWebsocketThread::processEvent(const DeviceEvent &ev)
{
    // 1. Fokus-Events direkt abhandeln (Performance)
    if (ev.type == eEventType::OsdCurrentItem)
    {
        currentFocusIndex = ev.number;
        focusChanged = true;
        lastOsdActivity = std::chrono::steady_clock::now();
    }
    // 2. Alle anderen Events durch BuildStatusJson schicken
    else
    {
        json j = BuildStatusJson(ev);

        // WICHTIG: BuildStatusJson setzt intern osdItemsChanged, osdHelpChanged etc.
        // Wir senden hier NUR, wenn es KEIN OSD-Event war (z.B. ChannelChange, Volume).
        // OSD-Events geben ein leeres JSON zurück und werden später gesammelt gesendet.
        if (!j.empty())
        {
            BroadcastJson(j);
        }
    }
}

void cWebsocketThread::Action()
{
    mg_mgr_init(&mgr);
    mgr.userdata = this;
    std::string url = "ws://0.0.0.0:" + std::to_string(port);
    if (!mg_http_listen(&mgr, url.c_str(), on_connect_callback, NULL))
    {
        esyslog("websocket-plugin: ERROR during startup");
        return;
    }
    UpdateLogoCache();

    bool isReplaying = false;
    auto lastPosUpdate = std::chrono::steady_clock::now();
    auto lastEpgUpdate = std::chrono::steady_clock::now();

    while (Running())
    {
        mg_mgr_poll(&mgr, 0);

        // 1. Hole das erste Event (blockierend max 20ms)
        auto optEv = queue.pop_with_timeout(20);
        if (optEv)
        {
            // TRICK: Wenn es ein Fokus-Event ist, warten wir winzige 15ms.
            // In dieser Zeit füllt der VDR die Queue mit weiteren Fokus-Events vom Scrollen.
            if (optEv->type == eEventType::OsdCurrentItem)
            {
                cCondWait::SleepMs(15);
            }

            lastQueueActivity = std::chrono::steady_clock::now();
            processEvent(*optEv);

            // 2. Jetzt saugen wir ALLES leer, was sich in den 15ms angesammelt hat.
            // Wenn 10 Fokus-Events drin liegen, werden sie hier in Mikrosekunden
            // verarbeitet (nur die Variable gesetzt), OHNE zu senden!
            while (auto nextEv = queue.pop_with_timeout(0))
            {
                processEvent(*nextEv);
            }
        }

        // 2. Zeitberechnung (Immer ausführen!)
        auto now = std::chrono::steady_clock::now();
        auto elapsedQueue = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastQueueActivity).count();
        auto elapsedOsd = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastOsdActivity).count();
        auto elapsedLastList = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastListSent).count();

        // 3. BROADCAST ENTSCHEIDUNG
        if (queue.empty())
        {
            // A: FOKUS (Balken)
            if (focusChanged && !osdItemsChanged && elapsedOsd >= 40 && elapsedLastList > 150)
            {
                focusChanged = false;
                BroadcastJson(nlohmann::json({{"type", "osd"}, {"sub", "focus"}, {"index", currentFocusIndex}}));
            }
            // B: LISTE ODER TEXT-OSD
            else if (osdItemsChanged || osdHelpChanged)
            {
                if (!osdItems.empty() && !osdTitle.empty() && !clearPending)
                {
                    int requiredIdle = (osdItems.size() > 50) ? 600 : 100;
                    if (elapsedQueue >= requiredIdle)
                    {
                        osdItemsChanged = false;
                        osdHelpChanged = false;
                        focusChanged = false;
                        lastListSent = now;

                        nlohmann::json j = {
                            {"type", "osd"},
                            {"sub", "list"},
                            {"items", osdItems},
                            {"title", osdTitle},
                            {"focus", currentFocusIndex},
                            {"red", osdHelp[0]},
                            {"green", osdHelp[1]},
                            {"yellow", osdHelp[2]},
                            {"blue", osdHelp[3]}};
                        BroadcastJson(j);
                    }
                }
                else if (clearPending)
                {
                    // Wenn ein Clear ansteht, verwerfen wir Inhalts-Updates für diese Runde.
                    // So kann Block C nach 250ms "Stille" feuern.
                    osdItemsChanged = false;
                    osdHelpChanged = false;
                }
            }
            // C: CLEAR (Schließen) - Hat jetzt Vorrang vor Block B
            else if (clearPending && elapsedOsd >= 250)
            {
                clearPending = false;
                osdItems.clear();
                osdTitle = "";
                BroadcastJson(nlohmann::json({{"type", "osd"}, {"sub", "clear"}}));
                isyslog("websocket-plugin: OSD CLOSED AT CLIENT");
            }
        }

        // 4. REPLAY / POS / EPG Watchdog
        if (isReplaying)
        {

            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPosUpdate).count() >= 1000)
            {
                int current = 0, total = 0;
                bool play = true, forward = true;
                int speed = 0;
                double fps = 25.0;
                bool hasData = false;

                {
                    cMutexLock ControlMutexLock;
                    cControl *control = cControl::Control(ControlMutexLock);
                    if (control && control->GetIndex(current, total))
                    {
                        control->GetReplayMode(play, forward, speed);
                        fps = control->FramesPerSecond();
                        if (fps <= 0)
                            fps = 25.0;
                        hasData = true;
                    }
                }

                if (hasData)
                {
                    json jpos = {
                        {"type", "pos"},
                        {"current", (int)(current / fps)},
                        {"total", (int)(total / fps)},
                        {"play", play},
                        {"forward", forward},
                        {"speed", speed}};
                    std::string s = jpos.dump();
                    for (struct mg_connection *c = mgr.conns; c != NULL; c = c->next)
                        if (c->is_websocket)
                            mg_ws_send(c, s.c_str(), s.size(), WEBSOCKET_OP_TEXT);
                }
                lastPosUpdate = now;
            }
        }
        else
        {
            bool isOsdActive = !osdItems.empty() || !osdTitle.empty();

            if (!isOsdActive && std::chrono::duration_cast<std::chrono::seconds>(now - lastEpgUpdate).count() >= 60)
            {
                {
                    std::string cName;
                    int cNum = 0;
                    {
                        LOCK_CHANNELS_READ;
                        const cChannel *channel = Channels->GetByNumber(cDevice::PrimaryDevice()->CurrentChannel());
                        if (channel)
                        {
                            cName = channel->Name();
                            cNum = channel->Number();
                        }
                    }

                    if (cNum > 0)
                    {
                        DeviceEvent ev(eEventType::ChannelChange, cName, "", cNum);
                        BroadcastJson(ev);
                    }
                    lastEpgUpdate = now;
                }
            }
        }
        if (!optEv)
        {
            cCondWait::SleepMs(5);
        }
    }
    mg_mgr_free(&mgr);
}

void cWebsocketThread::BroadcastJson(const nlohmann::json &j)
{
    if (j.is_null() || (j.is_object() && j.empty()))
        return;

    std::string s = j.dump();
    for (struct mg_connection *c = mgr.conns; c != NULL; c = c->next)
    {
        if (c->is_websocket)
        {
            mg_ws_send(c, s.c_str(), s.size(), WEBSOCKET_OP_TEXT);
        }
    }
}
