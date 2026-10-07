#include "pch.h"
#include "ImageCache.h"

#include <list>

using namespace winrt;
using namespace Microsoft::UI::Xaml::Media::Imaging;

namespace
{
    constexpr size_t MaxEntries = 300;

    struct Entry
    {
        std::wstring key;
        BitmapImage image{ nullptr };
    };

    std::list<Entry> g_lru;   // front = most recent
    std::unordered_map<std::wstring, std::list<Entry>::iterator> g_index;
}

namespace DiscordWin3::ImageCache
{
    Microsoft::UI::Xaml::Media::ImageSource Get(std::wstring const& url, int decodeWidth)
    {
        if (url.empty())
        {
            return nullptr;
        }

        std::wstring key = url + L"|" + std::to_wstring(decodeWidth);
        if (auto it = g_index.find(key); it != g_index.end())
        {
            g_lru.splice(g_lru.begin(), g_lru, it->second);
            return it->second->image;
        }

        BitmapImage image;
        image.DecodePixelType(DecodePixelType::Logical);
        image.DecodePixelWidth(decodeWidth);
        image.UriSource(Windows::Foundation::Uri{ url });

        g_lru.push_front({ key, image });
        g_index[key] = g_lru.begin();

        while (g_lru.size() > MaxEntries)
        {
            g_index.erase(g_lru.back().key);
            g_lru.pop_back();
        }
        return image;
    }

    void Clear()
    {
        g_index.clear();
        g_lru.clear();
    }
}
