#pragma once

#include <array>
#include <string>
#include <vector>

namespace kosmos
{
    /** One cloud layer of a METAR: cover is FEW, SCT, BKN, OVC (or VV for a sky obscured by fog) with its base in feet. */
    struct MetarCloud
    {
        std::string cover;
        int base_ft = 0;
        int tower = 0; // 1 for TCU (towering cumulus), 2 for CB (cumulonimbus)
    };

    /** The parts of a METAR observation the simulation uses. Wind speeds are in knots, visibility in metres. */
    struct Metar
    {
        std::string station;
        int day = 0, hour = 0, minute = 0;
        int wind_dir = -1;      // degrees the wind blows from; -1 for variable or calm
        int wind_kt = 0;
        int gust_kt = 0;        // 0 when not gusting
        int visibility_m = 9999;
        std::vector<std::string> weather; // present weather groups such as "-RA", "+TSRA", "FG"
        std::vector<MetarCloud> clouds;
        int temperature_c = 15, dewpoint_c = 10;
        int qnh_hpa = 1013;
        bool valid = false;
    };

    /** The METAR line for an observation, e.g. "EFHK 271750Z 24012G22KT 9999 -RA SCT025 BKN040 08/05 Q1008". */
    std::string encode_metar(const Metar &m);

    /**
     * Reads a METAR line (ICAO or US style: wind in KT or MPS, visibility in metres or SM, altimeter as Q or A, "CAVOK",
     * vertical visibility) into `out`; the remarks section is ignored. Returns false when no station and time group is found.
     */
    bool decode_metar(const std::string &line, Metar &out);

    /** One cloud layer of the weather at a place. `type` is an sl::CloudType (cumulus 0, stratocumulus 1, stratus 2, altocumulus 3, cirrus 4, cumulonimbus 5). */
    struct CloudLayerInfo
    {
        double base = 0.0, top = 0.0; // metres above the ground
        double cover = 0.0;           // 0 to 1
        int type = 0;
    };

    /** What the simulation needs at a place: wind in m/s, rain and cloud, surface wetness. */
    struct Conditions
    {
        double wind_east = 0.0, wind_north = 0.0; // m/s, the direction the air moves toward
        double gust = 0.0;                        // extra m/s the wind can briefly add
        double thunder = 0.0;                     // 0 none, 1 a thunderstorm overhead (lightning)
        double rain = 0.0;                        // 0 dry, about 0.3 light, 0.6 moderate, 1 heavy or worse
        double cloud_cover = 0.0;                 // 0 clear to 1 overcast
        double cloud_base = 3000.0;               // metres above the ground
        double cloud_top = 4000.0;
        // The METAR's layers, lowest first (cloud_cover, cloud_base and cloud_top above summarise them for the rest of the simulation).
        static constexpr int max_cloud_layers = 4;
        CloudLayerInfo layers[max_cloud_layers];
        int layer_count = 0;
        double visibility = 10000.0;              // metres
        double temperature = 15.0;                // Celsius
        double pressure = 1013.0;                 // hectopascals at the surface
    };

    Conditions conditions_from(const Metar &m);
    /** The top of the highest cloud layer, which for a storm is far above the lowest base. */
    double highest_cloud_top(const Conditions &c);
    Conditions blend(const Conditions &a, const Conditions &b, double t);

    /** Seconds since 1970 of a METAR day, hour and minute nearest to `reference` (the month is taken from it). */
    double metar_time(int day, int hour, int minute, double reference);
    /** Day of month, hour and minute (UTC) for a time in seconds since 1970. */
    void unix_to_dhm(double unix_seconds, int &day, int &hour, int &minute);

    /** How a body's weather behaves; bodies without an atmosphere have none. */
    struct BodyWeather
    {
        double mean_wind = 5.0;     // m/s
        double wind_variation = 4.0;
        double cloudiness = 0.5;    // 0 never cloudy to 1 always
        double rain_chance = 0.0;   // 0 never to 1 often
        double mean_temperature = 15.0, temperature_range = 15.0; // Celsius, pole to equator
        double pressure = 1013.0;   // hPa at the surface
        double pressure_variation = 12.0;
        double cloud_base = 1500.0, cloud_depth = 2500.0; // metres
        bool rain_is_precipitation = true;               // false: dust and haze instead of rain
    };

    struct WeatherStation
    {
        std::string id;
        double latitude = 0.0, longitude = 0.0; // degrees
        double elevation = 0.0;
        std::string line; // the current METAR
        Metar metar;
        Conditions now, before;
        bool live = false;
    };

    /** The weather of one body: a ring of stations, each holding a one-line METAR, blended between at any place. */
    class WeatherField
    {
    public:
        static constexpr int default_stations = 64;

        /** Procedural field for a body: stations evenly spread over the sphere. */
        WeatherField(const std::string &body, const BodyWeather &params, int seed, int stations = default_stations);
        /** Field with given stations (e.g. real airports); missing observations are generated. */
        WeatherField(const std::string &body, const BodyWeather &params, int seed, std::vector<WeatherStation> stations);

        /** Brings every procedural station up to date for `unix_seconds`, generating a fresh METAR every ten minutes. */
        void update(double unix_seconds);
        /** Replaces a station's observation with a real METAR line; returns false if the station is unknown or the line unreadable. */
        bool set_live(const std::string &line, double unix_seconds);
        /** Returns the station's observation to a procedural one. */
        void clear_live();

        /** Conditions at a place, blended from the nearest stations. */
        Conditions sample(double latitude, double longitude) const;
        /** The nearest station to a place, or null when there are none. */
        const WeatherStation *nearest(double latitude, double longitude, double *distance_km = nullptr) const;

        const std::vector<WeatherStation> &stations() const { return stations_; }
        const std::string &body() const { return body_; }
        const BodyWeather &params() const { return params_; }
        /** True when the stations are real airfields (so live observations apply) rather than a procedural ring. */
        bool real_stations() const { return real_; }
        /** Four-letter, NOAA-style identifier of procedural station `index` on `body`. */
        static std::string station_id(const std::string &body, int index);

    private:
        void generate(WeatherStation &s, double unix_seconds, long bucket) const;

        std::string body_;
        BodyWeather params_;
        int seed_;
        std::vector<WeatherStation> stations_;
        std::vector<std::array<double, 3>> directions_;
        bool real_ = false;
        long bucket_ = -1;
        double fraction_ = 1.0;
    };

    /** Parameters for a body, from data/weather/bodies.json when it lists the body, else a guess from the atmosphere. */
    BodyWeather body_weather_for(const std::string &body, double atmosphere_density, double surface_temperature_k, const std::string &data_root);
}
