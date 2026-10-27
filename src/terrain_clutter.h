#pragma once

#include "base.h"
#include "landclass.h"
#include "planet_data.h"
#include "universe.h"

#include <clutter.h>

#include <memory>
#include <vector>

namespace kosmos
{
    /** Ground that clutter keeps off: a level base site (a disc, or a strip when a and b differ) and the margin around it. */
    struct ClutterZone
    {
        sl::dvec3 a, b;
        double radius = 0.0;
    };

    /** What a world's ground looks like to the clutter: its kind, and the sites and land cover that shape it. */
    struct ClutterWorld
    {
        const Body *body = nullptr;
        SurfaceFrame frame;
        std::vector<ClutterZone> zones;
        /** Land cover for Earth; null for every other world. */
        std::shared_ptr<const Landclass> land;
        double density = 1.0;
        /** Game root, where the rock textures are found. */
        std::string root;
    };

    /** The zones to keep clear around a body's bases, from the same sites the terrain is levelled at. */
    std::vector<ClutterZone> clutter_zones(const std::vector<Base> &bases, const Universe &universe, int body_id, double radius);

    /**
     * The scatter for one world, chosen by what it is: conifers, broadleaf trees, shrubs, houses and rocks on an Earth,
     * alien plants on an inhabited invented world, boulders on a rocky world, and (rarely, in clusters) pressure domes and habitat tubes on an airless or thin-aired one.
     * Needs a graphics context and must be called outside begin_surface_3d(). Returns null for a world with no clutter.
     */
    std::unique_ptr<sl::ClutterLayer> make_clutter(const sl::Terrain &terrain, const ClutterWorld &world);

    /** Metres along the ground from a body-fixed direction to the nearest base site; very large when the world has none. */
    double distance_to_zones(const std::vector<ClutterZone> &zones, const sl::dvec3 &direction, double radius);

    /** True when a world gets any clutter at all. */
    bool has_clutter(const Body &body);
}
