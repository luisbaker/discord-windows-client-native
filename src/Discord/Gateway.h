#pragma once

namespace DiscordWin3::Discord
{
    enum class GatewayStatus
    {
        Connecting,
        Connected,
        Reconnecting,
        AuthFailed,
    };

    // Discord gateway v9 over Windows.Networking.Sockets.MessageWebSocket (JSON, no compression).
    // Callbacks run on thread-pool threads; the consumer marshals to the UI thread.
    class Gateway : public std::enable_shared_from_this<Gateway>
    {
    public:
        using DispatchHandler = std::function<void(std::wstring const& type, winrt::Windows::Data::Json::JsonObject const& d)>;
        using StatusHandler = std::function<void(GatewayStatus status)>;

        Gateway(std::wstring token, DispatchHandler onDispatch, StatusHandler onStatus);
        ~Gateway();

        void Start();
        void Stop();

    private:
        winrt::fire_and_forget Connect(bool resume);
        void OnText(winrt::hstring const& text, uint64_t generation);
        void OnClosed(uint16_t code, uint64_t generation);
        void Send(winrt::Windows::Data::Json::JsonObject const& payload);
        void SendHeartbeat();
        void SendIdentify();
        void SendResume();
        void StartHeartbeat(int intervalMs);
        void Teardown();
        void Reconnect(bool resume);

        std::wstring m_token;
        DispatchHandler m_onDispatch;
        StatusHandler m_onStatus;

        std::recursive_mutex m_lock;
        winrt::Windows::Networking::Sockets::MessageWebSocket m_socket{ nullptr };
        winrt::Windows::System::Threading::ThreadPoolTimer m_heartbeat{ nullptr };
        uint64_t m_generation = 0;     // bumps on every (re)connect; stale socket events are ignored
        bool m_stopped = false;
        bool m_resuming = false;
        std::atomic<bool> m_acked{ true };
        std::atomic<int64_t> m_seq{ -1 };
        std::wstring m_sessionId;
        std::wstring m_resumeUrl;
        int m_failures = 0;
    };
}
