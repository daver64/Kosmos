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
        std::string engine_file;
        sl::Sample *custom = nullptr;
        std::string data_root;
        std::map<std::string, sl::Sample *> effects;

        sl::Sample *effect(const std::string &name);
        void play(const std::string &name, int volume = 200);
        /** Set the three voice levels (0 to 1); the voices are created on the first call. */
        void set(double rocket_level, double rcs_level, double wind_level);
        /** Inside a pressure vessel, play one of the cabin ambience clips at random every 2 to 6 minutes. Call every frame. */
        void cabin_ambience(bool inside, bool enabled);
        void stop();
        /** Use a ship's own engine loop (a full path, or empty for the generic one); a file that will not load leaves the generic sound. */
        void set_engine_file(const std::string &path);

    private:
        void start();
        void pick_engine_sample();
        sl::Sample *generic_rocket = nullptr;
        double ambience_due = -1.0; // time_ms()/1000 of the next clip, or negative when outside
    };
} // namespace kosmos
