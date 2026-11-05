// The solar system as KosmosMission sees it: bodies read from data/sol.json, with Keplerian ephemerides measured from t = 0 (the elements' epoch).
#pragma once

#include "orbit.h"

#include <string>
#include <vector>

namespace km
{
    struct Body
    {
        std::string name, kind, parent;
        double gm = 0, radius = 0, spin = 0;
        double atm_density = 0, scale_height = 0; // surface density (kg/m3) and scale height (m); 0 if airless
        bool has_orbit = false;
        double a = 0, e = 0, i = 0, raan = 0, argp = 0, m0 = 0; // metres and radians
        double surface_gravity() const { return radius > 0 ? gm / (radius * radius) : 0; }
        /** Speed of the ground at the equator, m/s. */
        double equator_speed() const { return spin * radius; }
        double circular_speed(double r) const { return std::sqrt(gm / r); }
    };

    class Bodies
    {
      public:
        /** Reads `path` (data/sol.json). Returns false and sets `err` on failure. */
        bool load(const std::string &path, std::string &err);
        const Body *find(const std::string &name) const;
        const std::vector<Body> &all() const { return bodies_; }
        /** Position and velocity of `b` relative to its parent at `t` seconds after the epoch. Zero for the root body. */
        void state(const Body &b, double t, Vec &r, Vec &v) const;
        /** The body's orbit about its parent. */
        Orbit orbit_of(const Body &b) const;
        void add(const Body &b) { bodies_.push_back(b); }

      private:
        std::vector<Body> bodies_;
    };
} // namespace km
