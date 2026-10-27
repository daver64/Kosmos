#include "weather.h"

#include "json.hpp"
#include "noise.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

namespace kosmos
{
    namespace
    {
        constexpr double pi = 3.14159265358979323846;
        constexpr double knot = 0.514444;
        constexpr double feet = 0.3048;
        constexpr double bucket_seconds = 600.0;

        long long days_from_civil(long long y, unsigned m, unsigned d)
        {
            y -= m <= 2;
            const long long era = (y >= 0 ? y : y - 399) / 400;
            const unsigned yoe = static_cast<unsigned>(y - era * 400);
            const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
            const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
            return era * 146097 + static_cast<long long>(doe) - 719468;
        }

        void civil_from_days(long long z, long long &y, unsigned &m, unsigned &d)
        {
            z += 719468;
            const long long era = (z >= 0 ? z : z - 146096) / 146097;
            const unsigned doe = static_cast<unsigned>(z - era * 146097);
            const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
            y = static_cast<long long>(yoe) + era * 400;
            const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
            const unsigned mp = (5 * doy + 2) / 153;
            d = doy - (153 * mp + 2) / 5 + 1;
            m = mp < 10 ? mp + 3 : mp - 9;
            y += m <= 2;
        }

        std::string temperature_group(int t)
        {
            char buffer[16];
            std::snprintf(buffer, sizeof buffer, "%s%02d", t < 0 ? "M" : "", std::abs(t));
            return buffer;
        }

        bool is_weather_group(std::string token)
        {
            static const std::set<std::string> codes = {"MI", "BC", "PR", "DR", "BL", "SH", "TS", "FZ", "DZ", "RA", "SN", "SG", "IC", "PL", "GR", "GS", "UP",
                                                       "BR", "FG", "FU", "VA", "DU", "SA", "HZ", "PY", "PO", "SQ", "FC", "SS", "DS"};
            if (!token.empty() && (token[0] == '+' || token[0] == '-'))
                token.erase(0, 1);
            else if (token.compare(0, 2, "VC") == 0)
                token.erase(0, 2);
            if (token.empty() || token.size() % 2 != 0)
                return false;
            for (std::size_t i = 0; i < token.size(); i += 2)
                if (!codes.count(token.substr(i, 2)))
                    return false;
            return true;
        }

        double cover_fraction(const std::string &cover)
        {
            if (cover == "FEW")
                return 0.19;
            if (cover == "SCT")
                return 0.44;
            if (cover == "BKN")
                return 0.75;
            if (cover == "OVC" || cover == "VV")
                return 1.0;
            return 0.0;
        }
    }

    std::string encode_metar(const Metar &m)
    {
        std::ostringstream out;
        char buffer[64];
        out << (m.station.empty() ? "ZZZZ" : m.station);
        std::snprintf(buffer, sizeof buffer, " %02d%02d%02dZ ", m.day, m.hour, m.minute);
        out << buffer;
        if (m.wind_kt <= 0)
            out << "00000KT";
        else if (m.wind_dir < 0)
            out << "VRB" << (m.wind_kt < 10 ? "0" : "") << m.wind_kt << "KT";
        else
        {
            std::snprintf(buffer, sizeof buffer, "%03d%02d", m.wind_dir % 360, m.wind_kt);
            out << buffer;
            if (m.gust_kt > m.wind_kt)
                out << "G" << (m.gust_kt < 10 ? "0" : "") << m.gust_kt;
            out << "KT";
        }
        std::snprintf(buffer, sizeof buffer, " %04d", std::clamp(m.visibility_m, 0, 9999));
        out << buffer;
        for (const std::string &w : m.weather)
            out << ' ' << w;
        if (m.clouds.empty())
            out << " NSC";
        for (const MetarCloud &c : m.clouds)
        {
            std::snprintf(buffer, sizeof buffer, " %s%03d", c.cover.c_str(), std::clamp(c.base_ft / 100, 0, 999));
            out << buffer;
        }
        out << ' ' << temperature_group(m.temperature_c) << '/' << temperature_group(m.dewpoint_c);
        std::snprintf(buffer, sizeof buffer, " Q%04d", m.qnh_hpa);
        out << buffer;
        return out.str();
    }

