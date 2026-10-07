#include "pch.h"
#include "Gateway.h"
#include "Json.h"
#include "Rest.h"
#include "../MemLog.h"

using namespace winrt;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;
using namespace Windows::Networking::Sockets;
using namespace Windows::Storage::Streams;
using namespace Windows::System::Threading;

namespace DiscordWin3::Discord
{
    namespace
    {
        constexpr wchar_t DefaultGateway[] = L"wss://gateway.discord.gg";

        enum Op
        {
            Dispatch = 0,
            Heartbeat = 1,
            Identify = 2,
            Resume = 6,
            ReconnectOp = 7,
            InvalidSession = 9,
            Hello = 10,
            HeartbeatAck = 11,
        };

        // READY / GUILD_CREATE are megabytes of JSON and Windows.Data.Json costs ~27x the text in COM objects.
        // Cut the subtrees this client never reads *before* parsing (plain text scan, no allocation per value).
        bool IsStripped(std::wstring_view key)
        {
            static constexpr std::wstring_view Keys[] = {
                L"experiments", L"guild_experiments", L"stickers", L"guild_scheduled_events", L"embedded_activities",
                L"application_command_counts", L"soundboard_sounds", L"connected_accounts", L"consents", L"tutorial",
                L"geo_ordered_rtc_regions", L"notes", L"game_relationships", L"broadcaster_user_ids", L"auth",
                L"analytics_token", L"feature_settings", L"notification_settings", L"user_settings_proto",
                L"features", L"incidents_data", L"home_header", L"splash", L"discovery_splash", L"banner",
                L"threads", L"stage_instances", L"activity_instances", L"linked_users", L"guild_join_requests",
                L"pending_payments", L"explicit_content_scan_version", L"static_client_session_id",
                L"auth_session_id_hash", L"avatar_decoration_data", L"collectibles", L"display_name_styles",
                L"profile_themes_experiment_bucket",
            };
            for (auto k : Keys) if (k == key) return true;
            return false;
        }

        size_t SkipString(std::wstring_view s, size_t i)   // i at opening quote -> index after closing quote
        {
            for (++i; i < s.size(); ++i)
            {
                if (s[i] == L'\\') { ++i; continue; }
                if (s[i] == L'"') return i + 1;
            }
            return s.size();
        }

        size_t SkipValue(std::wstring_view s, size_t i)    // i at first char of a value -> index after it
        {
            if (i >= s.size()) return i;
            if (s[i] == L'"') return SkipString(s, i);
            if (s[i] == L'{' || s[i] == L'[')
            {
                int depth = 0;
                for (; i < s.size(); ++i)
                {
                    wchar_t c = s[i];
                    if (c == L'"') { i = SkipString(s, i) - 1; continue; }
                    if (c == L'{' || c == L'[') ++depth;
                    else if ((c == L'}' || c == L']') && --depth == 0) return i + 1;
                }
                return s.size();
            }
            while (i < s.size() && s[i] != L',' && s[i] != L'}' && s[i] != L']') ++i;   // number / true / null
            return i;
        }

        std::wstring StripUnusedKeys(std::wstring_view s)
        {
            std::wstring out;
            out.reserve(s.size());
            size_t i = 0;
            while (i < s.size())
            {
                wchar_t c = s[i];
                if (c != L'"')
                {
                    out.push_back(c);
                    ++i;
                    continue;
                }
                size_t end = SkipString(s, i);
                size_t after = end;
                while (after < s.size() && iswspace(s[after])) ++after;
                bool isKey = after < s.size() && s[after] == L':';
                if (!isKey || !IsStripped(s.substr(i + 1, end - i - 2)))
                {
                    out.append(s.substr(i, end - i));
                    i = end;
                    continue;
                }
                // Drop `"key": value` and one adjacent comma.
                size_t v = after + 1;
                while (v < s.size() && iswspace(s[v])) ++v;
                i = SkipValue(s, v);
                while (!out.empty() && iswspace(out.back())) out.pop_back();
                if (!out.empty() && out.back() == L',')
                {
                    out.pop_back();
                }
                else
                {
                    while (i < s.size() && iswspace(s[i])) ++i;
                    if (i < s.size() && s[i] == L',') ++i;
                }
            }
            return out;
        }

