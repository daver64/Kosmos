#pragma once

#include "universe.h"
#include "weather.h"
#include "weather_live.h"

#include "proptree.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace kosmos
{
    /**
     * Weather for every body with an atmosphere, from procedural METARs, or for Earth from NOAA's real reports when
     * /sim/settings/weather-source is "live" and the simulation clock is near the real one. Each frame the flown ship's
     * conditions are published under /environment and the planet's wind is set for the flight model.
     *
     * Settings: /sim/settings/weather (bool), /sim/settings/weather-source ("procedural" or "live"), /sim/settings/weather-preset
     * ("auto", a preset id or "custom") and /sim/settings/weather-metar (the line "custom" uses). A scenario's /sim/weather-force wins over all of them.
     */
    /** A ready-made set of conditions the player can pick instead of the simulated weather. */
    struct WeatherPreset
    {
        const char *id, *label, *metar;
    };

    class Weather
    {
    public:
        /** Clear, scattered cloud, overcast, rain, fog and storm; /sim/settings/weather-preset also takes "auto" (simulated weather) and "custom" (the METAR in /sim/settings/weather-metar). */
        static const std::vector<WeatherPreset> &presets();

        void init(const std::string &root, const std::string &user_dir, sl::PropertyTree &props);

        /** Call once a frame with the flown ship's body, position over it (degrees), altitude (m) and the simulated seconds elapsed. */
        void step(Universe &universe, sl::PropertyTree &props, int body, double latitude, double longitude, double altitude, double dt);

        /** The conditions at the flown ship from the last step (all zero when weather is off or the body has no atmosphere). */
        const Conditions &current() const { return current_; }
        /** 0 dry to 1 soaked, for the ground under the flown ship. */
        double wetness() const { return wet_; }
        /** Multiplier for tyre grip from the wetness. */
        double grip() const { return 1.0 - 0.55 * wet_; }
        /** The field of a body, created on demand; null when it has no atmosphere. */
        const WeatherField *field(const Universe &universe, int body);

        /** The conditions at any place and height (degrees, metres above the ground), as the flown ship would see them; false when the body has no atmosphere or weather is off. */
        bool at(const Universe &universe, int body, double latitude, double longitude, double altitude, Conditions &out);

        /** True while Earth is showing real observations. */
        bool live() const { return live_applied_; }
        std::string live_status() const;
        /** Whether the body's atmosphere supports the weather system. */
        static bool supports_body(const Universe &universe, int body);

    private:
        WeatherField *ensure(const Universe &universe, int body);

        void poll_live(WeatherField *field, sl::PropertyTree &props);
        void update_forced(sl::PropertyTree &props);
        void install_wind_field(Universe &universe, int body, const std::function<void(double, double, double, double &, double &)> &wind_at);
        void publish_environment(sl::PropertyTree &props, const Conditions &c, double east, double north);
        std::string root_, live_cache_;
        std::map<int, std::unique_ptr<WeatherField>> fields_;
        LiveWeather live_;
        bool live_configured_ = false, live_applied_ = false;
        Conditions current_;
        double wet_ = 0.0, clock_ = 0.0;
        int last_body_ = -1, wind_body_ = -1;
        double unix_now_ = 0.0;
        std::string forced_text_;
        bool forced_valid_ = false;
        Conditions forced_;
    };
}
