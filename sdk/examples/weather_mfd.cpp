// An MFD page that shows the weather at the flown ship: wind, cloud, visibility and temperature, refreshed once a second,
// with a note when the "weather" event says it changed.
#include "kosmos_link.hpp"

#include <cstdio>

int main()
{
    try
    {
        kosmos::Link link("weather-mfd", "Weather page", KL_CAP_READ | KL_CAP_DRAW);
        double latitude = 0, longitude = 0, altitude = 0;
        kl_weather w{};
        bool have = false;
        link.on_value = [&](const std::string &path, const kl_value &v) {
            if (path.find("/latitude") != std::string::npos)
                latitude = v.real;
            else if (path.find("/longitude") != std::string::npos)
                longitude = v.real;
            else if (path.find("/altitude") != std::string::npos)
                altitude = v.real;
        };
        link.on_event = [&](const std::string &name, const std::string &arg) {
            if (name == "tick" || name == "weather")
                have = link.weather_at(latitude, longitude, altitude, w);
            else if (name == "mfd")
            {
                kl_client *c = link.raw();
                char text[96];
                kl_draw_begin(c);
                kl_draw_colour(c, 120, 255, 140);
                kl_draw_text(c, 200, 30, "WEATHER", 1);
                if (have)
                {
                    std::snprintf(text, sizeof text, "WIND %03.0f / %.0f KT", w.wind_from, w.wind_speed * 1.94384);
                    kl_draw_text(c, 200, 120, text, 1);
                    std::snprintf(text, sizeof text, "CLOUD %.0f%%  BASE %.0f M", w.cloud_cover * 100.0, w.cloud_base);
                    kl_draw_text(c, 200, 160, text, 1);
                    std::snprintf(text, sizeof text, "VIS %.1f KM  RAIN %.0f%%", w.visibility / 1000.0, w.rain * 100.0);
                    kl_draw_text(c, 200, 200, text, 1);
                    std::snprintf(text, sizeof text, "%.0f C  %.0f HPA", w.temperature, w.pressure);
                    kl_draw_text(c, 200, 240, text, 1);
                }
                else
                    kl_draw_text(c, 200, 190, "NO WEATHER HERE", 1);
                kl_draw_end(c, arg.c_str());
            }
        };
        kl_define_mfd(link.raw(), "weather", "WEATHER");
        link.subscribe_event("tick");
        link.subscribe_event("weather");
        link.subscribe("/ships/ship[0]/latitude", 1);
        link.subscribe("/ships/ship[0]/longitude", 1);
        link.subscribe("/ships/ship[0]/altitude", 1);
        while (link.poll(50))
        {
        }
    }
    catch (const std::exception &e)
    {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
    return 0;
}
