#pragma once

// The layers planetbake makes for a world, one rectangle of the equirectangular map at a time.
// The terrain and colours come from the game's own generator (planet_gen), so a baked world matches what Kosmos draws.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "planet_gen.h"

namespace planetbake
{
    /** The most threads any part of this tool uses. It is a hard cap: a job can churn through a great deal of data. */
    constexpr unsigned max_threads = 4;

    enum Layer
    {
        layer_colour,   // surface colour, with the generator's relief shading
        layer_height,   // terrain height, 16 bit
        layer_normal,   // tangent space normal map (R east, G north, B up)
        layer_specular, // gloss: white is shiny (seas, ice)
        layer_night,    // city lights on the night side (Earth-like worlds)
        layer_clouds,   // white cloud cover in the alpha channel (worlds with an atmosphere: earth, venus, mars)
        layer_count
    };

    const char *layer_name(int layer);
    /** -1 if the name is not a layer. */
    int layer_from_name(const std::string &name);

    struct LayerSet
    {
        bool on[layer_count] = {false, false, false, false, false, false};
        bool any() const;
        static LayerSet all();
    };

    /** A world to make layers for. Const after construction. */
    class World
    {
    public:
        World(const kosmos::PlanetStyle &style, double radius_m, float normal_strength = 8.0f, float detail = 1.0f);

        const kosmos::PlanetStyle &style() const { return surface_.style(); }
        double radius() const { return radius_; }
        double lowest() const { return surface_.min_height(); }
        double highest() const { return surface_.max_height(); }
        float normal_strength() const { return normal_strength_; }
        const kosmos::PlanetSurface &surface() const { return surface_; }
        /** Whether the layer means anything for this world (a gas giant has only colour). */
        bool applicable(int layer) const;
        /** City light brightness 0-1 at a point on dry land; the rest of the world is dark. */
        /**
         * Fine variation for land colour at scales from tens of kilometres down to tens of metres, scaled by how fine the pixels are
         * (a coarse preview gets none). Multiplies the colour in place; pixel_m is the ground distance one pixel covers.
         */
        void add_detail(const sl::dvec3 &direction, double pixel_m, float rgb[3]) const;
        /** Cloud opacity 0-1 and the cloud's brightness, of the type that suits the world. */
        float cloud(const sl::dvec3 &direction, float tint[3]) const;
        float lights(const sl::dvec3 &direction, double height, const float rgb[3]) const;

    private:
        struct Night;
        struct Clouds;
        struct Detail;
        kosmos::PlanetSurface surface_;
        double radius_;
        float normal_strength_;
        float detail_;
        std::shared_ptr<const Night> night_;
        std::shared_ptr<const Clouds> clouds_;
        std::shared_ptr<const Detail> detail_noise_;
    };

    /** A rectangle u0..u1 by v0..v1 of the map (both 0-1), rendered at w x h pixels. */
    struct Region
    {
        double u0 = 0.0, v0 = 0.0, u1 = 1.0, v1 = 1.0;
        int w = 0, h = 0;
    };

    struct Rendered
    {
        int w = 0, h = 0;
        std::vector<std::uint8_t> layer[layer_count]; // RGBA8 for every layer but height
        std::vector<std::uint16_t> height;            // linear between the world's lowest and highest height
    };

    /**
     * Fills the requested layers of one region. The terrain is sampled once, with a one pixel border so shading and normals are
     * continuous across neighbouring regions. At most `threads` (never more than max_threads) threads work, and only one region
     * is rendered at a time in the whole process, so the cap holds even when a preview and an export overlap.
     */
    bool render_region(const World &world, const Region &region, const LayerSet &layers, Rendered &out, unsigned threads, std::string &error);
}
