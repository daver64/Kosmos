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

    inline bool earth_sphere_visible(double altitude, double radius, bool has_image, bool has_tiles,
                                     bool has_orbital_relief, double tile_switch)
    {
        return (has_image || has_orbital_relief) &&
               altitude > (has_tiles || has_orbital_relief ? tile_switch : 300.0e3) * radius / 6.371e6;
    }
}
