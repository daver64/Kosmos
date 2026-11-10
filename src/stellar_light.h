#pragma once

#include <algorithm>
#include <cmath>

namespace kosmos
{
    // Display exposure compresses inverse-square irradiance, so distant worlds remain visible.
    inline float stellar_light_scale(double luminosity, double distance_metres)
    {
        constexpr double au = 149597870700.0;
        const double distance_au = std::max(distance_metres / au, 0.001);
        const double flux = std::max(luminosity, 0.0) / (distance_au * distance_au);
        return static_cast<float>(std::min(std::pow(flux, 0.3), 1.0));
    }
}