    bool decode_metar(const std::string &line, Metar &out)
    {
        static const std::regex station_re("^[A-Z][A-Z0-9]{3}$"), time_re("^(\\d\\d)(\\d\\d)(\\d\\d)Z$"), wind_re("^(\\d{3}|VRB)(\\d{2,3})(?:G(\\d{2,3}))?(KT|MPS)$"),
            cloud_re("^(FEW|SCT|BKN|OVC)(\\d{3})(?:CB|TCU)?$"), vv_re("^VV(\\d{3})$"), temp_re("^(M?\\d{2,3})/(M?\\d{2,3})?$"), q_re("^Q(\\d{4,6})$"),
            a_re("^A(\\d{4})$"), vis_sm_re("^M?(\\d+)(?:/(\\d+))?SM$");
        Metar m;
        std::istringstream in(line);
        std::vector<std::string> tokens;
        for (std::string t; in >> t;)
            tokens.push_back(t);
        std::size_t i = 0;
        if (i < tokens.size() && (tokens[i] == "METAR" || tokens[i] == "SPECI"))
            ++i;
        if (i >= tokens.size() || !std::regex_match(tokens[i], station_re))
            return false;
        m.station = tokens[i++];
        std::smatch match;
        if (i >= tokens.size() || !std::regex_match(tokens[i], match, time_re))
            return false;
        m.day = std::stoi(match[1]);
        m.hour = std::stoi(match[2]);
        m.minute = std::stoi(match[3]);
        ++i;
        bool have_vis = false;
        for (; i < tokens.size(); ++i)
        {
            const std::string &t = tokens[i];
            if (t == "RMK" || t == "TEMPO" || t == "BECMG" || t == "NOSIG")
                break;
            if (t == "AUTO" || t == "COR" || t == "NIL" || t == "$")
                continue;
            if (std::regex_match(t, match, wind_re))
            {
                m.wind_dir = match[1] == "VRB" ? -1 : std::stoi(match[1]);
                double speed = std::stod(match[2]), gust = match[3].matched ? std::stod(match[3]) : 0.0;
                if (match[4] == "MPS")
                {
                    speed *= 1.94384;
                    gust *= 1.94384;
                }
                m.wind_kt = static_cast<int>(std::lround(speed));
                m.gust_kt = static_cast<int>(std::lround(gust));
                if (m.wind_kt == 0)
                    m.wind_dir = -1;
            }
            else if (t == "CAVOK")
            {
                m.visibility_m = 9999;
                have_vis = true;
                m.clouds.clear();
            }
            else if (!have_vis && t.size() == 4 && std::all_of(t.begin(), t.end(), [](unsigned char c) { return std::isdigit(c); }))
            {
                m.visibility_m = std::stoi(t);
                have_vis = true;
            }
            else if (std::regex_match(t, match, vis_sm_re))
            {
                // "1 1/2SM" arrives as two tokens, the whole miles first.
                double miles = match[2].matched ? std::stod(match[1]) / std::max(1.0, std::stod(match[2])) : std::stod(match[1]);
                if (i > 0 && match[2].matched && std::all_of(tokens[i - 1].begin(), tokens[i - 1].end(), [](unsigned char c) { return std::isdigit(c); }) && tokens[i - 1].size() < 3)
                    miles += std::stod(tokens[i - 1]);
                m.visibility_m = static_cast<int>(std::min(9999.0, miles * 1609.34));
                have_vis = true;
            }
            else if (std::regex_match(t, match, cloud_re))
                m.clouds.push_back({match[1], std::stoi(match[2]) * 100});
            else if (std::regex_match(t, match, vv_re))
                m.clouds.push_back({"OVC", std::stoi(match[1]) * 100});
            else if (t == "SKC" || t == "CLR" || t == "NSC" || t == "NCD")
                m.clouds.clear();
            else if (std::regex_match(t, match, temp_re))
            {
                const auto value = [](const std::string &s) { return (s[0] == 'M' ? -1 : 1) * std::stoi(s[0] == 'M' ? s.substr(1) : s); };
                m.temperature_c = value(match[1]);
                m.dewpoint_c = match[2].matched ? value(match[2]) : m.temperature_c - 10;
            }
            else if (std::regex_match(t, match, q_re))
                m.qnh_hpa = std::stoi(match[1]);
            else if (std::regex_match(t, match, a_re))
                m.qnh_hpa = static_cast<int>(std::lround(std::stod(match[1]) * 0.3386389));
            else if (is_weather_group(t))
                m.weather.push_back(t);
        }
        m.valid = true;
        out = m;
        return true;
    }

