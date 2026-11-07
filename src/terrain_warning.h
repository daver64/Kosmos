#pragma once

#include "flight.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace kosmos
{
    /** Sample a constant-velocity, ground-relative track; 0 means no sampled intersection. */
    inline double terrain_warning_time(const sl::dvec3 &up, const sl::dvec3 &horizontal, double radius,
                                       double clearance, double vertical,
                                       const std::function<double(const sl::dvec3 &)> &height)
    {
        const double speed = glm::length(horizontal);
        const sl::dvec3 along = speed > 1e-6 ? horizontal / speed : sl::dvec3(0.0);
        const double ground = height(up);
        for (int seconds = 2; seconds <= 30; seconds += 2)
        {
            const double angle = speed * seconds / radius;
            const sl::dvec3 direction = up * std::cos(angle) + along * std::sin(angle);
            if (clearance + vertical * seconds - (height(direction) - ground) <= 0.0)
                return seconds;
        }
        return 0.0;
    }
}
