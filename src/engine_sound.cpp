#include "engine_sound.h"

#include "example_fx.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace kosmos
{
    namespace
    {
    // Ten seconds of wind: band-limited noise whose strength drifts slowly and irregularly, so it does not pulse like an engine.
    sl::Sample *make_breeze_sound()
    {
        const int rate = 22050, fade = rate, frames = rate * 10;
        std::uint32_t state = 0xBEEF1u;
        auto noise = [&]()
        {
            state = state * 1664525u + 1013904223u;
            return static_cast<float>(state >> 8) / 8388608.0f - 1.0f;
        };
        std::vector<float> raw(static_cast<std::size_t>(frames + fade));
        float a = 0.0f, b = 0.0f, c = 0.0f;
        for (float &v : raw)
        {
            const float n = noise();
            a += 0.06f * (n - a);
            b += 0.012f * (n - b);
            c += 0.25f * (n - c);
            v = (a - b) * 2.2f + (c - a) * 0.5f;
        }
        std::vector<std::int16_t> pcm(static_cast<std::size_t>(frames));
        for (int i = 0; i < frames; ++i)
        {
            float v = raw[static_cast<std::size_t>(i)];
            if (i < fade)
            {
                const float k = 1.5707963f * static_cast<float>(i) / fade;
                v = v * std::sin(k) + raw[static_cast<std::size_t>(frames + i)] * std::cos(k);
            }
            const double t = static_cast<double>(i) / frames;
            const float gust = 0.7f + 0.15f * static_cast<float>(std::sin(6.2831853 * t) + 0.8 * std::sin(6.2831853 * 3.0 * t + 1.0) + 0.5 * std::sin(6.2831853 * 7.0 * t + 2.0));
            pcm[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(std::clamp(v * gust * 0.6f, -1.0f, 1.0f) * 30000.0f);
        }
        return sl::create_sample_from_pcm(pcm.data(), pcm.size(), rate, 1);
    }
    } // namespace

    sl::Sample *EngineSound::effect(const std::string &name)
    {
        auto it = effects.find(name);
        if (it == effects.end())
            it = effects.emplace(name, sl::load_sample(data_root + "/data/sounds/" + name + ".wav")).first;
        return it->second;
    }

    void EngineSound::play(const std::string &name, int volume)
    {
        if (sl::Sample *s = effect(name))
            sl::play_sample(s, volume, 128, 1000, 0);
    }

    void EngineSound::set_engine_file(const std::string &path)
    {
        if (path == engine_file)
            return;
        engine_file = path;
        if (!started)
            return;
        if (rocket_voice)
            sl::stop_voice(rocket_voice);
        pick_engine_sample();
        rocket_voice = rocket ? sl::play_sample(rocket, 0, 128, 1000, -1) : 0;
    }

    void EngineSound::pick_engine_sample()
    {
        if (custom)
        {
            sl::destroy_sample(custom);
            custom = nullptr;
        }
        if (!engine_file.empty())
            custom = sl::load_sample(engine_file);
        if (custom)
        {
            rocket = custom;
            return;
        }
        rocket = effect("main_engines");
        if (!rocket)
        {
            if (!generic_rocket)
                generic_rocket = fx::make_rocket_sound();
            rocket = generic_rocket;
        }
    }

    void EngineSound::start()
    {
        started = true;
        pick_engine_sample();
        hiss = effect("rcs_sustain");
        if (!hiss)
            hiss = fx::make_hiss_sound();
        wind = make_breeze_sound();
        rocket_voice = rocket ? sl::play_sample(rocket, 0, 128, 1000, -1) : 0;
        hiss_voice = hiss ? sl::play_sample(hiss, 0, 128, 1000, -1) : 0;
        wind_voice = wind ? sl::play_sample(wind, 0, 128, 1000, -1) : 0;
    }

    void EngineSound::set(double rocket_level, double rcs_level, double wind_level)
    {
        // The voices are only created once the first frame is on screen, so nothing sounds during start-up.
        if (!started)
            start();
        if (wind_voice)
            sl::set_voice_volume(wind_voice, static_cast<int>(std::clamp(wind_level, 0.0, 1.0) * 40.0));
        if (rocket_voice)
            sl::set_voice_volume(rocket_voice, static_cast<int>(std::clamp(rocket_level, 0.0, 1.0) * 51.0));
        if (hiss_voice)
            sl::set_voice_volume(hiss_voice, static_cast<int>(std::clamp(rcs_level, 0.0, 1.0) * 28.0));
    }

    void EngineSound::cabin_ambience(bool inside, bool enabled)
    {
        if (!inside || !enabled)
        {
            ambience_due = -1.0;
            return;
        }
        static std::mt19937 rng{std::random_device{}()};
        const double now = sl::time_ms() / 1000.0;
        const auto schedule = [&] { ambience_due = now + std::uniform_real_distribution<double>(120.0, 360.0)(rng); };
        if (ambience_due < 0.0)
            schedule();
        else if (now >= ambience_due)
        {
            play("cabin/amb" + std::to_string(std::uniform_int_distribution<int>(0, 11)(rng)), 140);
            schedule();
        }
    }

    void EngineSound::stop()
    {
        sl::stop_all_samples();
        for (const auto &e : effects)
            if (e.second)
                sl::destroy_sample(e.second);
        if (wind)
            sl::destroy_sample(wind);
        if (rocket == custom)
            rocket = nullptr;
        if (custom)
            sl::destroy_sample(custom);
        custom = nullptr;
        if (generic_rocket == rocket)
            generic_rocket = nullptr;
        for (sl::Sample **sample : {&rocket, &hiss, &generic_rocket})
            if (*sample && !std::any_of(effects.begin(), effects.end(), [&](const auto &e) { return e.second == *sample; }))
                sl::destroy_sample(*sample);
        effects.clear();
        rocket = hiss = wind = generic_rocket = nullptr;
        sl::audio_fx_shutdown();
    }
} // namespace kosmos
