#include "pch.h"
#include "Gateway.h"
#include "Json.h"
#include "Rest.h"

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
        JsonObject payload;
        if (!JsonObject::TryParse(text, payload))
        {
            return;
        }

        int op = static_cast<int>(Json::Num(payload, L"op", -1));
        std::wstring type;
        JsonObject data{ nullptr };
        {
            std::lock_guard guard{ m_lock };
            if (generation != m_generation || m_stopped)
            {
                return;
            }

            if (auto s = Json::Get(payload, L"s"))
            {
                m_seq = static_cast<int64_t>(s.GetNumber());
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
                auto d = Json::Get(payload, L"d");
                bool resumable = d && d.ValueType() == JsonValueType::Boolean && d.GetBoolean();
                if (!resumable)
                {
                    m_sessionId.clear();
                    m_seq = -1;
                }
                Reconnect(resumable);
                return;
            }
            case Dispatch:
                type = Json::Str(payload, L"t");
                data = Json::Obj(payload, L"d");
                if (type == L"READY")
                {
                    m_sessionId = Json::Str(data, L"session_id");
                    m_resumeUrl = Json::Str(data, L"resume_gateway_url");
                    m_failures = 0;
                }
                else if (type == L"RESUMED")
                {
                    m_failures = 0;
                }
                break;
            default:
                return;
            }
        }

        if (type == L"READY" || type == L"RESUMED")
        {
            m_onStatus(GatewayStatus::Connected);
        }
        if (data)
        {
            m_onDispatch(type, data);
        }
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
        d.Insert(L"capabilities", JsonValue::CreateNumberValue(0));
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
