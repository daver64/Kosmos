// Starting point for a native addon: shows the flown ship's speed on an MFD page.
// Build with the SDK's CMake (see the manual), then copy this folder (with bin/) into Kosmos's addons folder.
#include "kosmos_link.hpp"

#include <cstdio>

int main()
{
    try
    {
        kosmos::Link link("my-native-addon", "My native addon", KL_CAP_READ | KL_CAP_DRAW);
        double speed = 0;
        link.on_value = [&](const std::string &, const kl_value &v) { speed = v.real; };
        link.on_event = [&](const std::string &name, const std::string &page) {
            if (name != "mfd")
                return;
            char text[64];
            std::snprintf(text, sizeof text, "%.0f m/s", speed);
            kl_client *c = link.raw();
            kl_draw_begin(c);
            kl_draw_colour(c, 120, 255, 140);
            kl_draw_text(c, 200, 200, text, 1);
            kl_draw_end(c, page.c_str());
        };
        kl_define_mfd(link.raw(), "main", "MY PAGE");
        link.subscribe("/ships/ship[0]/speed", 10);
        while (link.poll(100))
        {
        }
    }
    catch (const std::exception &e)
    {
        std::fprintf(stderr, "my-native-addon: %s\n", e.what());
        return 1;
    }
    return 0;
}
