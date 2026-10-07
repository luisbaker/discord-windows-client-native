#include "pch.h"
#include "VoiceConnection.h"

#include "../Discord/Json.h"
#include "../Discord/Rest.h"

#include <ws2tcpip.h>
#include <opus/opus.h>
#include <dave/dave.h>

using namespace winrt;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;
using namespace Windows::Networking::Sockets;
using namespace Windows::Storage::Streams;
using namespace Windows::System::Threading;

namespace DiscordWin3::Voice
{
    namespace
    {
        // Voice gateway v8 opcodes (incl. DAVE 21-31).
        enum Op : int
        {
            Identify = 0, SelectProtocol = 1, Ready = 2, Heartbeat = 3, SessionDescription = 4, Speaking = 5,
            HeartbeatAck = 6, Hello = 8, ClientsConnect = 11, ClientDisconnect = 13,
            DavePrepareTransition = 21, DaveExecuteTransition = 22, DaveTransitionReady = 23, DavePrepareEpoch = 24,
            DaveExternalSender = 25, DaveKeyPackage = 26, DaveProposals = 27, DaveCommitWelcome = 28,
            DaveAnnounceCommit = 29, DaveWelcome = 30, DaveInvalidCommitWelcome = 31,
        };

        constexpr char const* Mode = "aead_aes256_gcm_rtpsize";
        constexpr uint8_t OpusSilence[] = { 0xF8, 0xFF, 0xFE };

        std::string Utf8(std::wstring const& s) { return winrt::to_string(s); }

        int64_t NowMs()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        void PutU16(uint8_t* p, uint16_t v) { p[0] = static_cast<uint8_t>(v >> 8); p[1] = static_cast<uint8_t>(v); }
        void PutU32(uint8_t* p, uint32_t v) { p[0] = static_cast<uint8_t>(v >> 24); p[1] = static_cast<uint8_t>(v >> 16); p[2] = static_cast<uint8_t>(v >> 8); p[3] = static_cast<uint8_t>(v); }
        uint16_t GetU16(uint8_t const* p) { return static_cast<uint16_t>(p[0] << 8 | p[1]); }
        uint32_t GetU32(uint8_t const* p) { return uint32_t{ p[0] } << 24 | uint32_t{ p[1] } << 16 | uint32_t{ p[2] } << 8 | p[3]; }

        struct WinsockInit
        {
            WinsockInit() { WSADATA data; WSAStartup(MAKEWORD(2, 2), &data); }
        };
    }

    VoiceConnection::VoiceConnection(VoiceParams params, Callbacks callbacks)
        : m_params(std::move(params)), m_cb(std::move(callbacks)), m_userIdUtf8(Utf8(m_params.userId))
    {
        static WinsockInit winsock;
        int error = 0;
        m_encoder = opus_encoder_create(AudioEngine::SampleRate, AudioEngine::Channels, OPUS_APPLICATION_VOIP, &error);
        if (m_encoder)
        {
            opus_encoder_ctl(m_encoder, OPUS_SET_BITRATE(64000));
            opus_encoder_ctl(m_encoder, OPUS_SET_INBAND_FEC(1));
            opus_encoder_ctl(m_encoder, OPUS_SET_PACKET_LOSS_PERC(5));
        }
        m_dave = daveSessionCreate(nullptr, nullptr, [](char const*, char const*, void*) {}, nullptr);
        m_encryptor = daveEncryptorCreate();
    }

    VoiceConnection::~VoiceConnection()
    {
        Stop();
        if (m_encoder) opus_encoder_destroy(m_encoder);
        for (auto& [ssrc, decoder] : m_decoders) opus_decoder_destroy(decoder);
        std::lock_guard guard{ m_daveLock };
        for (auto& [user, decryptor] : m_decryptors) daveDecryptorDestroy(decryptor);
        if (m_encryptor) daveEncryptorDestroy(m_encryptor);
        for (auto r : m_ratchets) daveKeyRatchetDestroy(r);
        for (auto r : m_oldRatchets) daveKeyRatchetDestroy(r);
        if (m_dave) daveSessionDestroy(m_dave);
    }