    Conditions conditions_from(const Metar &m)
    {
        Conditions c;
        const double speed = m.wind_kt * knot;
        const double from = (m.wind_dir < 0 ? 0 : m.wind_dir) * pi / 180.0;
        c.wind_east = -speed * std::sin(from);
        c.wind_north = -speed * std::cos(from);
        c.gust = m.gust_kt > m.wind_kt ? (m.gust_kt - m.wind_kt) * knot : 0.0;
        double visibility = m.visibility_m;
        for (const std::string &group : m.weather)
        {
            const bool light = group[0] == '-', heavy = group[0] == '+', nearby = group.compare(0, 2, "VC") == 0;
            const double strength = light ? 0.3 : (heavy ? 1.0 : 0.6);
            if (nearby)
                continue;
            if (group.find("RA") != std::string::npos || group.find("SN") != std::string::npos || group.find("SG") != std::string::npos ||
                group.find("PL") != std::string::npos || group.find("UP") != std::string::npos)
                c.rain = std::max(c.rain, strength);
            if (group.find("DZ") != std::string::npos)
                c.rain = std::max(c.rain, 0.5 * strength);
            if (group.find("GR") != std::string::npos || group.find("GS") != std::string::npos)
                c.rain = std::max(c.rain, 1.0);
            if (group.find("TS") != std::string::npos)
                c.rain = std::max(c.rain, 0.8);
            if (group.find("FG") != std::string::npos)
                visibility = std::min(visibility, 800.0);
            if (group.find("BR") != std::string::npos || group.find("HZ") != std::string::npos || group.find("FU") != std::string::npos)
                visibility = std::min(visibility, 4500.0);
            if (group.find("DU") != std::string::npos || group.find("SA") != std::string::npos || group.find("DS") != std::string::npos || group.find("SS") != std::string::npos)
                visibility = std::min(visibility, heavy ? 600.0 : 2500.0);
        }
        c.visibility = visibility >= 9999.0 ? 30000.0 : visibility; // "9999" means ten kilometres or more
        double clear = 1.0, lowest = 0.0;
        bool have_base = false;
        for (const MetarCloud &layer : m.clouds)
        {
            const double f = cover_fraction(layer.cover);
            clear *= 1.0 - f;
            const double base = layer.base_ft * feet;
            if (f >= 0.4 && (!have_base || base < lowest))
            {
                lowest = base;
                have_base = true;
            }
        }
        c.cloud_cover = 1.0 - clear;
        if (!have_base && !m.clouds.empty())
            lowest = m.clouds.front().base_ft * feet;
        c.cloud_base = have_base || !m.clouds.empty() ? std::max(lowest, 30.0) : 3000.0;
        c.cloud_top = c.cloud_base + 600.0 + 2000.0 * c.cloud_cover;
        c.temperature = m.temperature_c;
        c.pressure = m.qnh_hpa;
        return c;
    }

    Conditions blend(const Conditions &a, const Conditions &b, double t)
    {
        const auto mix = [t](double x, double y) { return x + (y - x) * t; };
        Conditions c;
        c.wind_east = mix(a.wind_east, b.wind_east);
        c.wind_north = mix(a.wind_north, b.wind_north);
        c.gust = mix(a.gust, b.gust);
        c.rain = mix(a.rain, b.rain);
        c.cloud_cover = mix(a.cloud_cover, b.cloud_cover);
        c.cloud_base = mix(a.cloud_base, b.cloud_base);
        c.cloud_top = mix(a.cloud_top, b.cloud_top);
        c.visibility = mix(a.visibility, b.visibility);
        c.temperature = mix(a.temperature, b.temperature);
        c.pressure = mix(a.pressure, b.pressure);
        return c;
    }

    void unix_to_dhm(double unix_seconds, int &day, int &hour, int &minute)
    {
        const long long days = static_cast<long long>(std::floor(unix_seconds / 86400.0));
        long long y;
        unsigned m, d;
        civil_from_days(days, y, m, d);
        const double second_of_day = unix_seconds - static_cast<double>(days) * 86400.0;
        day = static_cast<int>(d);
        hour = static_cast<int>(second_of_day / 3600.0) % 24;
        minute = static_cast<int>(std::fmod(second_of_day, 3600.0) / 60.0);
    }

