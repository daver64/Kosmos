#pragma once

#include "terrain.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

namespace kosmos
{
    inline glm::dquat orbiter_tile_orientation(const glm::dquat &body_orientation)
    {
        return body_orientation * glm::angleAxis(glm::pi<double>(), glm::dvec3(0.0, 1.0, 0.0));
    }

    inline double orbiter_tile_height(const sl::TerrainSource &source, const glm::dquat &terrain_frame,
                                     const glm::dvec3 &tile_direction, double radius, double cell_size)
    {
        const glm::dvec3 body_direction(-tile_direction.x, tile_direction.y, -tile_direction.z);
        return source.height_on_sphere_lod(terrain_frame * body_direction, radius, cell_size) / radius;
    }
}
