// Adds an MFD page that shows the flown ship's speed and altitude, and a key (G) that logs a line in Kosmos. Shows the first ship; read /sim/active-ship to follow the flown one.
#include "kosmos_link.hpp"

#include <cstdio>

int main()
{
    try
    {
        kosmos::Link link("speed-mfd", "Speed page", KL_CAP_READ | KL_CAP_DRAW | KL_CAP_KEYS);
        double speed = 0, altitude = 0;
        link.on_value = [&](const std::string &path, const kl_value &v) {
            if (path.find("/keys/") != std::string::npos)
            {
                if (v.boolean)
                    link.log("key pressed");
            }
            else if (path.find("/speed") != std::string::npos)
                speed = v.real;
            else if (path.find("/altitude") != std::string::npos)
                altitude = v.real;
        };
        link.on_event = [&](const std::string &name, const std::string &arg) {
            if (name == "mfd")
            {
                kl_client *c = link.raw();
                char text[64];
                kl_draw_begin(c);
                kl_draw_colour(c, 120, 255, 140);
                kl_draw_text(c, 200, 30, "SPEED", 1);
                std::snprintf(text, sizeof text, "%.0f m/s", speed);
                kl_draw_text(c, 200, 190, text, 1);
                std::snprintf(text, sizeof text, "%.0f m", altitude);
                kl_draw_text(c, 200, 230, text, 1);
                kl_draw_end(c, arg.c_str());
            }
        };
        kl_define_mfd(link.raw(), "speed", "SPEED");
        kl_define_key(link.raw(), "ping", "Log a line", "Speed page", "letter_g", 0, 0);
        link.subscribe("/ships/ship[0]/speed", 10);
        link.subscribe("/ships/ship[0]/altitude", 10);
        link.subscribe("/addons/speed-mfd/keys/ping", 20);
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