    double metar_time(int day, int hour, int minute, double reference)
    {
        long long y;
        unsigned m, d;
        civil_from_days(static_cast<long long>(std::floor(reference / 86400.0)), y, m, d);
        const double seconds = hour * 3600.0 + minute * 60.0;
        double best = 0.0, best_gap = 1e30;
        for (int shift = -1; shift <= 1; ++shift)
        {
            long long yy = y;
            int mm = static_cast<int>(m) + shift;
            if (mm < 1)
            {
                mm += 12;
                --yy;
            }
            else if (mm > 12)
            {
                mm -= 12;
                ++yy;
            }
            const double t = static_cast<double>(days_from_civil(yy, static_cast<unsigned>(mm), 1) + day - 1) * 86400.0 + seconds;
            if (std::fabs(t - reference) < best_gap)
            {
                best_gap = std::fabs(t - reference);
                best = t;
            }
        }
        return best;
    }

    // ---- the field ----------------------------------------------------------------------------------------------

    std::string WeatherField::station_id(const std::string &body, int index)
    {
        std::string letters;
        for (const char c : body)
            if (std::isalpha(static_cast<unsigned char>(c)))
                letters += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (letters.empty())
            letters = "XX";
        std::string id;
        id += letters.front();
        id += letters.size() > 1 ? letters.back() : 'X';
        id += static_cast<char>('A' + (index / 26) % 26);
        id += static_cast<char>('A' + index % 26);
        return id;
    }

    WeatherField::WeatherField(const std::string &body, const BodyWeather &params, int seed, int stations) : body_(body), params_(params), seed_(seed)
    {
        const double golden = pi * (3.0 - std::sqrt(5.0));
        for (int i = 0; i < stations; ++i)
        {
            WeatherStation s;
            s.id = station_id(body, i);
            s.latitude = std::asin(1.0 - 2.0 * (i + 0.5) / stations) * 180.0 / pi;
            s.longitude = std::remainder(golden * i, 2.0 * pi) * 180.0 / pi;
            stations_.push_back(s);
        }
        for (const WeatherStation &s : stations_)
        {
            const double la = s.latitude * pi / 180.0, lo = s.longitude * pi / 180.0;
            directions_.push_back({std::cos(la) * std::cos(lo), std::cos(la) * std::sin(lo), std::sin(la)});
        }
    }

    WeatherField::WeatherField(const std::string &body, const BodyWeather &params, int seed, std::vector<WeatherStation> stations)
        : body_(body), params_(params), seed_(seed), stations_(std::move(stations))
    {
        for (const WeatherStation &s : stations_)
        {
            const double la = s.latitude * pi / 180.0, lo = s.longitude * pi / 180.0;
            directions_.push_back({std::cos(la) * std::cos(lo), std::cos(la) * std::sin(lo), std::sin(la)});
        }
    }

