#include "blast_sound.h"

#include <algorithm>
#include <cmath>

namespace kosmos
{
    namespace
    {
        constexpr double pi = 3.14159265358979323846;

        void put_le(std::vector<std::uint8_t> &out, std::uint32_t v, int bytes)
        {
            for (int i = 0; i < bytes; ++i)
                out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xff));
        }
    } // namespace

    // A blast as a mono WAV in memory. Kind 0 is a bomb: a sharp crack and a deep thump. Kind 1 is a nuclear burst: a long,
    // bassy, rolling rumble under the thump.
    std::vector<std::uint8_t> make_blast_wav(int kind)
    {
        const int rate = 22050;
        const double seconds = kind == 0 ? 5.0 : 16.0;
        const int count = static_cast<int>(rate * seconds);
        std::uint32_t seed = 4242u + 99u * static_cast<std::uint32_t>(kind);
        const auto noise = [&seed]
        {
            seed = seed * 1664525u + 1013904223u;
            return static_cast<double>(static_cast<std::int32_t>(seed)) / 2147483648.0;
        };
        std::vector<std::int16_t> pcm(static_cast<std::size_t>(count));
        double low1 = 0.0, low2 = 0.0, brown = 0.0, phase = 0.0, phase2 = 0.0;
        for (int i = 0; i < count; ++i)
        {
            const double t = static_cast<double>(i) / rate;
            // The thump: a sine that sinks in pitch as it dies away, with a second octave to carry on small speakers.
            const double f = kind == 0 ? 38.0 + 70.0 * std::exp(-t * 6.0) : 24.0 + 40.0 * std::exp(-t * 1.6);
            phase += 2.0 * pi * f / rate;
            phase2 += 2.0 * pi * f * 2.01 / rate;
            const double attack = kind == 0 ? 0.004 : 0.35;
            const double rise = std::min(1.0, t / attack);
            const double body = std::exp(-t / (kind == 0 ? 0.55 : 3.6));
            const double thump = (std::sin(phase) + 0.45 * std::sin(phase2) * std::exp(-t * 1.5)) * rise * body;

            // Rolling rumble: low-passed noise whose cutoff sinks, swelled by slow irregular waves.
            const double cutoff = (kind == 0 ? 240.0 : 150.0) * std::exp(-t * (kind == 0 ? 2.0 : 0.35)) + 40.0;
            const double a = 1.0 - std::exp(-2.0 * pi * cutoff / rate);
            const double white = noise();
            low1 += a * (white - low1);
            low2 += a * (low1 - low2);
            brown = brown * 0.999 + white * 0.02;
            const double roll = 0.7 + 0.3 * std::sin(2.0 * pi * 1.7 * t + 0.6) * std::sin(2.0 * pi * 0.63 * t + 1.9);
            const double rumble = (low2 * 5.0 + brown * 1.6) * roll * rise * std::exp(-t / (kind == 0 ? 1.3 : 6.0));
            const double crack = kind == 0 ? white * std::exp(-t * 40.0) * 0.5 : white * std::exp(-t * 14.0) * 0.12;

            const double v = (thump * 0.8 + rumble * 0.55 + crack) * 0.95;
            pcm[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(std::clamp(std::tanh(v * 1.4), -1.0, 1.0) * 30000.0);
        }
        std::vector<std::uint8_t> wav = {'R', 'I', 'F', 'F'};
        put_le(wav, 36u + static_cast<std::uint32_t>(count) * 2u, 4);
        for (char c : {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '})
            wav.push_back(static_cast<std::uint8_t>(c));
        put_le(wav, 16, 4);
        put_le(wav, 1, 2);
        put_le(wav, 1, 2);
        put_le(wav, static_cast<std::uint32_t>(rate), 4);
        put_le(wav, static_cast<std::uint32_t>(rate) * 2u, 4);
        put_le(wav, 2, 2);
        put_le(wav, 16, 2);
        for (char c : {'d', 'a', 't', 'a'})
            wav.push_back(static_cast<std::uint8_t>(c));
        put_le(wav, static_cast<std::uint32_t>(count) * 2u, 4);
        for (std::int16_t s : pcm)
            put_le(wav, static_cast<std::uint16_t>(s), 2);
        return wav;
    }
} // namespace kosmos
