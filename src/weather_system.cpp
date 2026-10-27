#include "weather_system.h"

#include "json.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>

namespace kosmos
{
    namespace
    {
        constexpr double pi = 3.14159265358979323846;
        constexpr double deg = pi / 180.0;
        constexpr double unix_of_julian_zero = 2440587.5;
        // Real reports are only used when the simulated date is within this many seconds of the real one.
        constexpr double live_window = 3.0 * 3600.0;

        double unix_from_julian(double jd) { return (jd - unix_of_julian_zero) * 86400.0; }

        bool rocky_with_air(const Universe &universe, int body)
        {
            return body >= 0 && body < static_cast<int>(universe.bodies.size()) && universe.bodies[static_cast<std::size_t>(body)].atmosphere_density > 0.0;
        }
    }

    void Weather::init(const std::string &root, const std::string &user_dir, sl::PropertyTree &props)
    {
        root_ = root;
        if (!props.exists("/sim/settings/weather"))
            props.set_bool("/sim/settings/weather", true);
        if (!props.exists("/sim/settings/weather-source"))
            props.set_string("/sim/settings/weather-source", "procedural");
        if (!user_dir.empty())
            live_cache_ = user_dir + "/weather/earth.txt";
    }

    WeatherField *Weather::ensure(const Universe &universe, int body)
    {
        if (!rocky_with_air(universe, body))
            return nullptr;
        const auto found = fields_.find(body);
        if (found != fields_.end())
            return found->second.get();
        const Body &b = universe.bodies[static_cast<std::size_t>(body)];
        const BodyWeather params = body_weather_for(b.name, b.atmosphere_density, 0.0, root_);
        const int seed = static_cast<int>(std::hash<std::string>{}(b.name) % 100000);
        std::unique_ptr<WeatherField> field;
        if (b.name == "Earth")
        {
            std::vector<WeatherStation> stations;
            std::ifstream file(root_ + "/data/weather/earth_stations.json");
            if (file)
            {
                try
                {
                    nlohmann::json j;
                    file >> j;
                    for (const nlohmann::json &entry : j.value("stations", nlohmann::json::array()))
                    {
                        WeatherStation s;
                        s.id = entry.value("id", std::string());
                        s.latitude = entry.value("latitude", 0.0);
                        s.longitude = entry.value("longitude", 0.0);
                        s.elevation = entry.value("elevation", 0.0);
                        if (!s.id.empty())
                            stations.push_back(s);
                    }
                }
                catch (const std::exception &)
                {
                    stations.clear();
                }
            }
            if (!stations.empty())
                field = std::make_unique<WeatherField>(b.name, params, seed, std::move(stations));
        }
        if (!field)
            field = std::make_unique<WeatherField>(b.name, params, seed);
        return (fields_[body] = std::move(field)).get();
    }

    const WeatherField *Weather::field(const Universe &universe, int body)
    {
        return ensure(universe, body);
    }

    std::string Weather::live_status() const
    {
        return live_.status(real_unix_time());
    }

