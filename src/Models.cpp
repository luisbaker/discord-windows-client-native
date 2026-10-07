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
        : m_id(id), m_name(name), m_initials(MakeInitials(name)), m_iconUrl(iconUrl)
    {
    }

    ImageSource GuildItem::Icon()
    {
        return ::DiscordWin3::ImageCache::Get(std::wstring{ m_iconUrl }, 48);
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
