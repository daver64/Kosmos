#pragma once

#include <algorithm>

namespace kosmos
{
    inline constexpr double vessel_ascent_min_target_km = 150.0;
    inline constexpr double vessel_ascent_max_target_km = 1000.0;

    inline double vessel_ascent_pressure_factor(double pressure)
    {
        constexpr double limit_pa = 70000.0;
        constexpr double reduction_range_pa = 80000.0;
        constexpr double minimum_factor = 0.3;
        return std::clamp(1.0 - std::max(0.0, pressure - limit_pa) / reduction_range_pa, minimum_factor, 1.0);
    }
}
