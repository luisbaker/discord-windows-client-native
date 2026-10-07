#include "pch.h"
#include "RemoteAuth.h"
#include "Json.h"
#include "Rest.h"
#include "../Strings.h"

using namespace winrt;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;
using namespace Windows::Networking::Sockets;
using namespace Windows::Security::Cryptography;
using namespace Windows::Security::Cryptography::Core;
using namespace Windows::Storage::Streams;
using namespace Windows::System::Threading;

namespace DiscordWin3::Discord
{
    namespace
    {
        std::wstring Base64Url(IBuffer const& buffer)
        {
            std::wstring s{ CryptographicBuffer::EncodeToBase64String(buffer) };
            for (auto& c : s)
            {
                if (c == L'+') c = L'-';
                else if (c == L'/') c = L'_';
            }
            while (!s.empty() && s.back() == L'=')
            {
                s.pop_back();
            }
            return s;
        }
    }

    RemoteAuth::RemoteAuth(Callbacks callbacks) : m_cb(std::move(callbacks))
    {
    }

    RemoteAuth::~RemoteAuth()
    {
        Stop();
    }

    void RemoteAuth::Stop()
    {
        std::lock_guard guard{ m_lock };
        m_finished = true;
        if (m_heartbeat)
        {
            m_heartbeat.Cancel();
            m_heartbeat = nullptr;
        }
        if (m_socket)
        {
            try { m_socket.Close(1000, L""); } catch (...) {}
            m_socket = nullptr;
        }
    }

    fire_and_forget RemoteAuth::Start()
    {
        auto self = shared_from_this();
        co_await resume_background();

        auto provider = AsymmetricKeyAlgorithmProvider::OpenAlgorithm(AsymmetricAlgorithmNames::RsaOaepSha256());
        m_key = provider.CreateKeyPair(2048);

        MessageWebSocket socket;
        socket.Control().MessageType(SocketMessageType::Utf8);
        try
        {
            // The remote auth gateway rejects connections without a discord.com origin.
            socket.SetRequestHeader(L"Origin", L"https://discord.com");
            socket.SetRequestHeader(L"User-Agent", UserAgent());
        }
        catch (...)
        {
        }

        std::weak_ptr<RemoteAuth> weak = self;
        socket.MessageReceived([weak](MessageWebSocket const&, MessageWebSocketMessageReceivedEventArgs const& args)
        {
            auto strong = weak.lock();
            if (!strong)
            {
                return;
            }
            try
            {
                auto reader = args.GetDataReader();
                reader.UnicodeEncoding(UnicodeEncoding::Utf8);
                strong->OnText(reader.ReadString(reader.UnconsumedBufferLength()));
            }
            catch (hresult_error const& e)
            {
                strong->m_cb.onError(L"QR : " + std::wstring{ e.message() });
            }
        });
        socket.Closed([weak](IWebSocket const&, WebSocketClosedEventArgs const& args)
        {
            auto strong = weak.lock();
            if (!strong)
            {
                return;
            }
            bool finished;
            {
                std::lock_guard guard{ strong->m_lock };
                finished = strong->m_finished;
            }
            if (!finished)
            {
                strong->m_cb.onError(I18n::Fmt(I18n::S::QrClosed, std::to_wstring(args.Code())));
            }
        });

        {
            std::lock_guard guard{ m_lock };
            if (m_finished)
            {
                co_return;
            }
            m_socket = socket;
        }

        try
        {
            co_await socket.ConnectAsync(Uri{ L"wss://remote-auth-gateway.discord.gg/?v=2" });
        }
        catch (hresult_error const& e)
        {
            m_cb.onError(I18n::Fmt(I18n::S::QrConnectFailed, std::wstring{ e.message() }));
        }
    }

    void RemoteAuth::Send(std::wstring_view op, JsonObject extra)
    {
        JsonObject payload = extra ? extra : JsonObject{};
        payload.Insert(L"op", JsonValue::CreateStringValue(op));

        MessageWebSocket socket{ nullptr };
        {
            std::lock_guard guard{ m_lock };
            socket = m_socket;
        }
        if (!socket)
        {
            return;
        }
        DataWriter writer;
        writer.UnicodeEncoding(UnicodeEncoding::Utf8);
        writer.WriteString(payload.Stringify());
        socket.SendFinalFrameAsync(writer.DetachBuffer()).get();
    }

