#pragma once

#include <winrt/Windows.Graphics.Capture.h>

namespace DiscordWin3::Voice
{
    class ScreenShareImpl;

    // Go Live source: Windows.Graphics.Capture -> D3D11 video processor (scale + BGRA->NV12 on the GPU)
    // -> Media Foundation H.264 encoder (low latency, CBR) -> Annex B access units.
    // D3D / Media Foundation headers stay in the .cpp (they clash with WinUI's `Microsoft` namespace).
    class ScreenShare
    {
    public:
        using FrameCallback = std::function<void(uint8_t const* annexB, size_t length, uint32_t timestamp90k)>;

        ScreenShare();
        ~ScreenShare();

        // Shows the system picker (screens and windows). Null if cancelled. UI thread.
        static winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Graphics::Capture::GraphicsCaptureItem> PickAsync(HWND owner);

        bool Start(winrt::Windows::Graphics::Capture::GraphicsCaptureItem const& item, FrameCallback onFrame,
                   uint32_t width = 1280, uint32_t height = 720, uint32_t fps = 30, uint32_t bitrate = 2500000);
        void Stop();
        bool Running() const;
        std::wstring const& Error() const;

    private:
        std::unique_ptr<ScreenShareImpl> m_impl;
    };
}
