#pragma once

// UI-thread-only cache of BitmapImage objects. Every image is decoded at its display size
// (DecodePixelWidth), which is the single biggest RAM win versus a browser engine.
namespace DiscordWin3::ImageCache
{
    winrt::Microsoft::UI::Xaml::Media::ImageSource Get(std::wstring const& url, int decodeWidth);
    void Clear();
}
