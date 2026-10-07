#include "pch.h"
#include "Sounds.h"

#include <mmsystem.h>
#include <cmath>

namespace DiscordWin3::Voice
{
    namespace
    {
        struct Note
        {
            double frequency;   // Hz (0 = rest)
            int ms;
        };

        // 48 kHz mono 16-bit WAV in memory: soft sine notes with a short attack and exponential decay.
        std::vector<uint8_t> Synthesize(std::initializer_list<Note> notes, double volume)
        {
            constexpr int rate = 48000;
            std::vector<int16_t> pcm;
            for (auto const& note : notes)
            {
                int count = rate * note.ms / 1000;
                for (int i = 0; i < count; ++i)
                {
                    double t = static_cast<double>(i) / rate;
                    double attack = std::min(1.0, i / (rate * 0.008));
                    double decay = std::exp(-t * 9.0);
                    double sample = note.frequency > 0
                        ? (std::sin(2 * 3.14159265358979 * note.frequency * t) + 0.25 * std::sin(4 * 3.14159265358979 * note.frequency * t)) / 1.25
                        : 0.0;
                    pcm.push_back(static_cast<int16_t>(sample * attack * decay * volume * 32767));
                }
            }

            uint32_t dataSize = static_cast<uint32_t>(pcm.size() * 2);
            std::vector<uint8_t> wav(44 + dataSize);
            auto put32 = [&](size_t at, uint32_t v) { memcpy(wav.data() + at, &v, 4); };
            auto put16 = [&](size_t at, uint16_t v) { memcpy(wav.data() + at, &v, 2); };
            memcpy(wav.data(), "RIFF", 4);
            put32(4, 36 + dataSize);
            memcpy(wav.data() + 8, "WAVEfmt ", 8);
            put32(16, 16);
            put16(20, 1);            // PCM
            put16(22, 1);            // mono
            put32(24, rate);
            put32(28, rate * 2);
            put16(32, 2);
            put16(34, 16);
            memcpy(wav.data() + 36, "data", 4);
            put32(40, dataSize);
            memcpy(wav.data() + 44, pcm.data(), dataSize);
            return wav;
        }

        std::vector<uint8_t> const& Get(Sound sound)
        {
            // Built lazily, kept for the app lifetime (PlaySound reads the buffer asynchronously).
            static std::unordered_map<int, std::vector<uint8_t>> cache;
            static std::mutex lock;
            std::lock_guard guard{ lock };
            auto& wav = cache[static_cast<int>(sound)];
            if (!wav.empty()) return wav;
            switch (sound)
            {
            case Sound::SelfJoin:    wav = Synthesize({ { 659.25, 90 }, { 880.0, 110 }, { 1318.5, 220 } }, 0.30); break;   // rising E5-A5-E6
            case Sound::SelfLeave:   wav = Synthesize({ { 1318.5, 90 }, { 880.0, 110 }, { 587.33, 240 } }, 0.30); break;  // falling
            case Sound::UserJoin:    wav = Synthesize({ { 783.99, 80 }, { 1174.7, 200 } }, 0.22); break;
            case Sound::UserLeave:   wav = Synthesize({ { 1174.7, 80 }, { 783.99, 200 } }, 0.22); break;
            case Sound::Mute:        wav = Synthesize({ { 523.25, 70 }, { 392.0, 110 } }, 0.20); break;
            case Sound::Unmute:      wav = Synthesize({ { 392.0, 70 }, { 523.25, 110 } }, 0.20); break;
            case Sound::Deafen:      wav = Synthesize({ { 440.0, 70 }, { 329.63, 70 }, { 261.63, 120 } }, 0.20); break;
            case Sound::Undeafen:    wav = Synthesize({ { 261.63, 70 }, { 329.63, 70 }, { 440.0, 120 } }, 0.20); break;
            case Sound::StreamStart: wav = Synthesize({ { 880.0, 70 }, { 1108.7, 70 }, { 1318.5, 70 }, { 1760.0, 200 } }, 0.25); break;
            case Sound::StreamStop:  wav = Synthesize({ { 1760.0, 70 }, { 1318.5, 70 }, { 1108.7, 70 }, { 880.0, 200 } }, 0.25); break;
            }
            return wav;
        }
    }

    void Play(Sound sound)
    {
        auto const& wav = Get(sound);
        PlaySoundW(reinterpret_cast<LPCWSTR>(wav.data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
    }
}