    void VoiceConnection::Start()
    {
        if (m_cb.onState) m_cb.onState(State::Connecting, {});
        Connect();
    }

    void VoiceConnection::Stop()
    {
        if (m_stopped.exchange(true)) return;
        if (m_heartbeat) m_heartbeat.Cancel();
        if (m_ticker) m_ticker.Cancel();
        m_audio.Stop();
        {
            std::lock_guard guard{ m_sendLock };
            if (m_socket)
            {
                try { m_socket.Close(1000, L""); } catch (...) {}
                m_socket = nullptr;
            }
        }
        if (m_udp != INVALID_SOCKET)
        {
            closesocket(m_udp);   // unblocks recv() in the receive thread
            m_udp = INVALID_SOCKET;
        }
        if (m_receiver.joinable()) m_receiver.join();
    }

    void VoiceConnection::SetMuted(bool muted) { m_audio.SetMuted(muted); }
    void VoiceConnection::SetDeafened(bool deafened) { m_audio.SetDeafened(deafened); }

    void VoiceConnection::Fail(std::wstring const& detail)
    {
        if (m_stopped) return;
        m_state = State::Failed;
        if (m_cb.onState) m_cb.onState(State::Failed, detail);
    }

    // ------------------------------------------------------------------ voice gateway

    fire_and_forget VoiceConnection::Connect()
    {
        auto self = shared_from_this();
        co_await resume_background();

        MessageWebSocket socket;
        socket.Control().MessageType(SocketMessageType::Utf8);
        std::weak_ptr<VoiceConnection> weak = self;
        socket.MessageReceived([weak](MessageWebSocket const&, MessageWebSocketMessageReceivedEventArgs const& args)
        {
            auto strong = weak.lock();
            if (!strong || strong->m_stopped) return;
            try
            {
                auto reader = args.GetDataReader();
                if (args.MessageType() == SocketMessageType::Binary)
                {
                    std::vector<uint8_t> data(reader.UnconsumedBufferLength());
                    reader.ReadBytes(data);
                    strong->OnBinary(std::move(data));
                }
                else
                {
                    reader.UnicodeEncoding(UnicodeEncoding::Utf8);
                    strong->OnText(std::wstring{ reader.ReadString(reader.UnconsumedBufferLength()) });
                }
            }
            catch (...)
            {
            }
        });
        socket.Closed([weak](IWebSocket const&, WebSocketClosedEventArgs const& args)
        {
            if (auto strong = weak.lock()) strong->Fail(L"voice gateway closed (" + std::to_wstring(args.Code()) + L")");
        });

        {
            std::lock_guard guard{ m_sendLock };
            m_socket = socket;
        }
        try
        {
            co_await socket.ConnectAsync(Uri{ L"wss://" + m_params.endpoint + L"/?v=8" });
        }
        catch (hresult_error const& e)
        {
            Fail(std::wstring{ e.message() });
        }
    }

    void VoiceConnection::SendJson(int op, IJsonValue const& d)
    {
        JsonObject payload;
        payload.Insert(L"op", JsonValue::CreateNumberValue(op));
        payload.Insert(L"d", d);
        std::lock_guard guard{ m_sendLock };
        if (!m_socket) return;
        try
        {
            DataWriter writer;
            writer.UnicodeEncoding(UnicodeEncoding::Utf8);
            writer.WriteString(payload.Stringify());
            m_socket.Control().MessageType(SocketMessageType::Utf8);
            m_socket.SendFinalFrameAsync(writer.DetachBuffer()).get();
        }
        catch (...)
        {
        }
    }

    void VoiceConnection::SendBinary(uint8_t op, uint8_t const* payload, size_t length)
    {
        std::lock_guard guard{ m_sendLock };
        if (!m_socket) return;
        try
        {
            DataWriter writer;
            writer.WriteByte(op);
            writer.WriteBytes({ payload, payload + length });
            m_socket.Control().MessageType(SocketMessageType::Binary);
            m_socket.SendFinalFrameAsync(writer.DetachBuffer()).get();
        }
        catch (...)
        {
        }
    }

