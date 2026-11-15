#pragma once

#include <algorithm>
#include <cstdint>

namespace kosmos::app
{
    inline void remove_blue_night_artifact(std::uint8_t *pixel)
    {
        if (pixel[2] > 4 && 2 * pixel[2] > 3 * std::max(pixel[0], pixel[1]))
            pixel[0] = pixel[1] = pixel[2] = 0;
    }
}
