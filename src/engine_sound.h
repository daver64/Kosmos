#pragma once

#include "sl.h"

#include <cstdint>
#include <map>
#include <string>

namespace kosmos
{
    /**
     * The ship's looping engine, thruster and wind voices plus the one-shot effects from data/sounds (XRSound, MIT).
     * A missing effect file just means silence for that event; the engine voices fall back to synthesised sound.
     */
    struct EngineSound
    {
        sl::Sample *rocket = nullptr, *hiss = nullptr, *wind = nullptr;
        std::uint64_t rocket_voice = 0, hiss_voice = 0, wind_voice = 0;
        bool started = false;
        std::string data_root;
        std::map<std::string, sl::Sample *> effects;

        sl::Sample *effect(const std::string &name);
        void play(const std::string &name, int volume = 200);
        /** Set the three voice levels (0 to 1); the voices are created on the first call. */
        void set(double rocket_level, double rcs_level, double wind_level);
        void stop();

    private:
        void start();
    };
} // namespace kosmos