    void VoiceConnection::OnText(std::wstring text)
    {
        auto doc = Slim::Document::Parse(std::move(text));
        if (!doc) return;
        auto root = doc->Root();
        if (auto seq = root[L"seq"]; seq.IsNumber()) m_seqAck = static_cast<int>(seq.Num());
        int op = static_cast<int>(Json::Num(root, L"op", -1));
        auto d = root[L"d"];

        switch (op)
        {
        case Hello:
        {
            auto interval = std::chrono::milliseconds(static_cast<int64_t>(Json::Num(d, L"heartbeat_interval", 13750)));
            std::weak_ptr<VoiceConnection> weak = weak_from_this();
            m_heartbeat = ThreadPoolTimer::CreatePeriodicTimer([weak](ThreadPoolTimer const&)
            {
                auto strong = weak.lock();
                if (!strong || strong->m_stopped) return;
                JsonObject hb;
                hb.Insert(L"t", JsonValue::CreateNumberValue(static_cast<double>(NowMs())));
                hb.Insert(L"seq_ack", JsonValue::CreateNumberValue(strong->m_seqAck));
                strong->SendJson(Heartbeat, hb);
            }, interval);

            JsonObject identify;
            identify.Insert(L"server_id", JsonValue::CreateStringValue(m_params.serverId));
            identify.Insert(L"user_id", JsonValue::CreateStringValue(m_params.userId));
            identify.Insert(L"session_id", JsonValue::CreateStringValue(m_params.sessionId));
            identify.Insert(L"token", JsonValue::CreateStringValue(m_params.token));
            identify.Insert(L"max_dave_protocol_version", JsonValue::CreateNumberValue(daveMaxSupportedProtocolVersion()));
            SendJson(Identify, identify);
            break;
        }
        case Ready:
        {
            m_ssrc = static_cast<uint32_t>(Json::Num(d, L"ssrc"));
            std::string externalIp;
            uint16_t externalPort = 0;
            if (!OpenUdp(Utf8(Json::Str(d, L"ip")), static_cast<uint16_t>(Json::Num(d, L"port")), externalIp, externalPort))
            {
                Fail(L"UDP / IP discovery failed");
                return;
            }
            JsonObject data;
            data.Insert(L"address", JsonValue::CreateStringValue(winrt::to_hstring(externalIp)));
            data.Insert(L"port", JsonValue::CreateNumberValue(externalPort));
            data.Insert(L"mode", JsonValue::CreateStringValue(winrt::to_hstring(Mode)));
            JsonObject select;
            select.Insert(L"protocol", JsonValue::CreateStringValue(L"udp"));
            select.Insert(L"data", data);
            SendJson(SelectProtocol, select);
            break;
        }
        case SessionDescription:
        {
            std::vector<uint8_t> key;
            for (auto b : d[L"secret_key"]) key.push_back(static_cast<uint8_t>(b.Num()));
            if (key.size() != 32 || !m_sendCrypto.SetKey(key.data(), key.size()) || !m_recvCrypto.SetKey(key.data(), key.size()))
            {
                Fail(L"bad session key");
                return;
            }
            {
                std::lock_guard guard{ m_daveLock };
                m_daveVersion = static_cast<uint16_t>(Json::Num(d, L"dave_protocol_version"));
                daveEncryptorAssignSsrcToCodec(m_encryptor, m_ssrc, DAVE_CODEC_OPUS);
                if (m_daveVersion > 0) DaveReinit();
                else daveEncryptorSetPassthroughMode(m_encryptor, true);
            }

            // Media is ready: start audio, receiving, speaking indicators and UDP keep-alive.
            m_receiver = std::thread([this] { ReceiveLoop(); });
            std::weak_ptr<VoiceConnection> weak = weak_from_this();
            m_audio.Start([weak](int16_t const* pcm, bool voiced)
            {
                if (auto strong = weak.lock()) strong->OnCapturedFrame(pcm, voiced);
            });
            m_ticker = ThreadPoolTimer::CreatePeriodicTimer([weak](ThreadPoolTimer const&)
            {
                auto strong = weak.lock();
                if (!strong || strong->m_stopped) return;
                // Speaking indicators: someone is "speaking" if audio arrived in the last 300 ms.
                std::vector<std::pair<std::string, bool>> changes;
                {
                    std::lock_guard guard{ strong->m_peersLock };
                    auto now = NowMs();
                    for (auto const& [user, last] : strong->m_lastHeard)
                    {
                        bool speaking = now - last < 300;
                        bool was = strong->m_speakingNow.contains(user);
                        if (speaking != was)
                        {
                            if (speaking) strong->m_speakingNow.insert(user);
                            else strong->m_speakingNow.erase(user);
                            changes.emplace_back(user, speaking);
                        }
                    }
                }
                for (auto const& [user, speaking] : changes)
                {
                    if (strong->m_cb.onSpeaking) strong->m_cb.onSpeaking(winrt::to_hstring(user).c_str(), speaking);
                }
                // NAT keep-alive every ~5 s.
                static thread_local int counter = 0;
                if (++counter % 25 == 0 && strong->m_udp != INVALID_SOCKET)
                {
                    uint8_t keepAlive[8]{};
                    PutU32(keepAlive + 4, static_cast<uint32_t>(counter));
                    send(strong->m_udp, reinterpret_cast<char const*>(keepAlive), sizeof(keepAlive), 0);
                }
            }, std::chrono::milliseconds(200));

            m_state = State::Connected;
            if (m_cb.onState) m_cb.onState(State::Connected, {});
            break;
        }
        case HeartbeatAck:
        {
            // v8 echoes our nonce (a steady-clock timestamp): round trip to the voice server.
            auto sent = static_cast<int64_t>(Json::Num(d, L"t", -1));
            if (sent > 0) m_rtt = static_cast<int>(NowMs() - sent);
            break;
        }
        case Speaking:
        {
            auto user = Utf8(Json::Str(d, L"user_id"));
            auto ssrc = static_cast<uint32_t>(Json::Num(d, L"ssrc"));
            if (!user.empty() && ssrc)
            {
                std::lock_guard guard{ m_peersLock };
                m_ssrcUser[ssrc] = user;
                m_connectedUsers.insert(user);
            }
            break;
        }
        case ClientsConnect:
        {
            std::lock_guard guard{ m_peersLock };
            for (auto id : d[L"user_ids"]) m_connectedUsers.insert(Utf8(id.Str()));
            break;
        }
        case ClientDisconnect:
        {
            auto user = Utf8(Json::Str(d, L"user_id"));
            {
                std::lock_guard guard{ m_peersLock };
                m_connectedUsers.erase(user);
                std::erase_if(m_ssrcUser, [&](auto const& entry) { return entry.second == user; });
            }
            std::lock_guard guard{ m_daveLock };
            if (auto it = m_decryptors.find(user); it != m_decryptors.end())
            {
                daveDecryptorDestroy(it->second);
                m_decryptors.erase(it);
            }
            break;
        }
        case DavePrepareTransition:
        {
            std::lock_guard guard{ m_daveLock };
            auto transition = static_cast<uint16_t>(Json::Num(d, L"transition_id"));
            auto version = static_cast<uint16_t>(Json::Num(d, L"protocol_version"));
            m_pendingPassthrough = version == 0;
            m_pendingTransition = transition;
            if (transition == 0)
            {
                DaveApplyPending();
            }
            else
            {
                JsonObject ready;
                ready.Insert(L"transition_id", JsonValue::CreateNumberValue(transition));
                SendJson(DaveTransitionReady, ready);
            }
            break;
        }
        case DaveExecuteTransition:
        {
            std::lock_guard guard{ m_daveLock };
            if (static_cast<uint16_t>(Json::Num(d, L"transition_id")) == m_pendingTransition) DaveApplyPending();
            break;
        }
        case DavePrepareEpoch:
        {
            std::lock_guard guard{ m_daveLock };
            if (Json::Num(d, L"epoch") == 1)
            {
                m_daveVersion = static_cast<uint16_t>(Json::Num(d, L"protocol_version"));
                DaveReinit();
            }
            break;
        }
        default:
            break;
        }
    }