    void WeatherField::generate(WeatherStation &s, double unix_seconds, long bucket) const
    {
        sl::Generator g_pressure(seed_ * 7 + 1), g_cloud(seed_ * 7 + 2), g_rain(seed_ * 7 + 3), g_wind(seed_ * 7 + 4), g_dir(seed_ * 7 + 5), g_gust(seed_ * 7 + 6), g_layer(seed_ * 7 + 7);
        for (sl::Generator *g : {&g_pressure, &g_cloud, &g_rain, &g_wind, &g_dir, &g_gust, &g_layer})
            g->set_frequency(1.0f);
        const double la = s.latitude * pi / 180.0, lo = s.longitude * pi / 180.0;
        // Weather systems drift slowly about the globe: a day moves the pattern a fraction of a turn.
        const double drift = static_cast<double>(bucket) * bucket_seconds / (86400.0 * 4.0);
        const double x = std::cos(la) * std::cos(lo) + drift, y = std::cos(la) * std::sin(lo), z = std::sin(la);
        const auto at = [&](const sl::Generator &g, double scale) { return static_cast<double>(g.get(static_cast<float>(x * scale), static_cast<float>(y * scale), static_cast<float>(z * scale))); };
        const double n_pressure = at(g_pressure, 1.3), n_cloud = at(g_cloud, 2.2), n_rain = at(g_rain, 3.1), n_wind = at(g_wind, 2.6), n_dir = at(g_dir, 1.7),
                     n_gust = at(g_gust, 4.0), n_layer = at(g_layer, 3.5);

        Metar m;
        m.station = s.id;
        (void)unix_seconds;
        unix_to_dhm(static_cast<double>(bucket) * bucket_seconds, m.day, m.hour, m.minute);
        const double speed_mps = std::max(0.0, params_.mean_wind + params_.wind_variation * n_wind);
        m.wind_kt = static_cast<int>(std::lround(speed_mps / knot));
        // Prevailing winds: easterlies in the tropics and at the poles, westerlies between.
        const double abs_lat = std::fabs(s.latitude);
        const double prevailing = abs_lat < 30.0 ? 70.0 : (abs_lat < 60.0 ? 250.0 : 100.0);
        m.wind_dir = (static_cast<int>(std::lround(prevailing + 80.0 * n_dir)) % 360 + 360) % 360;
        if (m.wind_kt <= 2)
            m.wind_dir = -1;
        if (m.wind_kt >= 12 && n_gust > 0.0)
            m.gust_kt = m.wind_kt + static_cast<int>(std::lround(m.wind_kt * 0.25 + 6.0 * n_gust));

        const double cloud = std::clamp(params_.cloudiness + 0.55 * n_cloud, 0.0, 1.0);
        const auto cover_for = [](double c) { return c < 0.15 ? "" : (c < 0.3 ? "FEW" : (c < 0.5 ? "SCT" : (c < 0.75 ? "BKN" : "OVC"))); };
        const double base_m = params_.cloud_base * (0.6 + 0.4 * (n_layer + 1.0));
        if (*cover_for(cloud))
        {
            m.clouds.push_back({cover_for(cloud), static_cast<int>(base_m / feet / 100.0) * 100});
            if (cloud > 0.5)
                m.clouds.push_back({cover_for(cloud * 0.8), static_cast<int>((base_m + params_.cloud_depth) / feet / 100.0) * 100});
        }

        const double potential = cloud + 0.5 * params_.rain_chance + 0.3 * n_rain - 0.75;
        const bool wet = potential > 0.12 && params_.rain_chance > 0.0;
        if (wet && params_.rain_is_precipitation)
        {
            const char *prefix = potential < 0.3 ? "-" : (potential < 0.55 ? "" : "+");
            m.weather.push_back(std::string(prefix) + (potential > 0.7 && cloud > 0.85 ? "TSRA" : "RA"));
            m.visibility_m = potential < 0.3 ? 8000 : (potential < 0.55 ? 5000 : 2000);
            if (m.clouds.empty() || m.clouds.back().cover != "OVC")
                m.clouds.push_back({"OVC", static_cast<int>(base_m * 0.7 / feet / 100.0) * 100});
        }
        else if (wet && !params_.rain_is_precipitation)
        {
            // Dust storms on a dry world: strong wind and low visibility.
            m.weather.push_back(potential > 0.4 ? "+DU" : "DU");
            m.visibility_m = potential > 0.4 ? 600 : 2500;
        }

        std::sort(m.clouds.begin(), m.clouds.end(), [](const MetarCloud &x, const MetarCloud &y) { return x.base_ft < y.base_ft; });

        const double temperature = params_.mean_temperature + params_.temperature_range * (0.5 - std::fabs(std::sin(la))) - 3.0 * cloud + 2.0 * n_pressure;
        m.temperature_c = static_cast<int>(std::lround(temperature));
        const bool humid = params_.rain_chance > 0.0 && params_.rain_is_precipitation;
        const double spread = humid ? (wet ? 1.0 : 2.0 + 8.0 * (1.0 - cloud)) : 25.0;
        m.dewpoint_c = static_cast<int>(std::lround(temperature - spread));
        if (humid && !wet && spread < 3.0 && speed_mps < 2.0 && m.weather.empty())
        {
            m.weather.push_back(spread < 1.5 ? "FG" : "BR");
            m.visibility_m = spread < 1.5 ? 400 : 3500;
        }
        m.qnh_hpa = static_cast<int>(std::lround(params_.pressure + params_.pressure_variation * n_pressure));
        s.line = encode_metar(m);
        decode_metar(s.line, s.metar);
        s.now = conditions_from(s.metar);
    }

