#pragma once

#include "base.h"
#include "ship.h"
#include "universe.h"

#include "clutter.h"

#include <vector>

namespace kosmos
{
    /** The things a person on foot cannot walk through, near the person's body `body`. */
    struct SolidScene
    {
        const Universe *universe = nullptr;
        int body = -1;
        const std::vector<Base> *bases = nullptr;
        std::vector<Ship *> ships;
        /** Trees, rocks, houses and outposts scattered on the ground; may be null. */
        const sl::ClutterLayer *clutter = nullptr;
    };

    /** Radius of a person's body, in metres. */
    constexpr double person_radius = 0.35;

    /**
     * Move from `from` to `to` (positions relative to the body centre, world axes) without ending inside a ship, a base object
     * or a scattered object. `height` is how far the person's feet are above the ground. Returns the corrected `to`, or `from`
     * where there is no way out.
     */
    sl::dvec3 push_out_of_solids(const SolidScene &scene, const sl::dvec3 &from, const sl::dvec3 &to, double height);

    /**
     * Keep a wheeled vehicle out of buildings, other ships and scattered objects: the points around its wheelbase are pushed out
     * and the vehicle is moved by the largest push, losing the speed it had into the obstacle. `scene.ships` should not hold `ship`.
     */
    void collide_ground_vehicle(const SolidScene &scene, Ship &ship);
} // namespace kosmos