    void VoiceConnection::OnBinary(std::vector<uint8_t> data)
    {
        // Server -> client binary frames: [seq u16][opcode u8][payload]
        if (data.size() < 3) return;
        m_seqAck = GetU16(data.data());
        uint8_t op = data[2];
        uint8_t const* payload = data.data() + 3;
        size_t length = data.size() - 3;

        std::lock_guard guard{ m_daveLock };
        switch (op)
        {
        case DaveExternalSender:
            m_externalSender.assign(payload, payload + length);
            if (m_daveVersion > 0) daveSessionSetExternalSender(m_dave, payload, length);
            break;
        case DaveProposals:
        {
            auto users = DaveRecognizedUsers();
            std::vector<char const*> ids;
            for (auto const& u : users) ids.push_back(u.c_str());
            uint8_t* commitWelcome = nullptr;
            size_t commitWelcomeLength = 0;
            daveSessionProcessProposals(m_dave, payload, length, ids.data(), ids.size(), &commitWelcome, &commitWelcomeLength);
            if (commitWelcome && commitWelcomeLength) SendBinary(DaveCommitWelcome, commitWelcome, commitWelcomeLength);
            if (commitWelcome) daveFree(commitWelcome);
            break;
        }
        case DaveAnnounceCommit:
        {
            if (length < 2) break;
            uint16_t transition = GetU16(payload);
            auto result = daveSessionProcessCommit(m_dave, payload + 2, length - 2);
            bool ignored = result && daveCommitResultIsIgnored(result);
            bool failed = !result || daveCommitResultIsFailed(result);
            if (result) daveCommitResultDestroy(result);
            if (!ignored) DaveOnCommitOrWelcome(transition, !failed);
            break;
        }
        case DaveWelcome:
        {
            if (length < 2) break;
            uint16_t transition = GetU16(payload);
            auto users = DaveRecognizedUsers();
            std::vector<char const*> ids;
            for (auto const& u : users) ids.push_back(u.c_str());
            auto result = daveSessionProcessWelcome(m_dave, payload + 2, length - 2, ids.data(), ids.size());
            if (result) daveWelcomeResultDestroy(result);
            DaveOnCommitOrWelcome(transition, result != nullptr);
            break;
        }
        default:
            break;
        }
    }