        JsonObject Payload(int op, IJsonValue const& d)
        {
            JsonObject p;
            p.Insert(L"op", JsonValue::CreateNumberValue(op));
            p.Insert(L"d", d ? d : JsonValue::CreateNullValue());
            return p;
        }
    }

    Gateway::Gateway(std::wstring token, DispatchHandler onDispatch, StatusHandler onStatus)
        : m_token(std::move(token)), m_onDispatch(std::move(onDispatch)), m_onStatus(std::move(onStatus))
    {
    }

    Gateway::~Gateway()
    {
        std::lock_guard guard{ m_lock };
        m_stopped = true;
        Teardown();
    }

    void Gateway::Start()
    {
        m_onStatus(GatewayStatus::Connecting);
        Connect(false);
    }

    void Gateway::Stop()
    {
        std::lock_guard guard{ m_lock };
        m_stopped = true;
        Teardown();
    }

    void Gateway::RequestGuildMembers(std::wstring guildId, std::vector<std::wstring> userIds)
    {
        // Sending blocks on the socket, so never do it on the UI thread.
        [](std::weak_ptr<Gateway> weak, std::wstring guildId, std::vector<std::wstring> userIds) -> fire_and_forget
        {
            co_await resume_background();
            auto strong = weak.lock();
            if (!strong)
            {
                co_return;
            }
            for (size_t start = 0; start < userIds.size(); start += 100)
            {
                JsonArray ids;
                for (size_t i = start; i < std::min(start + 100, userIds.size()); ++i)
                {
                    ids.Append(JsonValue::CreateStringValue(userIds[i]));
                }
                JsonObject d;
                d.Insert(L"guild_id", JsonValue::CreateStringValue(guildId));
                d.Insert(L"user_ids", ids);
                d.Insert(L"presences", JsonValue::CreateBooleanValue(false));
                std::lock_guard guard{ strong->m_lock };
                strong->Send(Payload(8, d));
            }
        }(weak_from_this(), std::move(guildId), std::move(userIds));
    }

    void Gateway::Teardown()
    {
        ++m_generation;
        if (m_heartbeat)
        {
            m_heartbeat.Cancel();
            m_heartbeat = nullptr;
        }
        if (m_socket)
        {
            try
            {
                // 4000 (not 1000) keeps the session resumable on Discord's side.
                m_socket.Close(4000, L"reconnect");
            }
            catch (...)
            {
            }
            m_socket = nullptr;
        }
    }

    fire_and_forget Gateway::Connect(bool resume)
    {
        auto self = shared_from_this();
        co_await resume_background();

        MessageWebSocket socket;
        uint64_t generation = 0;
        std::wstring url;
        {
            std::lock_guard guard{ m_lock };
            if (m_stopped)
            {
                co_return;
            }
            Teardown();
            generation = ++m_generation;
            m_socket = socket;
            m_resuming = resume && !m_sessionId.empty();
            url = (m_resuming && !m_resumeUrl.empty() ? m_resumeUrl : std::wstring{ DefaultGateway }) + L"/?v=9&encoding=json";
        }

        socket.Control().MessageType(SocketMessageType::Utf8);
        try
        {
            socket.SetRequestHeader(L"User-Agent", UserAgent());
        }
        catch (...)
        {
        }

        std::weak_ptr<Gateway> weak = self;
        socket.MessageReceived([weak, generation](MessageWebSocket const&, MessageWebSocketMessageReceivedEventArgs const& args)
        {
            auto strong = weak.lock();
            if (!strong)
            {
                return;
            }
            hstring text;
            try
            {
                auto reader = args.GetDataReader();
                reader.UnicodeEncoding(UnicodeEncoding::Utf8);
                text = reader.ReadString(reader.UnconsumedBufferLength());
            }
            catch (...)
            {
                strong->OnClosed(0, generation);
                return;
            }
            strong->OnText(text, generation);
        });
        socket.Closed([weak, generation](IWebSocket const&, WebSocketClosedEventArgs const& args)
        {
            if (auto strong = weak.lock())
            {
                strong->OnClosed(args.Code(), generation);
            }
        });

        try
        {
            co_await socket.ConnectAsync(Uri{ url });
        }
        catch (...)
        {
            OnClosed(0, generation);
        }
    }

