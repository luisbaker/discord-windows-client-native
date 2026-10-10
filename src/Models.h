#pragma once

#include "GuildItem.g.h"
#include "ChannelItem.g.h"
#include "MessageItem.g.h"
#include "MemberItem.g.h"
#include "FriendItem.g.h"
#include "ParticipantItem.g.h"

namespace DiscordWin3
{
    // One run of a parsed message body (markdown-lite + Discord tokens).
    struct Segment
    {
        enum class Kind : uint8_t
        {
            Text,
            Bold,
            Code,       // `inline`
            CodeBlock,  // ```block```
            Mention,    // @user / @role / #channel pill
            Link,
            Emoji,      // custom emoji image, url in `url`
        };
        Kind kind = Kind::Text;
        std::wstring text;
        std::wstring url;
    };

    struct Reaction
    {
        std::wstring name;   // unicode emoji, or custom emoji name
        std::wstring id;     // custom emoji id (empty for unicode)
        int count = 0;
        bool me = false;

        // "👍" or "name:id", URL-escaped by the caller.
        std::wstring ApiKey() const { return id.empty() ? name : name + L":" + id; }
    };

    // Plain data handed to the item constructors (kept out of the WinRT surface).
    struct MessageData
    {
        std::vector<Reaction> reactions;
        bool edited = false;
        std::wstring rawContent;  // for editing / copying
        std::wstring id;
        std::wstring authorId;
        std::wstring authorName;
        std::wstring avatarUrl;   // already sized CDN url, empty if none
        std::wstring tag;         // server tag shown next to the name ("IPv6")
        std::vector<Segment> body;
        std::wstring reply;       // plain header line ("Forwarded") when there is no structured reply
        // Reply bar (official layout: curved connector, mini avatar, coloured name, tag, snippet).
        std::wstring replyName;
        std::wstring replyAvatarUrl;
        std::wstring replyTag;
        std::wstring replySnippet;
        uint32_t replyColor = 0;
        bool replyAttachmentOnly = false;
        struct GalleryImage { std::wstring url, full; double width = 0, height = 0; };
        std::vector<GalleryImage> gallery;   // every image when there are 2+ (mosaic), empty otherwise
        std::wstring timestamp;   // display string
        int64_t unixMs = 0;
        std::wstring imageUrl;    // first image attachment (proxied, resized)
        std::wstring mediaUrl;    // full-size original (download / video playback)
        bool isVideo = false;
        double imageWidth = 0;
        double imageHeight = 0;
        std::wstring files;       // other attachments, one per line
        std::wstring embedProvider;
        std::wstring embedTitle;
        std::wstring embedDescription;
        uint32_t embedColor = 0;
        uint32_t color = 0;       // top role color (0xRRGGBB), 0 = default
        bool own = false;          // written by the logged-in user
        bool mentionsMe = false;
        bool forceHeader = false; // replies / system messages never collapse into the previous group
        bool HasBody() const { return !body.empty(); }
    };
}

namespace winrt::DiscordWin3::implementation
{
    using Microsoft::UI::Xaml::Visibility;
    using Microsoft::UI::Xaml::Media::ImageSource;
    using Microsoft::UI::Xaml::Media::Brush;

    // Shared, cached solid brushes (UI thread only). 0 alpha byte = opaque.
    Brush SolidBrush(uint32_t rgb, uint8_t alpha = 255);

    inline Visibility Show(bool visible) { return visible ? Visibility::Visible : Visibility::Collapsed; }

    struct GuildItem : GuildItemT<GuildItem>
    {
        GuildItem(hstring id, hstring name, hstring iconUrl, bool unread = false, int mentions = 0);

        hstring Id() const { return m_id; }
        hstring Name() const { return m_name; }
        hstring Initials() const { return m_initials; }
        ImageSource Icon();
        Visibility IconVisibility() const { return Show(!m_iconUrl.empty()); }
        Visibility InitialsVisibility() const { return Show(m_iconUrl.empty() && m_id != L"@me"); }
        Visibility HomeVisibility() const { return Show(m_id == L"@me"); }
        Visibility UnreadVisibility() const { return Show(m_unread); }
        hstring MentionText() const { return m_mentions > 99 ? hstring{ L"99+" } : hstring{ std::to_wstring(m_mentions) }; }
        Visibility MentionVisibility() const { return Show(m_mentions > 0); }

