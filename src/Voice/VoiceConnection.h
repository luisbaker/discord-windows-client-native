#pragma once

#include "AesGcm.h"
#include "AudioEngine.h"
#include "../SlimJson.h"

#include <winsock2.h>

struct OpusEncoder;
struct OpusDecoder;
typedef struct DAVESessionHandle_s* DAVESessionHandle;
typedef struct DAVEEncryptorHandle_s* DAVEEncryptorHandle;
typedef struct DAVEDecryptorHandle_s* DAVEDecryptorHandle;
typedef struct DAVEKeyRatchetHandle_s* DAVEKeyRatchetHandle;

namespace DiscordWin3::Voice
{
    struct VoiceParams
    {
        std::wstring serverId;    // guild id, or the DM channel id for calls
        std::wstring channelId;
        std::wstring userId;
        std::wstring sessionId;   // from our VOICE_STATE_UPDATE
        std::wstring token;       // from VOICE_SERVER_UPDATE
        std::wstring endpoint;    // from VOICE_SERVER_UPDATE ("host:port")
    };

    // One voice call: voice gateway (v8) + UDP/RTP (aead_aes256_gcm_rtpsize) + Opus + DAVE (E2EE, mandatory
    // since March 2026). Callbacks run on background threads; the UI marshals them.
    class VoiceConnection : public std::enable_shared_from_this<VoiceConnection>
    {
    public:
        enum class State { Connecting, Connected, Failed };
        struct Callbacks
        {
            std::function<void(State state, std::wstring const& detail)> onState;
            std::function<void(std::wstring const& userId, bool speaking)> onSpeaking;
        };

        // "Stats for nerds": snapshot readable from any thread.
        struct Stats
        {
            State state = State::Connecting;
            std::wstring endpoint;
            std::wstring mode;
            uint32_t ssrc = 0;
            int rttMs = -1;                 // voice gateway heartbeat round trip
            uint16_t daveVersion = 0;       // 0 = DAVE off (passthrough)
            bool daveReady = false;         // our encryptor has an MLS key ratchet
            int participants = 0;
            uint64_t packetsSent = 0, packetsReceived = 0;
            uint64_t bytesSent = 0, bytesReceived = 0;
            uint64_t packetsLost = 0;       // from RTP sequence gaps
            uint64_t transportFailures = 0; // AES-GCM authentication failures
            uint64_t e2eeDecryptFailures = 0;
            uint64_t e2eeEncryptSkipped = 0;
            bool speaking = false;
        };
        Stats GetStats();

        VoiceConnection(VoiceParams params, Callbacks callbacks);
        ~VoiceConnection();

        void Start();
        void Stop();
        void SetMuted(bool muted);
        void SetDeafened(bool deafened);

    private:
        // Voice gateway
        winrt::fire_and_forget Connect();
        void OnText(std::wstring text);
        void OnBinary(std::vector<uint8_t> data);
        void SendJson(int op, winrt::Windows::Data::Json::IJsonValue const& d);
        void SendBinary(uint8_t op, uint8_t const* payload, size_t length);
        void Fail(std::wstring const& detail);

        // UDP / RTP
        bool OpenUdp(std::string const& ip, uint16_t port, std::string& externalIp, uint16_t& externalPort);
        void ReceiveLoop();
        void OnCapturedFrame(int16_t const* pcm, bool voiced);
        void SendOpus(uint8_t const* opus, size_t length);
        void SetSpeaking(bool speaking);

        // DAVE (end-to-end encryption)
        void DaveReinit();
        void DaveSendKeyPackage();
        void DaveOnCommitOrWelcome(uint16_t transitionId, bool ok);
        void DaveApplyPending();
        std::vector<std::string> DaveRecognizedUsers();
        DAVEDecryptorHandle DecryptorFor(std::string const& userId);

        VoiceParams m_params;
        Callbacks m_cb;
        std::string m_userIdUtf8;

        std::mutex m_sendLock;
        winrt::Windows::Networking::Sockets::MessageWebSocket m_socket{ nullptr };
        winrt::Windows::System::Threading::ThreadPoolTimer m_heartbeat{ nullptr };
        winrt::Windows::System::Threading::ThreadPoolTimer m_ticker{ nullptr };
        std::atomic<int> m_seqAck{ -1 };
        std::atomic<bool> m_stopped{ false };

        SOCKET m_udp = INVALID_SOCKET;
        std::thread m_receiver;
        uint32_t m_ssrc = 0;
        uint16_t m_rtpSeq = 0;
        uint32_t m_rtpTimestamp = 0;
        uint32_t m_nonce = 0;
        AesGcm m_sendCrypto;
        AesGcm m_recvCrypto;

        AudioEngine m_audio;
        OpusEncoder* m_encoder = nullptr;
        std::unordered_map<uint32_t, OpusDecoder*> m_decoders;   // receive thread only
        bool m_speaking = false;
        int m_trailingSilence = 0;

        // Remote speaking indicators
        std::mutex m_peersLock;
        std::unordered_map<uint32_t, std::string> m_ssrcUser;
        std::unordered_map<std::string, int64_t> m_lastHeard;     // userId -> ms
        std::unordered_set<std::string> m_speakingNow;
        std::unordered_set<std::string> m_connectedUsers;

        // DAVE state (guarded by m_daveLock: used by gateway, capture and receive threads)
        std::recursive_mutex m_daveLock;
        DAVESessionHandle m_dave = nullptr;
        DAVEEncryptorHandle m_encryptor = nullptr;
        std::unordered_map<std::string, DAVEDecryptorHandle> m_decryptors;
        std::vector<DAVEKeyRatchetHandle> m_ratchets;     // current generation (not owned by cryptors)
        std::vector<DAVEKeyRatchetHandle> m_oldRatchets;  // previous generation, kept for the transition window
        uint16_t m_daveVersion = 0;
        std::vector<uint8_t> m_externalSender;
        uint16_t m_pendingTransition = 0;
        bool m_pendingPassthrough = false;
        bool m_pendingRatchets = false;

        // Stats
        std::atomic<State> m_state{ State::Connecting };
        std::atomic<int> m_rtt{ -1 };
        std::atomic<uint64_t> m_packetsSent{ 0 }, m_packetsReceived{ 0 }, m_bytesSent{ 0 }, m_bytesReceived{ 0 };
        std::atomic<uint64_t> m_packetsLost{ 0 }, m_transportFailures{ 0 }, m_e2eeDecryptFailures{ 0 }, m_e2eeEncryptSkipped{ 0 };
        struct SeqTrack { uint16_t last = 0; bool started = false; };
        std::unordered_map<uint32_t, SeqTrack> m_seqTracks;   // receive thread only
    };
}
