#pragma once

#include "draw.h"
#include "terrain_mesh.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace kosmos
{
    inline glm::dvec3 orbiter_tile_direction(double u, double v)
    {
        const double longitude = 2.0 * glm::pi<double>() * u;
        const double latitude = glm::pi<double>() * v;
        return {std::sin(latitude) * std::cos(longitude), std::cos(latitude),
                -std::sin(latitude) * std::sin(longitude)};
    }

    inline bool apply_orbiter_mesh_colours(sl::Bitmap &bitmap, const MeshSource &meshes,
                                           const glm::dquat &frame, int level, int column, int row)
    {
        const std::size_t width = static_cast<std::size_t>(bitmap.width);
        const std::size_t height = static_cast<std::size_t>(bitmap.height);
        if (bitmap.width <= 0 || bitmap.height <= 0 || width > bitmap.pixels.size() / 4 / height)
            return false;
        const double rows = std::ldexp(1.0, level);
        const double columns = rows * 2.0;
        const auto terrain_direction = [&](double u, double v)
        {
            const glm::dvec3 tile = orbiter_tile_direction(u, v);
            return frame * glm::dvec3(-tile.x, tile.y, -tile.z);
        };
        const glm::dvec3 centre = terrain_direction((column + 0.5) / columns, (row + 0.5) / rows);
        if (!meshes.colour_may_overlap(centre, glm::pi<double>() / rows))
            return true;
        bool changed = false;
        for (int y = 0; y < bitmap.height; ++y)
            for (int x = 0; x < bitmap.width; ++x)
            {
                const double u = (column + (x + 0.5) / bitmap.width) / columns;
                const double v = (row + (y + 0.5) / bitmap.height) / rows;
                const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 4;
                float rgb[3] = {bitmap.pixels[index] / 255.0f, bitmap.pixels[index + 1] / 255.0f,
                                bitmap.pixels[index + 2] / 255.0f};
                if (!meshes.colour(terrain_direction(u, v), rgb))
                    continue;
                for (int channel = 0; channel < 3; ++channel)
                    bitmap.pixels[index + channel] = static_cast<std::uint8_t>(
                        std::lround(std::clamp(rgb[channel], 0.0f, 1.0f) * 255.0f));
                changed = true;
            }
        if (changed)
            bitmap.ram_dirty = true;
        return true;
    }
}