    void WeatherField::update(double unix_seconds)
    {
        const long bucket = static_cast<long>(std::floor(unix_seconds / bucket_seconds));
        fraction_ = std::clamp((unix_seconds - static_cast<double>(bucket) * bucket_seconds) / bucket_seconds, 0.0, 1.0);
        if (bucket == bucket_)
            return;
        const bool first = bucket_ < 0;
        bucket_ = bucket;
        for (WeatherStation &s : stations_)
        {
            if (s.live)
                continue;
            if (first)
            {
                // The previous observation, for blending toward the first one.
                generate(s, unix_seconds, bucket - 1);
                s.before = s.now;
            }
            else
                s.before = s.now;
            generate(s, unix_seconds, bucket);
        }
    }

    bool WeatherField::set_live(const std::string &line, double unix_seconds)
    {
        (void)unix_seconds;
        Metar m;
        if (!decode_metar(line, m))
            return false;
        for (WeatherStation &s : stations_)
            if (s.id == m.station)
            {
                s.line = line;
                s.metar = m;
                s.now = conditions_from(m);
                s.before = s.now;
                s.live = true;
                return true;
            }
        return false;
    }

    void WeatherField::clear_live()
    {
        for (WeatherStation &s : stations_)
            s.live = false;
        bucket_ = -1;
    }

    namespace
    {
        std::array<double, 3> direction_of(double latitude, double longitude)
        {
            const double la = latitude * pi / 180.0, lo = longitude * pi / 180.0;
            return {std::cos(la) * std::cos(lo), std::cos(la) * std::sin(lo), std::sin(la)};
        }
    }

    const WeatherStation *WeatherField::nearest(double latitude, double longitude, double *distance_km) const
    {
        if (stations_.empty())
            return nullptr;
        const auto p = direction_of(latitude, longitude);
        std::size_t best = 0;
        double best_dot = -2.0;
        for (std::size_t i = 0; i < directions_.size(); ++i)
        {
            const double d = p[0] * directions_[i][0] + p[1] * directions_[i][1] + p[2] * directions_[i][2];
            if (d > best_dot)
            {
                best_dot = d;
                best = i;
            }
        }
        if (distance_km)
            *distance_km = std::acos(std::clamp(best_dot, -1.0, 1.0)) * 6371.0;
        return &stations_[best];
    }

    Conditions WeatherField::sample(double latitude, double longitude) const
    {
        Conditions out;
        if (stations_.empty())
            return out;
        const auto p = direction_of(latitude, longitude);
        // The three nearest stations, weighted by the inverse square of their angular distance.
        std::array<std::size_t, 3> best{0, 0, 0};
        std::array<double, 3> angle{9.0, 9.0, 9.0};
        for (std::size_t i = 0; i < directions_.size(); ++i)
        {
            const double d = std::acos(std::clamp(p[0] * directions_[i][0] + p[1] * directions_[i][1] + p[2] * directions_[i][2], -1.0, 1.0));
            for (int k = 0; k < 3; ++k)
                if (d < angle[static_cast<std::size_t>(k)])
                {
                    for (int j = 2; j > k; --j)
                    {
                        angle[static_cast<std::size_t>(j)] = angle[static_cast<std::size_t>(j - 1)];
                        best[static_cast<std::size_t>(j)] = best[static_cast<std::size_t>(j - 1)];
                    }
                    angle[static_cast<std::size_t>(k)] = d;
                    best[static_cast<std::size_t>(k)] = i;
                    break;
                }
        }
        double total = 0.0;
        Conditions sum;
        sum.cloud_base = sum.cloud_top = sum.visibility = sum.temperature = sum.pressure = 0.0;
        for (int k = 0; k < 3; ++k)
        {
            const std::size_t j = best[static_cast<std::size_t>(k)];
            if (stations_.size() <= static_cast<std::size_t>(k) || angle[static_cast<std::size_t>(k)] > 8.0)
                continue;
            const double w = 1.0 / (angle[static_cast<std::size_t>(k)] * angle[static_cast<std::size_t>(k)] + 1.0e-4);
            const Conditions c = blend(stations_[j].before, stations_[j].now, fraction_);
            sum.wind_east += w * c.wind_east;
            sum.wind_north += w * c.wind_north;
            sum.gust += w * c.gust;
            sum.rain += w * c.rain;
            sum.cloud_cover += w * c.cloud_cover;
            sum.cloud_base += w * c.cloud_base;
            sum.cloud_top += w * c.cloud_top;
            sum.visibility += w * c.visibility;
            sum.temperature += w * c.temperature;
            sum.pressure += w * c.pressure;
            total += w;
        }
        if (total <= 0.0)
            return out;
        const double inv = 1.0 / total;
        out.wind_east = sum.wind_east * inv;
        out.wind_north = sum.wind_north * inv;
        out.gust = sum.gust * inv;
        out.rain = sum.rain * inv;
        out.cloud_cover = sum.cloud_cover * inv;
        out.cloud_base = sum.cloud_base * inv;
        out.cloud_top = sum.cloud_top * inv;
        out.visibility = sum.visibility * inv;
        out.temperature = sum.temperature * inv;
        out.pressure = sum.pressure * inv;
        return out;
    }

