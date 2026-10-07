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

    GuildItem::GuildItem(hstring id, hstring name, hstring iconUrl)
        : m_id(id), m_name(name), m_initials(id == L"@me" ? hstring{ L"\U0001F4AC" } : MakeInitials(name)), m_iconUrl(iconUrl)
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

    Microsoft::UI::Xaml::Media::Brush MessageItem::AuthorBrush()
    {
        // One shared brush per role color (UI thread only).
        static std::unordered_map<uint32_t, Microsoft::UI::Xaml::Media::SolidColorBrush> brushes;
        uint32_t rgb = m_d.color ? m_d.color : 0xF2F3F5;
        auto it = brushes.find(rgb);
        if (it == brushes.end())
        {
            Windows::UI::Color c{ 255, static_cast<uint8_t>(rgb >> 16), static_cast<uint8_t>(rgb >> 8), static_cast<uint8_t>(rgb) };
            it = brushes.emplace(rgb, Microsoft::UI::Xaml::Media::SolidColorBrush{ c }).first;
        }
        return it->second;
    }

    ImageSource MessageItem::Avatar()
    {
        return m_showHeader ? ::DiscordWin3::ImageCache::Get(m_d.avatarUrl, 40) : nullptr;
    }

    ImageSource MessageItem::Image()
    {
        return ::DiscordWin3::ImageCache::Get(m_d.imageUrl, static_cast<int>(m_d.imageWidth));
    }
}