    private:
        hstring m_id, m_name, m_initials, m_iconUrl;
        bool m_unread;
        int m_mentions;
    };

    enum class ChannelKind
    {
        Text,       // opens in the chat
        Category,
        Voice,      // voice / stage / forum: not readable as text
        VoiceUser,  // someone connected to the voice channel above
    };

    struct ChannelItem : ChannelItemT<ChannelItem>
    {
        ChannelItem(hstring id, hstring name, hstring glyph, ChannelKind kind, std::wstring avatarUrl = {},
                    bool unread = false, int mentions = 0)
            : m_id(id), m_name(name), m_glyph(glyph), m_kind(kind), m_avatarUrl(std::move(avatarUrl)),
              m_unread(unread), m_mentions(mentions) {}

        // Discord: read channels are dimmed, unread ones white + semibold.
        Brush NameBrush() const { return SolidBrush(m_unread || m_mentions ? 0xF2F3F5 : 0x949BA4); }
        Windows::UI::Text::FontWeight NameWeight() const { return { static_cast<uint16_t>(m_unread || m_mentions ? 600 : 400) }; }
        hstring MentionText() const { return m_mentions > 99 ? hstring{ L"99+" } : hstring{ std::to_wstring(m_mentions) }; }
        Visibility MentionVisibility() const { return Show(m_mentions > 0); }
        bool Unread() const { return m_unread; }
        int Mentions() const { return m_mentions; }
        ChannelKind Kind() const { return m_kind; }
        std::wstring const& AvatarUrl() const { return m_avatarUrl; }

        // DMs only: presence dot + activity line (set once, before the item is shown).
        void SetPresence(uint32_t statusColor, std::wstring subtitle) { m_statusColor = statusColor; m_subtitle = std::move(subtitle); }
        void SetSpeaking(bool speaking) { m_speaking = speaking; }
        bool Speaking() const { return m_speaking; }
        Brush AvatarRing() const { return m_speaking ? SolidBrush(0x23A55A) : SolidBrush(0, 0); }
        uint32_t StatusColor() const { return m_statusColor; }
        std::wstring const& SubtitleText() const { return m_subtitle; }
        double AvatarSize() const { return IsVoiceUser() ? 24.0 : 32.0; }
        Brush StatusBrush() const { return SolidBrush(m_statusColor ? m_statusColor : 0x80848E); }
        Visibility StatusVisibility() const { return Show(m_statusColor != 0); }
        hstring Subtitle() const { return hstring{ m_subtitle }; }
        Visibility SubtitleVisibility() const { return Show(!m_subtitle.empty()); }

        hstring Id() const { return m_id; }
        hstring Name() const { return m_name; }
        hstring Glyph() const { return m_glyph; }
        bool IsCategory() const { return m_kind == ChannelKind::Category; }
        bool IsTextLike() const { return m_kind == ChannelKind::Text; }
        bool IsVoiceUser() const { return m_kind == ChannelKind::VoiceUser; }
        ImageSource Avatar();
        Visibility AvatarVisibility() const { return Show(!m_avatarUrl.empty()); }
        Visibility GlyphVisibility() const { return Show(m_avatarUrl.empty()); }
        Microsoft::UI::Xaml::Thickness ItemMargin() const { return { IsVoiceUser() ? 24.0 : 0.0, 0, 0, 0 }; }
        double TextOpacity() const { return IsVoiceUser() ? 0.8 : 1.0; }
        Visibility CategoryVisibility() const { return Show(IsCategory()); }
        Visibility ChannelVisibility() const { return Show(!IsCategory()); }

    private:
        hstring m_id, m_name, m_glyph;
        ChannelKind m_kind;
        std::wstring m_avatarUrl;
        bool m_unread;
        int m_mentions;
        uint32_t m_statusColor = 0;
        std::wstring m_subtitle;
        bool m_speaking = false;
    };

    struct FriendItem : FriendItemT<FriendItem>
    {
        // relationship: 1 friend, 3 incoming request, 4 outgoing request
        FriendItem(std::wstring id, std::wstring name, std::wstring tag, std::wstring subtitle,
                   std::wstring avatarUrl, uint32_t statusColor, int relationship)
            : m_id(std::move(id)), m_name(std::move(name)), m_tag(std::move(tag)), m_subtitle(std::move(subtitle)),
              m_avatarUrl(std::move(avatarUrl)), m_statusColor(statusColor), m_relationship(relationship) {}