    // ------------------------------------------------------------------ DAVE

    std::vector<std::string> VoiceConnection::DaveRecognizedUsers()
    {
        std::lock_guard guard{ m_peersLock };
        std::vector<std::string> users{ m_connectedUsers.begin(), m_connectedUsers.end() };
        users.push_back(m_userIdUtf8);
        return users;
    }

    void VoiceConnection::DaveReinit()
    {
        // (re)join the MLS group: fresh session state + new key package.
        if (m_daveVersion == 0) return;
        daveSessionReset(m_dave);
        daveSessionInit(m_dave, m_daveVersion, Json::U64(m_params.channelId), m_userIdUtf8.c_str());
        if (!m_externalSender.empty()) daveSessionSetExternalSender(m_dave, m_externalSender.data(), m_externalSender.size());
        DaveSendKeyPackage();
    }

    void VoiceConnection::DaveSendKeyPackage()
    {
        uint8_t* package = nullptr;
        size_t length = 0;
        daveSessionGetMarshalledKeyPackage(m_dave, &package, &length);
        if (package && length) SendBinary(DaveKeyPackage, package, length);
        if (package) daveFree(package);
    }

    void VoiceConnection::DaveOnCommitOrWelcome(uint16_t transitionId, bool ok)
    {
        if (!ok)
        {
            JsonObject invalid;
            invalid.Insert(L"transition_id", JsonValue::CreateNumberValue(transitionId));
            SendJson(DaveInvalidCommitWelcome, invalid);
            DaveReinit();
            return;
        }
        m_pendingRatchets = true;
        m_pendingPassthrough = false;
        m_pendingTransition = transitionId;
        if (transitionId == 0)
        {
            DaveApplyPending();   // initial group: no transition to coordinate
        }
        else
        {
            JsonObject ready;
            ready.Insert(L"transition_id", JsonValue::CreateNumberValue(transitionId));
            SendJson(DaveTransitionReady, ready);
        }
    }

