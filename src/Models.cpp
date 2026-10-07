#include "pch.h"
#include "Models.h"
#include "ImageCache.h"

#if __has_include("GuildItem.g.cpp")
#include "GuildItem.g.cpp"
#endif
#if __has_include("ChannelItem.g.cpp")
#include "ChannelItem.g.cpp"
#endif
#if __has_include("MessageItem.g.cpp")
#include "MessageItem.g.cpp"
#endif
#if __has_include("FriendItem.g.cpp")
#include "FriendItem.g.cpp"
#endif
#if __has_include("MemberItem.g.cpp")
#include "MemberItem.g.cpp"
#endif

namespace winrt::DiscordWin3::implementation
{
    namespace
    {
        hstring MakeInitials(std::wstring_view name)
        {
            // "Mon Super Serveur" -> "MSS" (Discord style, capped at 4 chars)
            std::wstring out;
            bool atWordStart = true;
            for (wchar_t c : name)
            {
                if (c == L' ')
                {
                    atWordStart = true;
                    continue;
                }
                if (atWordStart)
                {
                    out.push_back(c);
                    atWordStart = false;
                    if (out.size() == 4)
                    {
                        break;
                    }
                }
            }
            return hstring{ out };
        }
    }

    Brush SolidBrush(uint32_t rgb, uint8_t alpha)
    {
        static std::unordered_map<uint64_t, Microsoft::UI::Xaml::Media::SolidColorBrush> brushes;
        uint64_t key = (static_cast<uint64_t>(alpha) << 32) | rgb;
        auto it = brushes.find(key);
        if (it == brushes.end())
        {
            Windows::UI::Color c{ alpha, static_cast<uint8_t>(rgb >> 16), static_cast<uint8_t>(rgb >> 8), static_cast<uint8_t>(rgb) };
            it = brushes.emplace(key, Microsoft::UI::Xaml::Media::SolidColorBrush{ c }).first;
        }
        return it->second;
    }

    GuildItem::GuildItem(hstring id, hstring name, hstring iconUrl, bool unread, int mentions)
        : m_id(id), m_name(name), m_initials(id == L"@me" ? hstring{ L"\U0001F4AC" } : MakeInitials(name)), m_iconUrl(iconUrl), m_unread(unread), m_mentions(mentions)
    {
    }

    ImageSource GuildItem::Icon()
    {
        return ::DiscordWin3::ImageCache::Get(std::wstring{ m_iconUrl }, 48);
    }

    ImageSource ChannelItem::Avatar()
    {
        return ::DiscordWin3::ImageCache::Get(m_avatarUrl, IsVoiceUser() ? 24 : 32);
    }

    ImageSource MessageItem::Avatar()
    {
        return m_showHeader ? ::DiscordWin3::ImageCache::Get(m_d.avatarUrl, 40) : nullptr;
    }

    ImageSource MessageItem::Image()
    {
        return ::DiscordWin3::ImageCache::Get(m_d.imageUrl, static_cast<int>(m_d.imageWidth));
    }

    ImageSource FriendItem::Avatar()
    {
        return ::DiscordWin3::ImageCache::Get(m_avatarUrl, 32);
    }

    ImageSource MemberItem::Avatar()
    {
        return m_isGroup ? nullptr : ::DiscordWin3::ImageCache::Get(m_avatarUrl, 32);
    }

    Brush MemberItem::StatusBrush() const
    {
        if (m_status == L"online") return SolidBrush(0x23A55A);
        if (m_status == L"idle") return SolidBrush(0xF0B232);
        if (m_status == L"dnd") return SolidBrush(0xF23F43);
        return SolidBrush(0x80848E);
    }
}
