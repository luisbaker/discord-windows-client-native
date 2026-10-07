#pragma once

#include "GuildItem.g.h"
#include "ChannelItem.g.h"
#include "MessageItem.g.h"

namespace DiscordWin3
{
    // Plain data handed to the item constructors (kept out of the WinRT surface).
    struct MessageData
    {
        std::wstring id;
        std::wstring authorId;
        std::wstring authorName;
        std::wstring avatarUrl;   // already sized CDN url, empty if none
        std::wstring content;
        std::wstring timestamp;   // display string
        int64_t unixMs = 0;
        std::wstring imageUrl;    // first image attachment (proxied, resized)
        double imageWidth = 0;
        double imageHeight = 0;
        std::wstring files;       // other attachments, one per line
    };
}

namespace winrt::DiscordWin3::implementation
{
    using Microsoft::UI::Xaml::Visibility;
    using Microsoft::UI::Xaml::Media::ImageSource;

    struct GuildItem : GuildItemT<GuildItem>
    {
        GuildItem(hstring id, hstring name, hstring iconUrl);

        hstring Id() const { return m_id; }
        hstring Name() const { return m_name; }
        hstring Initials() const { return m_initials; }
        ImageSource Icon();
        Visibility IconVisibility() const { return m_iconUrl.empty() ? Visibility::Collapsed : Visibility::Visible; }
        Visibility InitialsVisibility() const { return m_iconUrl.empty() ? Visibility::Visible : Visibility::Collapsed; }

    private:
        hstring m_id, m_name, m_initials, m_iconUrl;
    };

    struct ChannelItem : ChannelItemT<ChannelItem>
    {
        ChannelItem(hstring id, hstring name, hstring glyph, bool isCategory, bool isTextLike)
            : m_id(id), m_name(name), m_glyph(glyph), m_isCategory(isCategory), m_isTextLike(isTextLike) {}

        hstring Id() const { return m_id; }
        hstring Name() const { return m_name; }
        hstring Glyph() const { return m_glyph; }
        bool IsCategory() const { return m_isCategory; }
        bool IsTextLike() const { return m_isTextLike; }
        Visibility CategoryVisibility() const { return m_isCategory ? Visibility::Visible : Visibility::Collapsed; }
        Visibility ChannelVisibility() const { return m_isCategory ? Visibility::Collapsed : Visibility::Visible; }

    private:
        hstring m_id, m_name, m_glyph;
        bool m_isCategory, m_isTextLike;
    };

    struct MessageItem : MessageItemT<MessageItem>
    {
        MessageItem(::DiscordWin3::MessageData data, bool showHeader)
            : m_d(std::move(data)), m_showHeader(showHeader) {}

        hstring Id() const { return hstring{ m_d.id }; }
        hstring AuthorId() const { return hstring{ m_d.authorId }; }
        hstring AuthorName() const { return hstring{ m_d.authorName }; }
        hstring Content() const { return hstring{ m_d.content }; }
        hstring Timestamp() const { return hstring{ m_d.timestamp }; }
        int64_t UnixMs() const { return m_d.unixMs; }
        ImageSource Avatar();
        Visibility HeaderVisibility() const { return m_showHeader ? Visibility::Visible : Visibility::Collapsed; }
        Microsoft::UI::Xaml::Thickness RowPadding() const { return { 16, m_showHeader ? 12.0 : 1.0, 16, 1 }; }
        Visibility ContentVisibility() const { return m_d.content.empty() ? Visibility::Collapsed : Visibility::Visible; }
        ImageSource Image();
        Visibility ImageVisibility() const { return m_d.imageUrl.empty() ? Visibility::Collapsed : Visibility::Visible; }
        double ImageWidth() const { return m_d.imageWidth; }
        double ImageHeight() const { return m_d.imageHeight; }
        hstring Files() const { return hstring{ m_d.files }; }
        Visibility FilesVisibility() const { return m_d.files.empty() ? Visibility::Collapsed : Visibility::Visible; }

        ::DiscordWin3::MessageData const& Data() const { return m_d; }
        bool ShowsHeader() const { return m_showHeader; }

    private:
        ::DiscordWin3::MessageData m_d;
        bool m_showHeader;
    };
}