    DAVEDecryptorHandle VoiceConnection::DecryptorFor(std::string const& userId)
    {
        auto it = m_decryptors.find(userId);
        if (it != m_decryptors.end()) return it->second;
        auto decryptor = daveDecryptorCreate();
        if (m_daveVersion == 0)
        {
            daveDecryptorTransitionToPassthroughMode(decryptor, true);
        }
        else if (auto ratchet = daveSessionGetKeyRatchet(m_dave, userId.c_str()))
        {
            daveDecryptorTransitionToKeyRatchet(decryptor, ratchet);
            m_ratchets.push_back(ratchet);
        }
        m_decryptors.emplace(userId, decryptor);
        return decryptor;
    }

    void VoiceConnection::DaveApplyPending()
    {
        if (m_pendingPassthrough)
        {
            daveEncryptorSetPassthroughMode(m_encryptor, true);
            for (auto& [user, decryptor] : m_decryptors) daveDecryptorTransitionToPassthroughMode(decryptor, true);
            m_pendingPassthrough = false;
            return;
        }
        if (!m_pendingRatchets) return;
        m_pendingRatchets = false;

        // New epoch keys: our encryptor + one decryptor per participant. Keep the previous generation alive
        // for the transition window (cryptors do not own ratchets).
        for (auto r : m_oldRatchets) daveKeyRatchetDestroy(r);
        m_oldRatchets = std::move(m_ratchets);
        m_ratchets.clear();

        if (auto self = daveSessionGetKeyRatchet(m_dave, m_userIdUtf8.c_str()))
        {
            daveEncryptorSetKeyRatchet(m_encryptor, self);
            daveEncryptorSetPassthroughMode(m_encryptor, false);
            m_ratchets.push_back(self);
        }
        for (auto const& user : DaveRecognizedUsers())
        {
            if (user == m_userIdUtf8) continue;
            auto ratchet = daveSessionGetKeyRatchet(m_dave, user.c_str());
            if (!ratchet) continue;
            auto it = m_decryptors.find(user);
            auto decryptor = it != m_decryptors.end() ? it->second : m_decryptors.emplace(user, daveDecryptorCreate()).first->second;
            daveDecryptorTransitionToKeyRatchet(decryptor, ratchet);
            m_ratchets.push_back(ratchet);
        }
    }

    // ------------------------------------------------------------------ UDP / RTP