    void Gateway::OnText(hstring const& text, uint64_t generation)
    {
        bool big = text.size() > 64 * 1024;
        if (big) ::DiscordWin3::MemLog(L"gateway: before parse", text.size());
        auto doc = Slim::Document::Parse(big ? StripUnusedKeys(text) : std::wstring{ text });
        if (!doc)
        {
            return;
        }
        if (big) ::DiscordWin3::MemLog(L"gateway: after parse", doc->Nodes().size());
        auto payload = doc->Root();

        int op = static_cast<int>(Json::Num(payload, L"op", -1));
        DispatchEvent event;
        event.doc = doc;
        {
            std::lock_guard guard{ m_lock };
            if (generation != m_generation || m_stopped)
            {
                return;
            }

            if (auto s = payload[L"s"]; s.IsNumber())
            {
                m_seq = static_cast<int64_t>(s.Num());
            }

            switch (op)
            {
            case Hello:
            {
                auto d = Json::Obj(payload, L"d");
                StartHeartbeat(static_cast<int>(Json::Num(d, L"heartbeat_interval", 41250)));
                if (m_resuming)
                {
                    SendResume();
                }
                else
                {
                    SendIdentify();
                }
                return;
            }
            case HeartbeatAck:
                m_acked = true;
                return;
            case Heartbeat:
                SendHeartbeat();
                return;
            case ReconnectOp:
                Reconnect(true);
                return;
            case InvalidSession:
            {
                bool resumable = payload[L"d"].Bool(false);
                if (!resumable)
                {
                    m_sessionId.clear();
                    m_seq = -1;
                }
                Reconnect(resumable);
                return;
            }
            case Dispatch:
                event.type = Json::Str(payload, L"t");
                event.d = Json::Obj(payload, L"d");
                if (event.type == L"READY")
                {
                    m_sessionId = Json::Str(event.d, L"session_id");
                    m_resumeUrl = Json::Str(event.d, L"resume_gateway_url");
                    m_failures = 0;
                }
                else if (event.type == L"RESUMED")
                {
                    m_failures = 0;
                }
                break;
            default:
                return;
            }
        }

        if (event.type == L"READY" || event.type == L"RESUMED")
        {
            m_onStatus(GatewayStatus::Connected);
        }
        if (!event.d)
        {
            return;
        }
        // Message events (small) are still consumed through Windows.Data.Json by the message code.
        if (event.type.starts_with(L"MESSAGE_") || event.type == L"TYPING_START")
        {
            JsonObject json{ nullptr };
            if (JsonObject::TryParse(event.d.Raw(), json)) event.json = json;
        }
        m_onDispatch(event);
    }

    void Gateway::OnClosed(uint16_t code, uint64_t generation)
    {
        std::lock_guard guard{ m_lock };
        if (generation != m_generation || m_stopped)
        {
            return;
        }

        // 4004: bad token. 4010-4014: invalid shard / intents / API version -> not recoverable.
        if (code == 4004 || (code >= 4010 && code <= 4014))
        {
            m_stopped = true;
            Teardown();
            m_onStatus(GatewayStatus::AuthFailed);
            return;
        }

        bool canResume = code != 4007 && code != 4009 && !m_sessionId.empty();
        if (!canResume)
        {
            m_sessionId.clear();
            m_seq = -1;
        }
        Reconnect(canResume);
    }

    void Gateway::Reconnect(bool resume)
    {
        Teardown();
        m_onStatus(GatewayStatus::Reconnecting);

        int failures = std::min(m_failures++, 5);
        auto delay = std::chrono::milliseconds(failures == 0 ? 500 : (1000 << failures) + (GetTickCount() % 1000));

        std::weak_ptr<Gateway> weak = weak_from_this();
        [](std::weak_ptr<Gateway> weak, bool resume, std::chrono::milliseconds delay) -> fire_and_forget
        {
            co_await resume_after(delay);
            if (auto strong = weak.lock())
            {
                strong->Connect(resume);
            }
        }(weak, resume, delay);
    }

