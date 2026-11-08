#pragma once

namespace kosmos::app
{
    inline double orbiter_max_height(bool home, bool mars)
    {
        return home ? 9000.0 : (mars ? 30000.0 : 12000.0);
    }

    inline bool terrain_visible(double altitude, double radius, bool has_image, bool orbital_relief)
    {
        const double threshold = 300.0e3 * radius / 6.371e6;
        return altitude < threshold * (!has_image || orbital_relief ? 10.0 : 1.0);
    }
}
