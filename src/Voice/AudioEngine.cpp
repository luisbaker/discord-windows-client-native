#include "pch.h"
#include "AudioEngine.h"

#include <audioclient.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <wil/com.h>
#include <cmath>

namespace DiscordWin3::Voice
{
    namespace
    {
        constexpr REFERENCE_TIME BufferDuration = 400000;   // 40 ms (100 ns units)
        constexpr size_t PrimeSamples = AudioEngine::FrameSamples * AudioEngine::Channels * 2;   // 40 ms before playing
        constexpr size_t MaxQueuedSamples = AudioEngine::SampleRate / 5 * AudioEngine::Channels;  // 200 ms cap (latency)

        WAVEFORMATEX Format()
        {
            WAVEFORMATEX f{};
            f.wFormatTag = WAVE_FORMAT_PCM;
            f.nChannels = AudioEngine::Channels;
            f.nSamplesPerSec = AudioEngine::SampleRate;
            f.wBitsPerSample = 16;
            f.nBlockAlign = f.nChannels * f.wBitsPerSample / 8;
            f.nAvgBytesPerSec = f.nSamplesPerSec * f.nBlockAlign;
            return f;
        }

        wil::com_ptr_nothrow<IAudioClient> OpenClient(EDataFlow flow)
        {
            wil::com_ptr_nothrow<IMMDeviceEnumerator> enumerator;
            if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) return nullptr;
            wil::com_ptr_nothrow<IMMDevice> device;
            // eCommunications = the device the user picked for calls in Windows sound settings.
            if (FAILED(enumerator->GetDefaultAudioEndpoint(flow, eCommunications, &device))) return nullptr;
            wil::com_ptr_nothrow<IAudioClient> client;
            if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, client.put_void()))) return nullptr;
            auto format = Format();
            DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
            if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, BufferDuration, 0, &format, nullptr))) return nullptr;
            return client;
        }

        struct ThreadSetup
        {
            ThreadSetup()
            {
                CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                DWORD index = 0;
                task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &index);
            }
            ~ThreadSetup()
            {
                if (task) AvRevertMmThreadCharacteristics(task);
                CoUninitialize();
            }
            HANDLE task = nullptr;
        };
    }

    AudioEngine::~AudioEngine()
    {
        Stop();
    }

    void AudioEngine::Start(FrameCallback onFrame)
    {
        Stop();
        m_onFrame = std::move(onFrame);
        m_running = true;
        m_capture = std::thread([this] { CaptureLoop(); });
        m_render = std::thread([this] { RenderLoop(); });
    }

    void AudioEngine::Stop()
    {
        m_running = false;
        if (m_capture.joinable()) m_capture.join();
        if (m_render.joinable()) m_render.join();
        std::lock_guard guard{ m_mixLock };
        m_sources.clear();
    }

    void AudioEngine::CaptureLoop()
    {
        ThreadSetup setup;
        auto client = OpenClient(eCapture);
        if (!client) return;
        wil::unique_event ready{ CreateEventW(nullptr, FALSE, FALSE, nullptr) };
        client->SetEventHandle(ready.get());
        wil::com_ptr_nothrow<IAudioCaptureClient> capture;
        if (FAILED(client->GetService(IID_PPV_ARGS(&capture)))) return;
        client->Start();

        std::vector<int16_t> pending;
        pending.reserve(FrameSamples * Channels * 4);
        while (m_running)
        {
            if (WaitForSingleObject(ready.get(), 100) != WAIT_OBJECT_0) continue;
            UINT32 packet = 0;
            while (SUCCEEDED(capture->GetNextPacketSize(&packet)) && packet > 0)
            {
                BYTE* data = nullptr;
                UINT32 frames = 0;
                DWORD flags = 0;
                if (FAILED(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;
                auto samples = reinterpret_cast<int16_t const*>(data);
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) pending.insert(pending.end(), frames * Channels, 0);
                else pending.insert(pending.end(), samples, samples + frames * Channels);
                capture->ReleaseBuffer(frames);
            }

            constexpr size_t frameSize = FrameSamples * Channels;
            while (pending.size() >= frameSize)
            {
                // Speaking detection: RMS above ~-45 dBFS, with 300 ms hangover so words don't get clipped.
                double energy = 0;
                for (size_t i = 0; i < frameSize; ++i) energy += static_cast<double>(pending[i]) * pending[i];
                double rms = std::sqrt(energy / frameSize) / 32768.0;
                if (rms > 0.0056) m_hangover = 15;
                else if (m_hangover > 0) --m_hangover;
                bool voiced = m_hangover > 0 && !m_muted && !m_deafened;

                if (m_onFrame) m_onFrame(pending.data(), voiced);
                pending.erase(pending.begin(), pending.begin() + frameSize);
            }
        }
        client->Stop();
    }

    void AudioEngine::RenderLoop()
    {
        ThreadSetup setup;
        auto client = OpenClient(eRender);
        if (!client) return;
        wil::unique_event ready{ CreateEventW(nullptr, FALSE, FALSE, nullptr) };
        client->SetEventHandle(ready.get());
        wil::com_ptr_nothrow<IAudioRenderClient> render;
        if (FAILED(client->GetService(IID_PPV_ARGS(&render)))) return;
        UINT32 bufferFrames = 0;
        client->GetBufferSize(&bufferFrames);
        client->Start();

        while (m_running)
        {
            if (WaitForSingleObject(ready.get(), 100) != WAIT_OBJECT_0) continue;
            UINT32 padding = 0;
            if (FAILED(client->GetCurrentPadding(&padding))) break;
            UINT32 frames = bufferFrames - padding;
            if (frames == 0) continue;
            BYTE* data = nullptr;
            if (FAILED(render->GetBuffer(frames, &data))) break;
            Mix(reinterpret_cast<int16_t*>(data), frames);
            render->ReleaseBuffer(frames, 0);
        }
        client->Stop();
    }

    void AudioEngine::PushPlayback(uint32_t ssrc, int16_t const* pcm, size_t frames)
    {
        std::lock_guard guard{ m_mixLock };
        auto& source = m_sources[ssrc];
        source.samples.insert(source.samples.end(), pcm, pcm + frames * Channels);
        // Too far behind (network burst): drop the oldest audio to keep latency low.
        while (source.samples.size() > MaxQueuedSamples) source.samples.pop_front();
    }

    void AudioEngine::RemoveSource(uint32_t ssrc)
    {
        std::lock_guard guard{ m_mixLock };
        m_sources.erase(ssrc);
    }

    void AudioEngine::Mix(int16_t* out, size_t frames)
    {
        size_t count = frames * Channels;
        std::fill(out, out + count, int16_t{ 0 });
        if (m_deafened) return;

        std::lock_guard guard{ m_mixLock };
        for (auto& [ssrc, source] : m_sources)
        {
            if (!source.primed)
            {
                if (source.samples.size() < PrimeSamples) continue;
                source.primed = true;
            }
            size_t take = std::min(count, source.samples.size());
            for (size_t i = 0; i < take; ++i)
            {
                int mixed = out[i] + source.samples[i];
                out[i] = static_cast<int16_t>(std::clamp(mixed, -32768, 32767));
            }
            source.samples.erase(source.samples.begin(), source.samples.begin() + take);
            if (source.samples.empty()) source.primed = false;   // underrun: rebuffer a little
        }
    }
}
