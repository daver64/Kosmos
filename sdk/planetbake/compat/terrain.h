#pragma once

// A stand-in for simlib's terrain.h, so the game's planet generator (planet_gen.cpp) builds here without simlib.
// Only what planet_gen.h and planet_gen.cpp use is declared.

#include <glm/glm.hpp>

namespace sl
{
    using dvec3 = glm::dvec3;

    class TerrainSource
    {
    public:
        virtual ~TerrainSource() = default;
        virtual double height(double x, double z) const = 0;
        virtual double height_on_sphere(const dvec3 &direction, double radius) const { (void)radius; return height(direction.x, direction.z); }
        virtual double min_height() const = 0;
        virtual double max_height() const = 0;
    };
}
