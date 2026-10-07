#pragma once

#include "MainWindow.g.h"
#include "Models.h"
#include "Discord/Gateway.h"
#include "Discord/Permissions.h"
#include "Discord/RemoteAuth.h"
#include "Discord/Rest.h"
#include "Voice/VoiceConnection.h"
#include "Voice/ScreenShare.h"

namespace winrt::DiscordWin3::implementation
{
    struct ChannelInfo
    {
        std::wstring id;
        std::wstring name;
        std::wstring parentId;
        std::wstring avatarUrl;      // DMs: recipient avatar / group icon
        std::wstring recipientId;    // 1:1 DMs: the other user (presence dot)
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
        struct Emoji { std::wstring id, name; };
        std::vector<Emoji> emojis;                              // custom emojis (picker)
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
        void OnNavigateBack(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSettings(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnVoiceDisconnect(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnToggleMute(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnToggleDeafen(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnStartCall(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnShareScreen(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnToggleStats(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnOpenCallView(IInspectable const&, Microsoft::UI::Xaml::Input::TappedRoutedEventArgs const&);
        void OnPlayVideo(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        fire_and_forget OnDownloadMedia(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnHoverReaction(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnHoverPicker(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnHoverEdit(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnHoverReply(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnHoverForward(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnHoverMore(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnComposerEmoji(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        fire_and_forget OnComposerPaste(IInspectable const&, Microsoft::UI::Xaml::Controls::TextControlPasteEventArgs const&);
        void OnChatDragOver(IInspectable const&, Microsoft::UI::Xaml::DragEventArgs const&);
        fire_and_forget OnChatDrop(IInspectable const&, Microsoft::UI::Xaml::DragEventArgs const&);
        void OnNavigateForward(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        fire_and_forget OnQuickSwitch(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnShowFriends(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnFriendsTab(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnFriendsSearchChanged(IInspectable const&, Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&);
        fire_and_forget OnSendFriendRequest(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnFriendMessage(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        fire_and_forget OnFriendAccept(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        fire_and_forget OnFriendRemove(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        // Session
        void ShowLogin(std::wstring const& error);
        void ShowChat();
        void StartQr();
        fire_and_forget RenderQr(std::wstring url);
        void StartSession(std::wstring token);
        void EndSession();
        void OnDispatch(::DiscordWin3::Discord::DispatchEvent const& e);
        void OnStatus(::DiscordWin3::Discord::GatewayStatus status);

        // Model
        void HandleReady(::DiscordWin3::Slim::Value const& d);
        GuildInfo ParseGuild(::DiscordWin3::Slim::Value const& g);
        ChannelInfo ParseChannel(::DiscordWin3::Slim::Value const& c);
        void ParseDmChannel(::DiscordWin3::Slim::Value const& c, ChannelInfo& info);
        void ParseVoiceState(GuildInfo& guild, ::DiscordWin3::Slim::Value const& state);
        UserInfo const& CacheUser(Windows::Data::Json::JsonObject const& user);
        UserInfo const& CacheUser(::DiscordWin3::Slim::Value const& user);
        template <typename O> UserInfo const& CacheUserT(O const& user);
        void CacheMember(GuildInfo const& guild, Windows::Data::Json::JsonObject const& member);
        void CacheMember(GuildInfo const& guild, ::DiscordWin3::Slim::Value const& member);
        template <typename O> void CacheMemberT(GuildInfo const& guild, O const& member);
        void UpsertGuild(GuildInfo guild);
        void RefreshGuildRail();
        void RefreshChannelList();
        GuildInfo* FindGuild(std::wstring const& id);

        // Members (nick / guild avatar / role color), fetched lazily with gateway op 8
        void ApplyMember(::DiscordWin3::MessageData& data);
        void RequestMissingMembers();
        void OnMembersChunk(::DiscordWin3::Slim::Value const& d);

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
        void OnMemberListUpdate(::DiscordWin3::Slim::Value const& d);
        IInspectable BuildMemberRow(::DiscordWin3::Slim::Value const& item, GuildInfo const& guild);

        // Typing indicator
        void OnTypingStart(Windows::Data::Json::JsonObject const& d);
        void UpdateTypingText();

        void UpdateTitle();

        // Unread state (READY read_state + user_guild_settings, kept live by MESSAGE_CREATE / MESSAGE_ACK)
        void ParseReadStates(::DiscordWin3::Slim::Value const& d);
        void ParseGuildSettings(::DiscordWin3::Slim::Value const& settings);
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

        // Home: friends, presence, quick switcher, history
        void ParseRelationships(::DiscordWin3::Slim::Value const& d);
        void ParsePresence(::DiscordWin3::Slim::Value const& p, std::wstring userId = {});
        void OnPresenceUpdate(::DiscordWin3::Slim::Value const& d);
        void OnRelationshipEvent(std::wstring const& type, ::DiscordWin3::Slim::Value const& d);
        uint32_t PresenceColor(std::wstring const& userId) const;
        std::wstring PresenceText(std::wstring const& userId, bool fallbackToStatus) const;
        void ShowFriends(bool show);
        void RefreshFriends();
        void RefreshActiveNow();
        void UpdateHomeChrome();
        fire_and_forget OpenDmWith(std::wstring userId);
        void PushHistory();
        void UpdateNavButtons();
        void UpdateTitleBarRegions();

        // Voice (gateway op 4 -> VOICE_STATE_UPDATE + VOICE_SERVER_UPDATE -> VoiceConnection)
        void JoinVoice(std::wstring guildId, std::wstring channelId);
        void LeaveVoice();
        void SendVoiceState();
        void TryStartVoice();
        void OnOwnVoiceState(::DiscordWin3::Slim::Value const& d);
        void OnVoiceServerUpdate(::DiscordWin3::Slim::Value const& d);
        void UpdateVoiceUserRow(std::wstring const& userId);
        void UpdateVoiceButtons();
        void ShowCallView(bool show);
        void RefreshCallParticipants();
        void UpdateStatsText();

        // Go Live (gateway op 18 -> STREAM_CREATE + STREAM_SERVER_UPDATE -> stream VoiceConnection + ScreenShare)
        fire_and_forget StartScreenShare();
        void StopScreenShare(bool notifyServer);
        void TryStartStream();
        void OnStreamEvent(std::wstring const& type, ::DiscordWin3::Slim::Value const& d);

        // Translations
        void ApplyTexts();

        // Attachments waiting to be sent
        struct PendingAttachment { std::wstring path, filename, description; bool spoiler = false; bool image = false; uint64_t size = 0; };
        fire_and_forget StageFile(std::wstring path, std::wstring displayName = {});
        void RenderPending();
        fire_and_forget EditPending(size_t index);
        fire_and_forget SendWithAttachments(std::wstring text, std::wstring replyToId);

        // Hover bar, emoji picker, forwarding
        void ShowEmojiPicker(Microsoft::UI::Xaml::FrameworkElement const& anchor, std::function<void(::DiscordWin3::Reaction const&)> onPick);
        void RememberEmoji(std::wstring const& emoji);
        fire_and_forget ForwardMessage(::DiscordWin3::MessageData data);
        ::DiscordWin3::MessageData const* MessageFromSender(IInspectable const& sender);

        // Notifications
        void InitNotifications();
        void Notify(::DiscordWin3::MessageData const& data, std::wstring const& channelId, std::wstring const& guildId);
        void OpenChannel(std::wstring const& guildId, std::wstring const& channelId);

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
        uint64_t m_idleGeneration = 0;
        bool m_notificationsReady = false;
        std::wstring m_pendingOpenGuild, m_pendingOpenChannel;
        Microsoft::UI::Xaml::Controls::MenuFlyout m_messageMenu{ nullptr };
        Microsoft::UI::Xaml::Controls::Panel m_videoHome{ nullptr };   // where a fullscreen video came from
        std::shared_ptr<::DiscordWin3::Voice::VoiceConnection> m_voice;
        std::wstring m_voiceGuild, m_voiceChannel, m_voiceSession, m_voiceToken, m_voiceEndpoint;
        bool m_selfMute = false;
        bool m_selfDeaf = false;
        std::unordered_set<std::wstring> m_speakingUsers;
        std::shared_ptr<::DiscordWin3::Voice::VoiceConnection> m_stream;
        std::unique_ptr<::DiscordWin3::Voice::ScreenShare> m_screen;
        winrt::Windows::Graphics::Capture::GraphicsCaptureItem m_captureItem{ nullptr };
        std::wstring m_streamKey, m_streamServerId, m_streamToken, m_streamEndpoint;
        bool m_voiceConnected = false;
        std::unordered_set<std::wstring> m_dmCallUsers;                              // DM call participants
        std::unordered_map<std::wstring, std::pair<bool, bool>> m_voiceFlags;         // userId -> (muted, deafened)
        bool m_showingCall = false;
        Microsoft::UI::Dispatching::DispatcherQueueTimer m_statsTimer{ nullptr };
        Windows::Foundation::Collections::IObservableVector<IInspectable> m_callItems =
            single_threaded_observable_vector<IInspectable>();

        struct Presence { std::wstring status; std::wstring activity; std::wstring game; bool hasActivity = false; };
        std::unordered_map<std::wstring, Presence> m_presence;            // userId -> presence (friends + DMs)
        struct Relationship { int type = 0; std::wstring nickname; };
        std::unordered_map<std::wstring, Relationship> m_relationships;   // userId -> relationship
        std::unordered_map<std::wstring, std::wstring> m_userTags;        // userId -> server tag
        std::wstring m_friendsTab = L"online";
        bool m_showingFriends = false;
        Windows::Foundation::Collections::IObservableVector<IInspectable> m_friendItems =
            single_threaded_observable_vector<IInspectable>();
        std::vector<std::pair<std::wstring, std::wstring>> m_history;      // (guild, channel)
        size_t m_historyIndex = 0;
        bool m_navigatingHistory = false;
        std::vector<PendingAttachment> m_pending;
        std::vector<std::wstring> m_recentEmojis{ L"\U0001F44D", L"\u2764\uFE0F", L"\U0001F602" };   // thumbs up, heart, joy
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
