#pragma once

namespace DiscordWin3::Discord
{
    // "Log in with QR code" flow (remote-auth-gateway v2). The phone app scans
    // https://discord.com/ra/<fingerprint>, the user confirms, and we receive an
    // RSA-OAEP encrypted token. No password ever touches this app.
    class RemoteAuth : public std::enable_shared_from_this<RemoteAuth>
    {
    public:
        struct Callbacks
        {
            std::function<void(std::wstring const& qrUrl)> onQrCode;
            std::function<void(std::wstring const& username)> onScanned;
            std::function<void(std::wstring const& token)> onToken;
            std::function<void(std::wstring const& message)> onError;
        };

        explicit RemoteAuth(Callbacks callbacks);
        ~RemoteAuth();

        winrt::fire_and_forget Start();
        void Stop();

    private:
        void OnText(winrt::hstring const& text);
        void Send(std::wstring_view op, winrt::Windows::Data::Json::JsonObject extra = nullptr);
        winrt::Windows::Storage::Streams::IBuffer Decrypt(std::wstring const& base64);
        winrt::fire_and_forget ExchangeTicket(std::wstring ticket);

        Callbacks m_cb;
        std::mutex m_lock;
        winrt::Windows::Networking::Sockets::MessageWebSocket m_socket{ nullptr };
        winrt::Windows::System::Threading::ThreadPoolTimer m_heartbeat{ nullptr };
        winrt::Windows::Security::Cryptography::Core::CryptographicKey m_key{ nullptr };
        bool m_finished = false;
    };
}
