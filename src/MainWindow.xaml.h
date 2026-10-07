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
        std::wstring avatarUrl;      // DMs: recipient avatar / group icon
        int type = 0;
        int position = 0;
        std::wstring lastMessageId;
        std::vector<::DiscordWin3::Discord::Overwrite> overwrites;
    };

    struct RoleStyle
    {
        int position = 0;
        uint32_t color = 0;
    };

    struct GuildInfo
    {
        std::wstring id;
        std::wstring name;
        std::wstring icon;
        std::vector<ChannelInfo> channels;
        ::DiscordWin3::Discord::GuildPermissions perms;
        std::unordered_map<uint64_t, RoleStyle> roles;
        std::unordered_map<std::wstring, std::wstring> voice;   // userId -> voice channelId
    };

    // Kept tiny on purpose: only what the UI shows.
    struct UserInfo
    {
        std::wstring name;
        std::wstring avatarUrl;
    };

    struct GuildMember
    {
        std::wstring nick;
        std::wstring avatarUrl;   // guild-specific avatar, empty if none
        uint32_t color = 0;
    };

    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow() = default;
        void InitializeComponent();

        void OnGuildSelected(IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void OnGuildContainerChanging(Microsoft::UI::Xaml::Controls::ListViewBase const&,
                                      Microsoft::UI::Xaml::Controls::ContainerContentChangingEventArgs const&);
        void OnChannelSelected(IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void OnLogout(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnMessageListLoaded(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnComposerKeyDown(IInspectable const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const&);
        void OnTokenLogin(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnQrRetry(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        // Session
        void ShowLogin(std::wstring const& error);
        void ShowChat();
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
        void ParseDmChannel(Windows::Data::Json::JsonObject const& c, ChannelInfo& info);
        void ParseVoiceStates(GuildInfo& guild, Windows::Data::Json::JsonArray const& states);
        UserInfo const& CacheUser(Windows::Data::Json::JsonObject const& user);
        void CacheMember(GuildInfo const& guild, Windows::Data::Json::JsonObject const& member);
        void UpsertGuild(GuildInfo guild);
        void RefreshGuildRail();
        void RefreshChannelList();
        GuildInfo* FindGuild(std::wstring const& id);

        // Members (nick / guild avatar / role color), fetched lazily with gateway op 8
        void ApplyMember(::DiscordWin3::MessageData& data);
        void RequestMissingMembers();
        void OnMembersChunk(Windows::Data::Json::JsonObject const& d);

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

        // Animations
        void MorphGuild(Microsoft::UI::Xaml::UIElement const& root, bool squircle, bool animate);
        void UpdateGuildMorphs();
        void AnimateMessagesIn();

        void SetBackgroundMode(bool background);

        std::wstring m_token;
        std::shared_ptr<::DiscordWin3::Discord::Rest> m_rest;
        std::shared_ptr<::DiscordWin3::Discord::Gateway> m_gateway;
        std::shared_ptr<::DiscordWin3::Discord::RemoteAuth> m_remoteAuth;
        bool m_saveTokenOnReady = false;

        std::wstring m_selfId;
        std::vector<GuildInfo> m_guilds;
        std::vector<ChannelInfo> m_dms;
        std::unordered_map<std::wstring, UserInfo> m_users;
        std::unordered_map<std::wstring, std::unordered_map<std::wstring, GuildMember>> m_members;  // guild -> user
        std::unordered_set<std::wstring> m_requestedMembers;                                     // "guild:user"
        std::unordered_map<std::wstring, std::wstring> m_channelNames;
        std::unordered_map<std::wstring, std::wstring> m_roleNames;

        std::wstring m_currentGuildId;
        std::wstring m_currentChannelId;
        uint64_t m_channelGeneration = 0;
        bool m_loadingOlder = false;
        bool m_hasMoreOlder = false;
        bool m_background = false;

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