    void Weather::step(Universe &universe, sl::PropertyTree &props, int body, double latitude, double longitude, double altitude, double dt)
    {
        // The wind a planet had last frame is dropped when the flown ship moves to another body or weather is off.
        const auto clear_wind = [&] {
            if (wind_body_ >= 0)
                if (sl::FlightBody *fb = universe.world.body(wind_body_))
                    fb->wind = sl::dvec3(0.0);
            wind_body_ = -1;
        };
        WeatherField *field = props.get_bool("/sim/settings/weather", true) ? ensure(universe, body) : nullptr;
        if (!field)
        {
            clear_wind();
            current_ = Conditions();
            current_.visibility = 1.0e5;
            wet_ = 0.0;
            props.set_double("/environment/rain", 0.0);
            props.set_double("/environment/cloud-cover", 0.0);
            props.set_double("/environment/wind-speed", 0.0);
            props.set_string("/environment/metar", "");
            return;
        }
        unix_now_ = unix_from_julian(universe.julian_date());
        field->update(unix_now_);

        // Real observations for Earth when asked for and the simulated date is close to today's.
        if (universe.bodies[static_cast<std::size_t>(body)].name == "Earth")
        {
            const bool want = props.get_string("/sim/settings/weather-source", "procedural") == "live" && http_available();
            const double real = real_unix_time();
            const bool near_now = std::fabs(unix_now_ - real) < live_window;
            if (want && !live_configured_ && !live_cache_.empty())
            {
                std::vector<std::string> ids;
                for (const WeatherStation &s : field->stations())
                    ids.push_back(s.id);
                live_.configure(ids, live_cache_);
                live_configured_ = true;
            }
            if (want && near_now && live_configured_)
            {
                live_.poll(real);
                std::vector<std::string> lines;
                if (live_.take(lines))
                {
                    for (const std::string &line : lines)
                        field->set_live(line, real);
                    live_applied_ = true;
                }
            }
            else if (live_applied_)
            {
                field->clear_live();
                live_applied_ = false;
                field->update(unix_now_);
            }
        }

        const Conditions c = field->sample(latitude, longitude);
        // Wind grows with height above the ground, doubling by three kilometres, and gusts come and go.
        const double lift = 1.0 + std::clamp(altitude, 0.0, 3000.0) / 3000.0;
        clock_ += dt;
        const double t = unix_now_;
        const double gust = std::max(0.0, std::sin(t * 0.61) + 0.7 * std::sin(t * 1.73 + 1.3) + 0.4 * std::sin(t * 4.1 + 0.4)) / 2.1;
        double east = c.wind_east, north = c.wind_north;
        const double speed = std::hypot(east, north);
        if (speed > 0.01)
        {
            const double extra = c.gust * gust;
            east += east / speed * extra;
            north += north / speed * extra;
        }
        east *= lift;
        north *= lift;
        current_ = c;
        current_.wind_east = east;
        current_.wind_north = north;

        // To the world frame: body frame has +Y up the spin axis, east is -sin(lon), 0, -cos(lon).
        const double la = latitude * deg, lo = longitude * deg;
        const sl::dvec3 e(-std::sin(lo), 0.0, -std::cos(lo));
        const sl::dvec3 n(-std::sin(la) * std::cos(lo), std::cos(la), std::sin(la) * std::sin(lo));
        const sl::dvec3 world_wind = universe.orientation(body) * (e * east + n * north);
        if (wind_body_ != body)
            clear_wind();
        if (sl::FlightBody *fb = universe.world.body(body))
        {
            fb->wind = world_wind;
            wind_body_ = body;
        }

        // Ground wetness: quick to soak, slow to dry (about half an hour of simulated time).
        const double target = std::clamp(c.rain * 1.6, 0.0, 1.0);
        if (body != last_body_)
            wet_ = target;
        else if (target > wet_)
            wet_ = std::min(target, wet_ + dt / 60.0);
        else
            wet_ = std::max(target, wet_ - dt / 1800.0);
        last_body_ = body;

        props.set_double("/environment/wind-east", east);
        props.set_double("/environment/wind-north", north);
        const double now_speed = std::hypot(east, north);
        props.set_double("/environment/wind-speed", now_speed);
        props.set_double("/environment/wind-from", now_speed > 0.01 ? std::fmod(std::atan2(-east, -north) / deg + 360.0, 360.0) : 0.0);
        props.set_double("/environment/rain", c.rain);
        props.set_double("/environment/wetness", wet_);
        props.set_double("/environment/cloud-cover", c.cloud_cover);
        props.set_double("/environment/cloud-base", c.cloud_base);
        props.set_double("/environment/cloud-top", c.cloud_top);
        props.set_double("/environment/visibility", c.visibility);
        props.set_double("/environment/temperature", c.temperature);
        props.set_double("/environment/pressure", c.pressure);
        double distance = 0.0;
        if (const WeatherStation *s = field->nearest(latitude, longitude, &distance))
        {
            props.set_string("/environment/station", s->id);
            props.set_string("/environment/metar", s->line);
            props.set_double("/environment/station-distance", distance * 1000.0);
            props.set_bool("/environment/live", s->live);
        }
    }
}