    void Gateway::StartHeartbeat(int intervalMs)
    {
        m_acked = true;
        uint64_t generation = m_generation;
        std::weak_ptr<Gateway> weak = weak_from_this();
        m_heartbeat = ThreadPoolTimer::CreatePeriodicTimer([weak, generation](ThreadPoolTimer const&)
        {
            auto strong = weak.lock();
            if (!strong)
            {
                return;
            }
            std::lock_guard guard{ strong->m_lock };
            if (generation != strong->m_generation || strong->m_stopped)
            {
                return;
            }
            if (!strong->m_acked.exchange(false))
            {
                // No ACK since the last beat: zombied connection.
                strong->Reconnect(true);
                return;
            }
            strong->SendHeartbeat();
        }, std::chrono::milliseconds(intervalMs));
    }

    void Gateway::Send(JsonObject const& payload)
    {
        if (!m_socket)
        {
            return;
        }
        try
        {
            DataWriter writer;
            writer.UnicodeEncoding(UnicodeEncoding::Utf8);
            writer.WriteString(payload.Stringify());
            m_socket.SendFinalFrameAsync(writer.DetachBuffer()).get();
        }
        catch (...)
        {
            // The Closed event drives reconnection.
        }
    }

    void Gateway::SendHeartbeat()
    {
        int64_t seq = m_seq;
        Send(Payload(Heartbeat, seq < 0 ? JsonValue::CreateNullValue() : JsonValue::CreateNumberValue(static_cast<double>(seq))));
    }

    void Gateway::SendIdentify()
    {
        JsonObject presence;
        presence.Insert(L"status", JsonValue::CreateStringValue(L"online"));
        presence.Insert(L"since", JsonValue::CreateNumberValue(0));
        presence.Insert(L"activities", JsonArray{});
        presence.Insert(L"afk", JsonValue::CreateBooleanValue(false));

        JsonObject d;
        d.Insert(L"token", JsonValue::CreateStringValue(m_token));
        // 16 = DEDUPE_USER_OBJECTS: every user object is sent once in READY.users instead of per member / channel.
        d.Insert(L"capabilities", JsonValue::CreateNumberValue(16));
        d.Insert(L"properties", ClientProperties());
        d.Insert(L"presence", presence);
        d.Insert(L"compress", JsonValue::CreateBooleanValue(false));
        Send(Payload(Identify, d));
    }

    void Gateway::SendResume()
    {
        JsonObject d;
        d.Insert(L"token", JsonValue::CreateStringValue(m_token));
        d.Insert(L"session_id", JsonValue::CreateStringValue(m_sessionId));
        d.Insert(L"seq", JsonValue::CreateNumberValue(static_cast<double>(m_seq.load())));
        Send(Payload(Resume, d));
    }
}

namespace DiscordWin3::Discord
{
    void Gateway::SendOp(int op, JsonObject d)
    {
        [](std::weak_ptr<Gateway> weak, int op, JsonObject d) -> fire_and_forget
        {
            co_await resume_background();
            if (auto strong = weak.lock())
            {
                std::lock_guard guard{ strong->m_lock };
                JsonObject p;
                p.Insert(L"op", JsonValue::CreateNumberValue(op));
                p.Insert(L"d", d);
                strong->Send(p);
            }
        }(weak_from_this(), op, std::move(d));
    }

    void Gateway::SubscribeMemberList(std::wstring guildId, std::wstring channelId)
    {
        JsonArray range;
        range.Append(JsonValue::CreateNumberValue(0));
        range.Append(JsonValue::CreateNumberValue(99));
        JsonArray ranges;
        ranges.Append(range);
        JsonObject channels;
        channels.Insert(channelId, ranges);

        JsonObject subscription;
        subscription.Insert(L"typing", JsonValue::CreateBooleanValue(true));
        subscription.Insert(L"activities", JsonValue::CreateBooleanValue(true));
        subscription.Insert(L"threads", JsonValue::CreateBooleanValue(false));
        subscription.Insert(L"channels", channels);

        JsonObject subscriptions;
        subscriptions.Insert(guildId, subscription);
        JsonObject d;
        d.Insert(L"subscriptions", subscriptions);
        SendOp(37, d);
    }
}
