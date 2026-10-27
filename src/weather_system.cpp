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

    const std::vector<WeatherPreset> &Weather::presets()
    {
        static const std::vector<WeatherPreset> list = {
            {"clear", "Clear skies", "KOSM 011200Z 18004KT CAVOK 20/08 Q1018"},
            {"scattered", "Scattered cloud", "KOSM 011200Z 27008KT 9999 FEW040 SCT250 17/08 Q1015"},
            {"overcast", "Overcast", "KOSM 011200Z 24010KT 8000 OVC012 11/09 Q1008"},
            {"rain", "Rain", "KOSM 011200Z 21014G24KT 4000 RA BKN012 OVC020 10/08 Q1002"},
            {"fog", "Fog", "KOSM 011200Z 00000KT 0300 FG VV002 06/06 Q1021"},
            {"storm", "Thunderstorm", "KOSM 011200Z 24022G42KT 1500 +TSRA BKN008CB OVC015 12/11 Q0990"},
        };
        return list;
    }

    void Weather::init(const std::string &root, const std::string &user_dir, sl::PropertyTree &props)
    {
        root_ = root;
        if (!props.exists("/sim/settings/weather"))
            props.set_bool("/sim/settings/weather", true);
        props.set_bool("/environment/metar-valid", true);
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
                    fb->wind_field = nullptr;
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

        std::string forced = props.get_string("/sim/weather-force", "");
        if (forced.empty())
        {
            const std::string preset = props.get_string("/sim/settings/weather-preset", "auto");
            if (preset == "custom")
                forced = props.get_string("/sim/settings/weather-metar", "");
            else
                for (const WeatherPreset &p : presets())
                    if (preset == p.id)
                        forced = p.metar;
        }
        if (forced != forced_text_)
        {
            forced_text_ = forced;
            Metar m;
            forced_valid_ = !forced.empty() && decode_metar(forced, m);
            props.set_bool("/environment/metar-valid", forced.empty() || forced_valid_);
            if (forced_valid_)
                forced_ = conditions_from(m);
        }
        const Conditions c = forced_valid_ ? forced_ : field->sample(latitude, longitude);
        const double t = unix_now_;
        clock_ += dt;
        // Wind at a place and height: grows with height above the ground, doubling by three kilometres, with gusts that come and go.
        const auto wind_at = [field, t, forced_valid = forced_valid_, forced = forced_](double lat, double lon, double alt, double &east, double &north) {
            const Conditions w = forced_valid ? forced : field->sample(lat, lon);
            const double lift = 1.0 + std::clamp(alt, 0.0, 3000.0) / 3000.0;
            const double gust = std::max(0.0, std::sin(t * 0.61) + 0.7 * std::sin(t * 1.73 + 1.3) + 0.4 * std::sin(t * 4.1 + 0.4)) / 2.1;
            east = w.wind_east;
            north = w.wind_north;
            const double speed = std::hypot(east, north);
            if (speed > 0.01)
            {
                east += east / speed * w.gust * gust;
                north += north / speed * w.gust * gust;
            }
            east *= lift;
            north *= lift;
        };
        double east = 0.0, north = 0.0;
        wind_at(latitude, longitude, altitude, east, north);
        current_ = c;
        current_.wind_east = east;
        current_.wind_north = north;

        // Every ship on the planet feels the wind at its own position. Body frame has +Y up the spin axis; east is (-sin lon, 0, -cos lon).
        if (wind_body_ != body)
            clear_wind();
        if (sl::FlightBody *fb = universe.world.body(body))
        {
            const auto orientation = universe.orientation(body);
            const auto inverse = glm::inverse(orientation);
            const double radius = fb->radius;
            fb->wind_field = [wind_at, orientation, inverse, radius](const sl::dvec3 &offset) {
                const double r = glm::length(offset);
                if (r < 1.0)
                    return sl::dvec3(0.0);
                const sl::dvec3 b = inverse * (offset / r);
                const double la = std::asin(std::clamp(b.y, -1.0, 1.0)), lo = std::atan2(-b.z, b.x);
                double e = 0.0, n = 0.0;
                wind_at(la / deg, lo / deg, r - radius, e, n);
                const sl::dvec3 east_axis(-std::sin(lo), 0.0, -std::cos(lo));
                const sl::dvec3 north_axis(-std::sin(la) * std::cos(lo), std::cos(la), std::sin(la) * std::sin(lo));
                return orientation * (east_axis * e + north_axis * n);
            };
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
        props.set_double("/environment/thunder", current_.thunder);
        props.set_double("/environment/rain", c.rain);
        props.set_double("/environment/wetness", wet_);
        props.set_double("/environment/cloud-cover", c.cloud_cover);
        props.set_double("/environment/cloud-base", c.cloud_base);
        props.set_double("/environment/cloud-top", c.cloud_top);
        props.set_double("/environment/visibility", c.visibility);
        props.set_double("/environment/temperature", c.temperature);
        props.set_double("/environment/pressure", c.pressure);
        double distance = 0.0;
        if (forced_valid_)
        {
            Metar m;
            decode_metar(forced_text_, m);
            props.set_string("/environment/station", m.station);
            props.set_string("/environment/metar", forced_text_);
            props.set_double("/environment/station-distance", 0.0);
            props.set_bool("/environment/live", false);
        }
        else if (const WeatherStation *s = field->nearest(latitude, longitude, &distance))
        {
            props.set_string("/environment/station", s->id);
            props.set_string("/environment/metar", s->line);
            props.set_double("/environment/station-distance", distance * 1000.0);
            props.set_bool("/environment/live", s->live);
        }
    }
}
