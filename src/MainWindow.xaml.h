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
        void OnChannelClicked(IInspectable const&, Microsoft::UI::Xaml::Controls::ItemClickEventArgs const&);
        void OnToggleMembers(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnMessageContainerChanging(Microsoft::UI::Xaml::Controls::ListViewBase const&,
                                        Microsoft::UI::Xaml::Controls::ContainerContentChangingEventArgs const&);
        fire_and_forget OnAttach(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnComposerTextChanged(IInspectable const&, Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&);
        void OnLogout(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnMessageListLoaded(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnComposerKeyDown(IInspectable const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const&);
        void OnTokenLogin(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnQrRetry(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnCancelReply(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

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
        std::vector<::DiscordWin3::Segment> ParseBody(std::wstring const& raw, Windows::Data::Json::JsonObject const& m);
        std::wstring PlainText(std::wstring const& raw, Windows::Data::Json::JsonObject const& m);
        void RenderBody(Microsoft::UI::Xaml::Controls::RichTextBlock const& block, ::DiscordWin3::MessageData const& data);
        std::wstring DayLabel(::DiscordWin3::MessageData const* prev, ::DiscordWin3::MessageData const& cur);
        winrt::DiscordWin3::MessageItem MakeRow(::DiscordWin3::MessageData data, ::DiscordWin3::MessageData const* prev);
        bool ShouldShowHeader(::DiscordWin3::MessageData const* prev, ::DiscordWin3::MessageData const& cur);
        void AppendMessage(::DiscordWin3::MessageData data);
        void FixHeaderAt(uint32_t index);
        int FindMessage(std::wstring const& id);

        // Animations
        void MorphGuild(Microsoft::UI::Xaml::UIElement const& root, bool squircle, bool animate);
        void UpdateGuildMorphs();
        void AnimateMessagesIn();

        void SetBackgroundMode(bool background);
        void ApplyPowerPolicy();   // Energy Saver / "animation effects" -> no animations + EcoQoS

        // Member list sidebar (op 37 -> GUILD_MEMBER_LIST_UPDATE)
        void SubscribeMembers();
        void OnMemberListUpdate(Windows::Data::Json::JsonObject const& d);
        IInspectable BuildMemberRow(Windows::Data::Json::JsonObject const& item, GuildInfo const& guild);

        // Typing indicator
        void OnTypingStart(Windows::Data::Json::JsonObject const& d);
        void UpdateTypingText();

        void UpdateTitle();

        // Unread state (READY read_state + user_guild_settings, kept live by MESSAGE_CREATE / MESSAGE_ACK)
        void ParseReadStates(Windows::Data::Json::JsonObject const& d);
        void ParseGuildSettings(Windows::Data::Json::IJsonValue const& settings);
        bool IsUnread(ChannelInfo const& channel) const;
        int MentionsIn(std::wstring const& channelId) const;
        std::pair<bool, int> GuildBadge(GuildInfo const& guild) const;
        std::pair<bool, int> HomeBadge() const;
        IInspectable MakeGuildItem(GuildInfo const& guild) const;
        void UpdateGuildRow(std::wstring const& guildId);
        void UpdateChannelRow(std::wstring const& channelId);
        ChannelInfo* FindChannel(std::wstring const& channelId, std::wstring* guildId = nullptr);
        void OnMessageForUnread(Windows::Data::Json::JsonObject const& d, ::DiscordWin3::MessageData const* data);
        void Ack(std::wstring const& channelId, std::wstring const& messageId);
        void OnRemoteAck(Windows::Data::Json::JsonObject const& d);

        // Reactions
        void RenderReactions(Microsoft::UI::Xaml::Controls::StackPanel const& panel, ::DiscordWin3::MessageData const& data);
        fire_and_forget ToggleReaction(std::wstring messageId, ::DiscordWin3::Reaction reaction);
        void OnReactionEvent(std::wstring const& type, Windows::Data::Json::JsonObject const& d);

        // Message actions (context menu, reply, edit, delete)
        void OnMessageMenuOpening(IInspectable const& sender);
        void StartReply(::DiscordWin3::MessageData const& data);
        void StartEdit(::DiscordWin3::MessageData const& data);
        void ClearComposerMode();
        fire_and_forget DeleteMessage(std::wstring messageId);
        bool EditLastOwnMessage();

        // Notifications
        void InitNotifications();
        void Notify(::DiscordWin3::MessageData const& data, std::wstring const& channelId, std::wstring const& guildId);
        void OpenChannel(std::wstring const& guildId, std::wstring const& channelId);
        fire_and_forget UploadFile(std::wstring path);

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
        bool m_energySaver = false;
        bool m_reduceMotion = false;
        Windows::UI::ViewManagement::UISettings m_uiSettings{ nullptr };
        std::unordered_set<std::wstring> m_collapsed;                  // collapsed category ids
        std::wstring m_memberListGuild;
        std::unordered_map<std::wstring, int> m_memberGroupCounts;
        std::unordered_map<std::wstring, std::pair<int64_t, std::wstring>> m_typing;  // userId -> (expiry ms, name)
        Microsoft::UI::Dispatching::DispatcherQueueTimer m_typingTimer{ nullptr };
        int64_t m_lastTypingSent = 0;

        struct ReadState { std::wstring lastRead; int mentions = 0; };
        std::unordered_map<std::wstring, ReadState> m_readStates;      // channelId -> state
        std::unordered_set<std::wstring> m_mutedGuilds;
        std::unordered_set<std::wstring> m_mutedChannels;
        std::unordered_map<std::wstring, std::wstring> m_channelGuild; // channelId -> guildId
        std::wstring m_replyToId;
        std::wstring m_editingId;
        bool m_windowActive = true;
        bool m_notificationsReady = false;
        std::wstring m_pendingOpenGuild, m_pendingOpenChannel;
        Microsoft::UI::Xaml::Controls::MenuFlyout m_messageMenu{ nullptr };
        std::wstring m_ackChannel, m_ackMessage;
        bool m_ackScheduled = false;

        Windows::Foundation::Collections::IObservableVector<IInspectable> m_guildItems =
            single_threaded_observable_vector<IInspectable>();
        Windows::Foundation::Collections::IObservableVector<IInspectable> m_channelItems =
            single_threaded_observable_vector<IInspectable>();
        Windows::Foundation::Collections::IObservableVector<IInspectable> m_messageItems =
            single_threaded_observable_vector<IInspectable>();
        Windows::Foundation::Collections::IObservableVector<IInspectable> m_memberItems =
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
