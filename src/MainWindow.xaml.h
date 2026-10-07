#pragma once

#include "MainWindow.g.h"
#include "Models.h"
#include "Discord/Gateway.h"
#include "Discord/Permissions.h"
#include "Discord/RemoteAuth.h"
#include "Discord/Rest.h"

namespace winrt::DiscordWin3::implementation
{
    struct ChannelInfo
    {
        std::wstring id;
        std::wstring name;
        std::wstring parentId;
        int type = 0;
        int position = 0;
        std::wstring lastMessageId;
        std::vector<::DiscordWin3::Discord::Overwrite> overwrites;
    };

    struct GuildInfo
    {
        std::wstring id;
        std::wstring name;
        std::wstring icon;
        std::vector<ChannelInfo> channels;
        ::DiscordWin3::Discord::GuildPermissions perms;
    };

    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow() = default;
        void InitializeComponent();

        void OnGuildSelected(IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void OnChannelSelected(IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void OnLogout(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnMessageListLoaded(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnComposerKeyDown(IInspectable const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const&);
        void OnTokenLogin(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnQrRetry(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        // Session
        void ShowLogin(std::wstring const& error);
        void StartQr();
        fire_and_forget RenderQr(std::wstring url);
        void StartSession(std::wstring token);
        void EndSession();
        void OnDispatch(std::wstring const& type, Windows::Data::Json::JsonObject const& d);
        void OnStatus(::DiscordWin3::Discord::GatewayStatus status);

        // Model
        void HandleReady(Windows::Data::Json::JsonObject const& d);
        GuildInfo ParseGuild(Windows::Data::Json::JsonObject const& g);
        ChannelInfo ParseChannel(Windows::Data::Json::JsonObject const& c);
        std::wstring DmName(Windows::Data::Json::JsonObject const& c);
        void UpsertGuild(GuildInfo guild);
        void RefreshGuildRail();
        void RefreshChannelList();
        GuildInfo* FindGuild(std::wstring const& id);

        // Messages
        fire_and_forget LoadChannel(std::wstring id, std::wstring title);
        fire_and_forget LoadOlder();
        fire_and_forget SendMessage(std::wstring text);
        ::DiscordWin3::MessageData BuildMessage(Windows::Data::Json::JsonObject const& m);
        std::wstring FormatContent(std::wstring const& raw, Windows::Data::Json::JsonObject const& m);
        bool ShouldShowHeader(::DiscordWin3::MessageData const* prev, ::DiscordWin3::MessageData const& cur);
        void AppendMessage(::DiscordWin3::MessageData data);
        void FixHeaderAt(uint32_t index);
        int FindMessage(std::wstring const& id);

        void TrimMemory();

        std::wstring m_token;
        std::shared_ptr<::DiscordWin3::Discord::Rest> m_rest;
        std::shared_ptr<::DiscordWin3::Discord::Gateway> m_gateway;
        std::shared_ptr<::DiscordWin3::Discord::RemoteAuth> m_remoteAuth;
        bool m_saveTokenOnReady = false;

        std::wstring m_selfId;
        std::vector<GuildInfo> m_guilds;
        std::vector<ChannelInfo> m_dms;
        std::unordered_map<std::wstring, std::wstring> m_userNames;
        std::unordered_map<std::wstring, std::wstring> m_channelNames;
        std::unordered_map<std::wstring, std::wstring> m_roleNames;

        std::wstring m_currentGuildId;
        std::wstring m_currentChannelId;
        uint64_t m_channelGeneration = 0;
        bool m_loadingOlder = false;
        bool m_hasMoreOlder = false;

        Windows::Foundation::Collections::IObservableVector<IInspectable> m_guildItems =
            single_threaded_observable_vector<IInspectable>();
        Windows::Foundation::Collections::IObservableVector<IInspectable> m_channelItems =
            single_threaded_observable_vector<IInspectable>();
        Windows::Foundation::Collections::IObservableVector<IInspectable> m_messageItems =
            single_threaded_observable_vector<IInspectable>();

        Microsoft::UI::Xaml::Controls::ScrollViewer m_messageScroller{ nullptr };
        Microsoft::UI::Dispatching::DispatcherQueue m_dispatcher{ nullptr };
    };
}

namespace winrt::DiscordWin3::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