        hstring Id() const { return hstring{ m_id }; }
        hstring Name() const { return hstring{ m_name }; }
        hstring Tag() const { return hstring{ m_tag }; }
        Visibility TagVisibility() const { return Show(!m_tag.empty()); }
        hstring Subtitle() const { return hstring{ m_subtitle }; }
        ImageSource Avatar();
        Brush StatusBrush() const { return SolidBrush(m_statusColor); }
        Visibility MessageVisibility() const { return Show(m_relationship == 1); }
        Visibility AcceptVisibility() const { return Show(m_relationship == 3); }
        hstring RemoveLabel() const
        {
            return m_relationship == 1 ? L"Retirer l'ami" : m_relationship == 3 ? L"Refuser" : L"Annuler la demande";
        }

    private:
        std::wstring m_id, m_name, m_tag, m_subtitle, m_avatarUrl;
        uint32_t m_statusColor;
        int m_relationship;
    };

    // One tile of the call screen.
    struct ParticipantItem : ParticipantItemT<ParticipantItem>
    {
        ParticipantItem(std::wstring userId, std::wstring name, std::wstring avatarUrl, bool speaking, bool muted, bool deaf)
            : m_userId(std::move(userId)), m_name(std::move(name)), m_avatarUrl(std::move(avatarUrl)),
              m_speaking(speaking), m_muted(muted), m_deaf(deaf) {}

        hstring UserId() const { return hstring{ m_userId }; }
        hstring Name() const { return hstring{ m_name }; }
        ImageSource Avatar();
        Brush RingBrush() const { return m_speaking ? SolidBrush(0x23A55A) : SolidBrush(0, 0); }
        Visibility MutedVisibility() const { return Show(m_muted && !m_deaf); }
        Visibility DeafVisibility() const { return Show(m_deaf); }
        std::wstring const& NameText() const { return m_name; }
        std::wstring const& AvatarUrl() const { return m_avatarUrl; }
        bool Muted() const { return m_muted; }
        bool Deaf() const { return m_deaf; }

    private:
        std::wstring m_userId, m_name, m_avatarUrl;
        bool m_speaking, m_muted, m_deaf;
    };

    struct MessageItem : MessageItemT<MessageItem>
    {
        MessageItem(::DiscordWin3::MessageData data, bool showHeader, std::wstring dayText)
            : m_d(std::move(data)), m_showHeader(showHeader), m_day(std::move(dayText)) {}

