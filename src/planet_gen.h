#pragma once

#include <memory>
#include <string>

#include "terrain.h"
#include <glm/gtc/quaternion.hpp>

namespace sl
{
    struct Bitmap;
}

namespace kosmos
{
    /** The broad kind of an invented world. Each has its own generator, all deterministic from the seed. */
    enum class Archetype
    {
        none,
        mercury, // airless and cratered
        venus,   // smooth volcanic plains under an opaque haze
        earth,   // continents, oceans, ice caps and biomes
        mars,    // rust dust, basalt, polar caps, canyons
        gas      // banded atmosphere with storms; no ground (also used for ice giants)
    };

    Archetype archetype_from_name(const std::string &name);
    const char *archetype_name(Archetype type);

    /** What a body looks like: the archetype plus the parameters galaxy generation varies from planet to planet. */
    struct PlanetStyle
    {
        Archetype type = Archetype::none;
        int seed = 1;
        /** Earth-like: fraction of the surface under water (0-1). */
        float ocean = 0.6f;
        /** Earth and Mars-like: how far the ice caps reach (0 none, 1 snowball). Mercury-like: bright icy surface. */
        float ice = 0.15f;
        /** Multiplies the height of mountains and the depth of craters. */
        float relief = 1.0f;
        /** Earth-like: amplitude in metres of additional kilometre-scale hills. */
        float local_relief = 0.0f;
        /** The body's main colour (0-1); each archetype derives its palette from it. */
        float tint[3] = {0.6f, 0.6f, 0.6f};
        /** Gas: number of latitude bands, and how strongly they differ in colour (0-1). */
        int bands = 9;
        float contrast = 0.5f;

        bool valid() const { return type != Archetype::none; }
        /** True when the world has a solid surface that gets streaming terrain. */
        bool has_ground() const { return type != Archetype::none && type != Archetype::gas; }
        bool use_generated_ground(bool real_world) const { return has_ground() && !real_world; }
        /** A stable text key of every parameter, for naming cached images. */
        std::string key() const;
    };

    /** The surface of one world. Const after construction and safe to call from the terrain worker threads. */
    class PlanetSurface
    {
    public:
        PlanetSurface(const PlanetStyle &style, double radius);
        ~PlanetSurface();

        const PlanetStyle &style() const { return style_; }
        /** Height in metres above the mean radius in a unit direction from the planet's centre. Sea level is zero. */
        double height(const sl::dvec3 &direction) const;
        double min_height() const;
        double max_height() const;
        /** Linear 0-1 colour for the ground at a direction. `slope_degrees` exposes bare rock on steep ground. */
        void colour(const sl::dvec3 &direction, double height, double slope_degrees, float rgb[3]) const;
        /** The colour seen from space: the ground, or for a hazy world the cloud tops. */
        void far_colour(const sl::dvec3 &direction, double height, float rgb[3]) const;

        /** Equirectangular far-view texture of the given size, built on several threads. The caller destroys it. */
        sl::Bitmap *make_texture(int width, int height) const;
        /** The same, for the rectangle u0..u1 by v0..v1 of the equirectangular map (both 0-1), as one tile of a larger image. At most max_threads threads are used. */
        sl::Bitmap *make_region(double u0, double v0, double u1, double v1, int width, int height, unsigned max_threads = 16) const;
        /** The terrain source for sl::Terrain (not valid for a gas world). */
        std::shared_ptr<sl::TerrainSource> make_source(const glm::dquat &orientation = glm::dquat(1.0, 0.0, 0.0, 0.0)) const;

    private:
        struct Impl;
        PlanetStyle style_;
        std::shared_ptr<const Impl> impl_;
    };

    /** How the far-view texture maps longitude (0..1 across the image) to a direction in the body's frame. */
    sl::dvec3 texture_direction(double u, double v);
}
