#pragma once

#include "universe.h"
#include "weather.h"
#include "weather_live.h"

#include "proptree.h"

#include <map>
#include <memory>
#include <string>

namespace kosmos
{
    /**
     * Weather for every body with an atmosphere, from procedural METARs, or for Earth from NOAA's real reports when
     * /sim/settings/weather-source is "live" and the simulation clock is near the real one. Each frame the flown ship's
     * conditions are published under /environment and the planet's wind is set for the flight model.
     *
     * Settings: /sim/settings/weather (bool), /sim/settings/weather-source ("procedural" or "live").
     */
    class Weather
    {
    public:
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

        /** True while Earth is showing real observations. */
        bool live() const { return live_applied_; }
        std::string live_status() const;

    private:
        WeatherField *ensure(const Universe &universe, int body);

        std::string root_, live_cache_;
        std::map<int, std::unique_ptr<WeatherField>> fields_;
        LiveWeather live_;
        bool live_configured_ = false, live_applied_ = false;
        Conditions current_;
        double wet_ = 0.0, clock_ = 0.0;
        int last_body_ = -1, wind_body_ = -1;
        double unix_now_ = 0.0;
    };
}