        hstring Id() const { return hstring{ m_d.id }; }
        hstring AuthorId() const { return hstring{ m_d.authorId }; }
        hstring AuthorName() const { return hstring{ m_d.authorName }; }
        Brush AuthorBrush() const { return SolidBrush(m_d.color ? m_d.color : 0xF2F3F5); }
        hstring Timestamp() const { return hstring{ m_d.timestamp }; }
        int64_t UnixMs() const { return m_d.unixMs; }
        ImageSource Avatar();
        Visibility HeaderVisibility() const { return Show(m_showHeader); }
        Microsoft::UI::Xaml::Thickness RowPadding() const { return { 16, m_showHeader ? 12.0 : 1.0, 16, 1 }; }
        Brush RowBackground() const { return m_d.mentionsMe ? SolidBrush(0xF0B232, 0x14) : SolidBrush(0, 0); }
        Brush MentionBar() const { return m_d.mentionsMe ? SolidBrush(0xF0B232) : SolidBrush(0, 0); }
        Visibility ReplyBarVisibility() const { return Show(!m_d.replyName.empty()); }
        ImageSource ReplyAvatar();
        hstring ReplyName() const { return hstring{ L"@" + m_d.replyName }; }
        Brush ReplyNameBrush() const { return SolidBrush(m_d.replyColor ? m_d.replyColor : 0xC4C9CE); }
        hstring ReplyTag() const { return hstring{ m_d.replyTag }; }
        Visibility ReplyTagVisibility() const { return Show(!m_d.replyTag.empty()); }
        hstring ReplySnippet() const { return hstring{ m_d.replySnippet }; }
        Windows::UI::Text::FontStyle ReplySnippetStyle() const { return m_d.replyAttachmentOnly ? Windows::UI::Text::FontStyle::Italic : Windows::UI::Text::FontStyle::Normal; }
        Visibility ReplyAttachmentVisibility() const { return Show(m_d.replyAttachmentOnly); }
        Visibility GalleryVisibility() const { return Show(!m_d.gallery.empty()); }
        Visibility ContentVisibility() const { return Show(m_d.HasBody()); }
        Visibility ReactionsVisibility() const { return Show(!m_d.reactions.empty()); }
        Visibility OwnVisibility() const { return Show(m_d.own); }
        Visibility OthersVisibility() const { return Show(!m_d.own); }
        Visibility DayVisibility() const { return Show(!m_day.empty()); }
        hstring DayText() const { return hstring{ m_day }; }
        hstring TagText() const { return hstring{ m_d.tag }; }
        Visibility TagVisibility() const { return Show(!m_d.tag.empty()); }
        hstring ReplyText() const { return hstring{ m_d.reply }; }
        Visibility ReplyVisibility() const { return Show(!m_d.reply.empty()); }
        ImageSource Image();
        Visibility ImageVisibility() const { return Show(!m_d.imageUrl.empty()); }
        Visibility VideoVisibility() const { return Show(m_d.isVideo); }
        double ImageWidth() const { return m_d.imageWidth; }
        double ImageHeight() const { return m_d.imageHeight; }
        hstring Files() const { return hstring{ m_d.files }; }
        Visibility FilesVisibility() const { return Show(!m_d.files.empty()); }
        Visibility EmbedVisibility() const { return Show(!m_d.embedTitle.empty() || !m_d.embedDescription.empty()); }
        Brush EmbedBar() const { return SolidBrush(m_d.embedColor ? m_d.embedColor : 0x4E5058); }
        hstring EmbedProvider() const { return hstring{ m_d.embedProvider }; }
        Visibility EmbedProviderVisibility() const { return Show(!m_d.embedProvider.empty()); }
        hstring EmbedTitle() const { return hstring{ m_d.embedTitle }; }
        Visibility EmbedTitleVisibility() const { return Show(!m_d.embedTitle.empty()); }
        hstring EmbedDescription() const { return hstring{ m_d.embedDescription }; }
        Visibility EmbedDescriptionVisibility() const { return Show(!m_d.embedDescription.empty()); }

        ::DiscordWin3::MessageData const& Data() const { return m_d; }
        bool ShowsHeader() const { return m_showHeader; }
        std::wstring const& Day() const { return m_day; }

    private:
        ::DiscordWin3::MessageData m_d;
        bool m_showHeader;
        std::wstring m_day;
    };

    struct MemberItem : MemberItemT<MemberItem>
    {
        // Group header row.
        MemberItem(std::wstring groupTitle) : m_isGroup(true), m_name(std::move(groupTitle)) {}
        // Member row. status: "online" | "idle" | "dnd" | other = offline.
        MemberItem(std::wstring name, uint32_t color, std::wstring avatarUrl, std::wstring status, std::wstring activity)
            : m_isGroup(false), m_name(std::move(name)), m_color(color), m_avatarUrl(std::move(avatarUrl)),
              m_status(std::move(status)), m_activity(std::move(activity)) {}

        bool IsGroup() const { return m_isGroup; }
        hstring Name() const { return hstring{ m_name }; }
        hstring Activity() const { return hstring{ m_activity }; }
        Brush NameBrush() const { return SolidBrush(m_color ? m_color : 0xDBDEE1); }
        Brush StatusBrush() const;
        ImageSource Avatar();
        double RowOpacity() const { return IsOffline() ? 0.4 : 1.0; }
        Visibility GroupVisibility() const { return Show(m_isGroup); }
        Visibility MemberVisibility() const { return Show(!m_isGroup); }
        Visibility ActivityVisibility() const { return Show(!m_isGroup && !m_activity.empty()); }

    private:
        bool IsOffline() const { return !m_isGroup && m_status != L"online" && m_status != L"idle" && m_status != L"dnd"; }

        bool m_isGroup;
        std::wstring m_name;
        uint32_t m_color = 0;
        std::wstring m_avatarUrl;
        std::wstring m_status;
        std::wstring m_activity;
    };
}