    IBuffer RemoteAuth::Decrypt(std::wstring const& base64)
    {
        auto data = CryptographicBuffer::DecodeFromBase64String(base64);
        return CryptographicEngine::Decrypt(m_key, data, nullptr);
    }

    void RemoteAuth::OnText(hstring const& text)
    {
        JsonObject msg;
        if (!JsonObject::TryParse(text, msg))
        {
            return;
        }
        auto op = Json::Str(msg, L"op");

        if (op == L"hello")
        {
            int interval = static_cast<int>(Json::Num(msg, L"heartbeat_interval", 41250));
            std::weak_ptr<RemoteAuth> weak = weak_from_this();
            {
                std::lock_guard guard{ m_lock };
                m_heartbeat = ThreadPoolTimer::CreatePeriodicTimer([weak](ThreadPoolTimer const&)
                {
                    if (auto strong = weak.lock())
                    {
                        try { strong->Send(L"heartbeat"); } catch (...) {}
                    }
                }, std::chrono::milliseconds(interval));
            }

            auto spki = m_key.ExportPublicKey(CryptographicPublicKeyBlobType::X509SubjectPublicKeyInfo);
            JsonObject init;
            init.Insert(L"encoded_public_key", JsonValue::CreateStringValue(CryptographicBuffer::EncodeToBase64String(spki)));
            Send(L"init", init);
        }
        else if (op == L"nonce_proof")
        {
            auto nonce = Decrypt(Json::Str(msg, L"encrypted_nonce"));
            auto hash = HashAlgorithmProvider::OpenAlgorithm(HashAlgorithmNames::Sha256()).HashData(nonce);
            JsonObject proof;
            proof.Insert(L"proof", JsonValue::CreateStringValue(Base64Url(hash)));
            Send(L"nonce_proof", proof);
        }
        else if (op == L"pending_remote_init")
        {
            m_cb.onQrCode(L"https://discord.com/ra/" + Json::Str(msg, L"fingerprint"));
        }
        else if (op == L"pending_ticket")
        {
            // Payload: "<id>:<discriminator>:<avatar hash>:<username>"
            auto plain = CryptographicBuffer::ConvertBinaryToString(BinaryStringEncoding::Utf8,
                Decrypt(Json::Str(msg, L"encrypted_user_payload")));
            std::wstring s{ plain };
            size_t pos = 0;
            for (int i = 0; i < 3 && pos != std::wstring::npos; ++i)
            {
                pos = s.find(L':', pos == 0 && i == 0 ? 0 : pos + 1);
            }
            m_cb.onScanned(pos == std::wstring::npos ? s : s.substr(pos + 1));
        }
        else if (op == L"pending_login")
        {
            ExchangeTicket(Json::Str(msg, L"ticket"));
        }
        else if (op == L"pending_finish")
        {
            // Older protocol revision: token delivered directly over the socket.
            auto token = CryptographicBuffer::ConvertBinaryToString(BinaryStringEncoding::Utf8,
                Decrypt(Json::Str(msg, L"encrypted_token")));
            {
                std::lock_guard guard{ m_lock };
                m_finished = true;
            }
            m_cb.onToken(std::wstring{ token });
        }
        else if (op == L"cancel")
        {
            m_cb.onError(I18n::Tr(I18n::S::QrCancelled));
        }
    }

    fire_and_forget RemoteAuth::ExchangeTicket(std::wstring ticket)
    {
        auto self = shared_from_this();
        try
        {
            Rest anonymous{ L"" };
            JsonObject body;
            body.Insert(L"ticket", JsonValue::CreateStringValue(ticket));
            auto result = co_await anonymous.PostJson(L"/users/@me/remote-auth/login", body);
            auto encrypted = Json::Str(result.GetObject(), L"encrypted_token");
            auto token = CryptographicBuffer::ConvertBinaryToString(BinaryStringEncoding::Utf8, Decrypt(encrypted));
            {
                std::lock_guard guard{ m_lock };
                m_finished = true;
            }
            m_cb.onToken(std::wstring{ token });
        }
        catch (hresult_error const& e)
        {
            m_cb.onError(I18n::Fmt(I18n::S::QrValidateFailed, std::wstring{ e.message() }));
        }
    }
}
