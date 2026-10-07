#pragma once

#include <deque>
#include <thread>

namespace DiscordWin3::Voice
{
    // WASAPI shared-mode capture + render at 48 kHz / stereo / 16-bit (Windows converts from the
    // device format), on two MMCSS "Pro Audio" threads. Opus and networking live elsewhere.
    class AudioEngine
    {
    public:
        static constexpr int SampleRate = 48000;
        static constexpr int Channels = 2;
        static constexpr int FrameSamples = 960;   // 20 ms per channel (one Opus frame)

        // Called on the capture thread for every 20 ms frame. `voiced` = above the speaking threshold.
        using FrameCallback = std::function<void(int16_t const* pcm, bool voiced)>;

        AudioEngine() = default;
        ~AudioEngine();

        void Start(FrameCallback onFrame);
        void Stop();

        void SetMuted(bool muted) { m_muted = muted; }
        void SetDeafened(bool deafened) { m_deafened = deafened; }

        // Decoded remote audio (interleaved stereo) for one sender; mixed on the render thread.
        void PushPlayback(uint32_t ssrc, int16_t const* pcm, size_t frames);
        void RemoveSource(uint32_t ssrc);

    private:
        void CaptureLoop();
        void RenderLoop();
        void Mix(int16_t* out, size_t frames);

        FrameCallback m_onFrame;
        std::thread m_capture;
        std::thread m_render;
        std::atomic<bool> m_running{ false };
        std::atomic<bool> m_muted{ false };
        std::atomic<bool> m_deafened{ false };

        struct Source
        {
            std::deque<int16_t> samples;   // interleaved stereo
            bool primed = false;           // waits for a little buffer before playing (jitter)
        };
        std::mutex m_mixLock;
        std::unordered_map<uint32_t, Source> m_sources;
        int m_hangover = 0;                // frames left before "not speaking"
    };
}