    bool VoiceConnection::OpenUdp(std::string const& ip, uint16_t port, std::string& externalIp, uint16_t& externalPort)
    {
        m_udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (m_udp == INVALID_SOCKET) return false;
        sockaddr_in server{};
        server.sin_family = AF_INET;
        server.sin_port = htons(port);
        if (inet_pton(AF_INET, ip.c_str(), &server.sin_addr) != 1) return false;
        if (connect(m_udp, reinterpret_cast<sockaddr*>(&server), sizeof(server)) != 0) return false;
        DWORD timeout = 2000;
        setsockopt(m_udp, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char const*>(&timeout), sizeof(timeout));

        // IP discovery: type 0x1, length 70, ssrc, 64-byte address, port.
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            uint8_t request[74]{};
            PutU16(request, 0x1);
            PutU16(request + 2, 70);
            PutU32(request + 4, m_ssrc);
            send(m_udp, reinterpret_cast<char const*>(request), sizeof(request), 0);
            uint8_t response[74]{};
            int received = recv(m_udp, reinterpret_cast<char*>(response), sizeof(response), 0);
            if (received == 74 && GetU16(response) == 0x2)
            {
                externalIp.assign(reinterpret_cast<char const*>(response + 8), strnlen(reinterpret_cast<char const*>(response + 8), 64));
                externalPort = GetU16(response + 72);
                timeout = 0;   // blocking receive loop from now on
                setsockopt(m_udp, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char const*>(&timeout), sizeof(timeout));
                return true;
            }
        }
        return false;
    }

    void VoiceConnection::OnCapturedFrame(int16_t const* pcm, bool voiced)
    {
        if (m_stopped || !m_encoder) return;
        if (voiced != m_speaking) SetSpeaking(voiced);
        if (!voiced)
        {
            // Opus convention: a few silence frames when stopping, then nothing (saves bandwidth & CPU).
            if (m_trailingSilence > 0)
            {
                --m_trailingSilence;
                SendOpus(OpusSilence, sizeof(OpusSilence));
            }
            return;
        }
        uint8_t opus[1275];
        int length = opus_encode(m_encoder, pcm, AudioEngine::FrameSamples, opus, sizeof(opus));
        if (length > 0) SendOpus(opus, static_cast<size_t>(length));
    }

    void VoiceConnection::SetSpeaking(bool speaking)
    {
        m_speaking = speaking;
        if (!speaking) m_trailingSilence = 5;
        JsonObject d;
        d.Insert(L"speaking", JsonValue::CreateNumberValue(speaking ? 1 : 0));
        d.Insert(L"delay", JsonValue::CreateNumberValue(0));
        d.Insert(L"ssrc", JsonValue::CreateNumberValue(m_ssrc));
        SendJson(Speaking, d);
        if (m_cb.onSpeaking) m_cb.onSpeaking(m_params.userId, speaking);
    }

    void VoiceConnection::SendOpus(uint8_t const* opus, size_t length)
    {
        if (m_udp == INVALID_SOCKET || !m_sendCrypto.HasKey()) return;

        // 1) DAVE end-to-end layer on the Opus frame.
        uint8_t e2ee[1500];
        size_t e2eeLength = 0;
        {
            std::lock_guard guard{ m_daveLock };
            if (daveEncryptorEncrypt(m_encryptor, DAVE_MEDIA_TYPE_AUDIO, m_ssrc, opus, length, e2ee, sizeof(e2ee), &e2eeLength)
                != DAVE_ENCRYPTOR_RESULT_CODE_SUCCESS)
            {
                ++m_e2eeEncryptSkipped;
                return;   // no key yet (joining): Discord drops unencrypted frames anyway
            }
        }

        // 2) RTP header + transport AEAD (rtpsize: header authenticated, payload encrypted, 32-bit nonce appended).
        uint8_t packet[1600];
        packet[0] = 0x80;
        packet[1] = 0x78;
        PutU16(packet + 2, m_rtpSeq++);
        PutU32(packet + 4, m_rtpTimestamp);
        PutU32(packet + 8, m_ssrc);
        m_rtpTimestamp += AudioEngine::FrameSamples;

        uint8_t nonce[12]{};
        uint32_t counter = m_nonce++;
        PutU32(nonce, counter);
        uint8_t* cipher = packet + 12;
        uint8_t* tag = cipher + e2eeLength;
        if (!m_sendCrypto.Encrypt(nonce, packet, 12, e2ee, e2eeLength, cipher, tag)) return;
        PutU32(tag + 16, counter);
        int total = static_cast<int>(12 + e2eeLength + 16 + 4);
        if (send(m_udp, reinterpret_cast<char const*>(packet), total, 0) == total)
        {
            ++m_packetsSent;
            m_bytesSent += static_cast<uint64_t>(total);
        }
    }

    void VoiceConnection::ReceiveLoop()
    {
        std::vector<uint8_t> packet(4096), plain(4096), opus(4096);
        std::vector<int16_t> pcm(AudioEngine::FrameSamples * AudioEngine::Channels * 6);
        while (!m_stopped)
        {
            int received = recv(m_udp, reinterpret_cast<char*>(packet.data()), static_cast<int>(packet.size()), 0);
            if (received <= 0)
            {
                if (m_stopped) break;
                continue;
            }
            size_t size = static_cast<size_t>(received);
            m_bytesReceived += size;
            if (size < 12 + 16 + 4 || (packet[0] & 0xC0) != 0x80) continue;
            uint8_t payloadType = packet[1] & 0x7F;
            if (payloadType != 0x78) continue;   // RTCP and others

            // Header = 12 + CSRCs (+ 4-byte extension header, authenticated but not encrypted in rtpsize).
            size_t headerLength = 12 + 4 * (packet[0] & 0x0F);
            bool extension = (packet[0] & 0x10) != 0;
            size_t extensionWords = 0;
            if (extension)
            {
                if (size < headerLength + 4) continue;
                extensionWords = GetU16(packet.data() + headerLength + 2);
                headerLength += 4;
            }
            if (size < headerLength + 16 + 4) continue;
            size_t cipherLength = size - headerLength - 16 - 4;
            uint8_t nonce[12]{};
            memcpy(nonce, packet.data() + size - 4, 4);
            if (!m_recvCrypto.Decrypt(nonce, packet.data(), headerLength, packet.data() + headerLength, cipherLength,
                                      packet.data() + headerLength + cipherLength, plain.data()))
            {
                ++m_transportFailures;
                continue;
            }
            size_t skip = extensionWords * 4;
            if (skip > cipherLength) continue;
            uint8_t const* frame = plain.data() + skip;
            size_t frameLength = cipherLength - skip;

            uint32_t ssrc = GetU32(packet.data() + 8);
            {
                // Loss estimate from RTP sequence gaps (wrap-around safe).
                uint16_t seq = GetU16(packet.data() + 2);
                auto& track = m_seqTracks[ssrc];
                if (track.started)
                {
                    auto ahead = static_cast<int16_t>(seq - track.last);
                    if (ahead > 1 && ahead < 1000) m_packetsLost += static_cast<uint64_t>(ahead - 1);
                    if (ahead > 0) track.last = seq;
                }
                else
                {
                    track.started = true;
                    track.last = seq;
                }
            }
            std::string user;
            {
                std::lock_guard guard{ m_peersLock };
                if (auto it = m_ssrcUser.find(ssrc); it != m_ssrcUser.end()) user = it->second;
            }
            if (user.empty()) continue;   // unknown sender until its Speaking event arrives

            // DAVE end-to-end layer.
            size_t opusLength = 0;
            {
                std::lock_guard guard{ m_daveLock };
                auto decryptor = DecryptorFor(user);
                if (daveDecryptorDecrypt(decryptor, DAVE_MEDIA_TYPE_AUDIO, frame, frameLength, opus.data(), opus.size(), &opusLength)
                    != DAVE_DECRYPTOR_RESULT_CODE_SUCCESS)
                {
                    ++m_e2eeDecryptFailures;
                    continue;
                }
            }
            ++m_packetsReceived;
            if (opusLength == 3 && memcmp(opus.data(), OpusSilence, 3) == 0) continue;

            auto& decoder = m_decoders[ssrc];
            if (!decoder)
            {
                int error = 0;
                decoder = opus_decoder_create(AudioEngine::SampleRate, AudioEngine::Channels, &error);
                if (!decoder) continue;
            }
            int frames = opus_decode(decoder, opus.data(), static_cast<opus_int32>(opusLength), pcm.data(),
                                     static_cast<int>(pcm.size() / AudioEngine::Channels), 0);
            if (frames <= 0) continue;
            m_audio.PushPlayback(ssrc, pcm.data(), static_cast<size_t>(frames));
            std::lock_guard guard{ m_peersLock };
            m_lastHeard[user] = NowMs();
        }
    }
}

namespace DiscordWin3::Voice
{
    VoiceConnection::Stats VoiceConnection::GetStats()
    {
        Stats s;
        s.state = m_state;
        s.endpoint = m_params.endpoint;
        s.mode = L"aead_aes256_gcm_rtpsize";
        s.ssrc = m_ssrc;
        s.rttMs = m_rtt;
        s.packetsSent = m_packetsSent;
        s.packetsReceived = m_packetsReceived;
        s.bytesSent = m_bytesSent;
        s.bytesReceived = m_bytesReceived;
        s.packetsLost = m_packetsLost;
        s.transportFailures = m_transportFailures;
        s.e2eeDecryptFailures = m_e2eeDecryptFailures;
        s.e2eeEncryptSkipped = m_e2eeEncryptSkipped;
        s.speaking = m_speaking;
        {
            std::lock_guard guard{ m_peersLock };
            s.participants = static_cast<int>(m_connectedUsers.size()) + 1;
        }
        {
            std::lock_guard guard{ m_daveLock };
            s.daveVersion = m_daveVersion;
            s.daveReady = m_encryptor && daveEncryptorHasKeyRatchet(m_encryptor) && !daveEncryptorIsPassthroughMode(m_encryptor);
        }
        return s;
    }
}