    BodyWeather body_weather_for(const std::string &body, double atmosphere_density, double surface_temperature_k, const std::string &data_root)
    {
        BodyWeather w;
        w.mean_temperature = surface_temperature_k > 0.0 ? surface_temperature_k - 273.15 : -20.0;
        w.pressure = std::max(1.0, atmosphere_density * 287.0 * (w.mean_temperature + 273.15) / 100.0);
        w.pressure_variation = w.pressure * 0.012;
        w.cloudiness = 0.3;
        w.rain_chance = 0.0;
        w.rain_is_precipitation = false;
        if (body == "Earth")
        {
            w.mean_wind = 6.0;
            w.wind_variation = 4.5;
            w.cloudiness = 0.5;
            w.rain_chance = 0.4;
            w.rain_is_precipitation = true;
            w.mean_temperature = 14.0;
            w.temperature_range = 40.0;
            w.pressure = 1013.0;
            w.pressure_variation = 12.0;
        }
        else if (body == "Mars")
        {
            w.mean_wind = 7.0;
            w.wind_variation = 6.0;
            w.cloudiness = 0.1;
            w.rain_chance = 0.25;
            w.mean_temperature = -55.0;
            w.temperature_range = 60.0;
            w.cloud_base = 4000.0;
        }
        else if (body == "Venus")
        {
            w.mean_wind = 1.5;
            w.wind_variation = 1.0;
            w.cloudiness = 0.0;
            w.mean_temperature = 462.0;
            w.temperature_range = 4.0;
            w.pressure = 92000.0;
            w.pressure_variation = 300.0;
        }
        else if (body == "Titan")
        {
            w.mean_wind = 1.0;
            w.wind_variation = 1.0;
            w.cloudiness = 0.55;
            w.rain_chance = 0.2;
            w.rain_is_precipitation = true;
            w.mean_temperature = -179.0;
            w.temperature_range = 3.0;
            w.pressure = 1467.0;
            w.pressure_variation = 8.0;
            w.cloud_base = 20000.0;
        }
        else
        {
            w.mean_wind = 12.0;
            w.wind_variation = 10.0;
            w.temperature_range = 10.0;
        }
        std::ifstream file(data_root + "/data/weather/bodies.json");
        if (file)
        {
            try
            {
                nlohmann::json j;
                file >> j;
                if (j.contains(body) && j[body].is_object())
                {
                    const nlohmann::json &o = j[body];
                    const auto set = [&o](const char *key, double &value) {
                        if (o.contains(key) && o[key].is_number())
                            value = o[key].get<double>();
                    };
                    set("mean_wind", w.mean_wind);
                    set("wind_variation", w.wind_variation);
                    set("cloudiness", w.cloudiness);
                    set("rain_chance", w.rain_chance);
                    set("mean_temperature", w.mean_temperature);
                    set("temperature_range", w.temperature_range);
                    set("pressure", w.pressure);
                    set("pressure_variation", w.pressure_variation);
                    set("cloud_base", w.cloud_base);
                    set("cloud_depth", w.cloud_depth);
                    if (o.contains("rain") && o["rain"].is_boolean())
                        w.rain_is_precipitation = o["rain"].get<bool>();
                }
            }
            catch (const std::exception &)
            {
            }
        }
        return w;
    }
}
