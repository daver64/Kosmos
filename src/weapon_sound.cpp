#include "weapon_sound.h"

#include <algorithm>
#include <cmath>

namespace kosmos
{
    namespace
    {
        constexpr double pi = 3.14159265358979323846;
        constexpr int sample_rate = 22050;

        std::vector<std::uint8_t> wav_of(const std::vector<std::int16_t> &pcm)
        {
            std::vector<std::uint8_t> wav = {'R', 'I', 'F', 'F'};
            auto put = [&wav](std::uint32_t v, int bytes) {
                for (int i = 0; i < bytes; ++i)
                    wav.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xff));
            };
            put(36u + static_cast<std::uint32_t>(pcm.size()) * 2u, 4);
            for (char c : {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '})
                wav.push_back(static_cast<std::uint8_t>(c));
            put(16, 4);
            put(1, 2);
            put(1, 2);
            put(sample_rate, 4);
            put(sample_rate * 2, 4);
            put(2, 2);
            put(16, 2);
            for (char c : {'d', 'a', 't', 'a'})
                wav.push_back(static_cast<std::uint8_t>(c));
            put(static_cast<std::uint32_t>(pcm.size()) * 2u, 4);
            for (std::int16_t s : pcm)
                put(static_cast<std::uint16_t>(s), 2);
            return wav;
        }

        struct Noise
        {
            std::uint32_t seed;
            double operator()()
            {
                seed = seed * 1664525u + 1013904223u;
                return static_cast<double>(static_cast<std::int32_t>(seed)) / 2147483648.0;
            }
        };
    } // namespace

    std::vector<std::uint8_t> make_beam_wav(double seconds, double pitch)
    {
        seconds = std::clamp(seconds, 0.3, 12.0);
        const int count = static_cast<int>(sample_rate * seconds);
        Noise noise{777u};
        std::vector<std::int16_t> pcm(static_cast<std::size_t>(count));
        double thump_phase = 0.0, low = 0.0, crackle = 0.0;
        for (int i = 0; i < count; ++i)
        {
            const double t = static_cast<double>(i) / sample_rate;
            const double u = t / seconds;
            // The whump: a sine that drops fast in pitch, with a soft burst of low noise on top.
            const double f = 28.0 + 95.0 * std::exp(-t * 10.0);
            thump_phase += 2.0 * pi * f / sample_rate;
            low += 0.04 * (noise() - low);
            const double whump = (std::sin(thump_phase) * std::exp(-t * 4.0) + low * 9.0 * std::exp(-t * 9.0)) * std::min(1.0, t / 0.004);

            // The hum swells in just after the whump and dies away with the beam; odd harmonics and a slow wobble make it electrical.
            const double swell = std::min(1.0, t / 0.12) * std::pow(std::max(0.0, 1.0 - u), 1.4);
            const double base = 55.0 * pitch * (1.0 + 0.012 * std::sin(2.0 * pi * 6.5 * t));
            const double w = 2.0 * pi * base * t;
            double hum = std::sin(w) + 0.55 * std::sin(2.0 * w + 0.6) + 0.4 * std::sin(3.0 * w + 1.1) + 0.22 * std::sin(5.0 * w) + 0.12 * std::sin(7.0 * w + 2.0);
            hum *= 0.75 + 0.25 * std::sin(2.0 * pi * 100.0 * pitch * t);
            if (noise() > 0.995)
                crackle = 0.8 * noise();
            crackle *= 0.9;
            const double v = whump * 1.0 + (hum * 0.42 + crackle * 0.5) * swell;
            pcm[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(std::clamp(std::tanh(v * 1.3), -1.0, 1.0) * 30000.0);
        }
        return wav_of(pcm);
    }

    std::vector<std::uint8_t> make_zap_wav()
    {
        const double seconds = 0.38;
        const int count = static_cast<int>(sample_rate * seconds);
        Noise noise{31337u};
        std::vector<std::int16_t> pcm(static_cast<std::size_t>(count));
        double phase = 0.0, mod_phase = 0.0, body_phase = 0.0;
        for (int i = 0; i < count; ++i)
        {
            const double t = static_cast<double>(i) / sample_rate;
            // A bright note that falls steeply in pitch, with fast frequency wobble for the plasma "pew".
            const double f = 380.0 + 2600.0 * std::exp(-t * 20.0);
            mod_phase += 2.0 * pi * 95.0 / sample_rate;
            phase += 2.0 * pi * f * (1.0 + 0.18 * std::sin(mod_phase)) / sample_rate;
            body_phase += 2.0 * pi * (110.0 + 90.0 * std::exp(-t * 25.0)) / sample_rate;
            const double attack = std::min(1.0, t / 0.0015);
            const double tone = (std::sin(phase) + 0.5 * std::sin(2.0 * phase) + 0.3 * std::sin(3.0 * phase)) * std::exp(-t * 9.0);
            const double thump = std::sin(body_phase) * std::exp(-t * 22.0) * 0.8;
            const double tick = noise() * std::exp(-t * 90.0) * 0.8;
            const double v = (tone * 0.55 + thump + tick) * attack;
            pcm[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(std::clamp(std::tanh(v * 1.5), -1.0, 1.0) * 30000.0);
        }
        return wav_of(pcm);
    }
} // namespace kosmos
