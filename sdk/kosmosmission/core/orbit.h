// KosmosMission's orbital mechanics: vectors, Keplerian states and the Lambert solver. A port of the maths in addons/stockmfd/orbitcomp.lua,
// with no GTK, GL or Kosmos dependencies, so the planner and its tests can use it alone.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace km
{
    constexpr double PI = 3.14159265358979323846;
    constexpr double TAU = 2.0 * PI;
    constexpr double G0 = 9.80665;
    constexpr double AU = 1.495978707e11;
    constexpr double DAY = 86400.0;

    struct Vec
    {
        double x = 0, y = 0, z = 0;
    };
    inline Vec operator+(Vec a, Vec b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    inline Vec operator-(Vec a, Vec b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    inline Vec operator*(Vec a, double s) { return {a.x * s, a.y * s, a.z * s}; }
    inline double dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    inline Vec cross(Vec a, Vec b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
    inline double length(Vec a) { return std::sqrt(dot(a, a)); }
    inline Vec unit(Vec a)
    {
        const double l = length(a);
        return l > 0 ? a * (1.0 / l) : Vec{1, 0, 0};
    }
    inline double clamp(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
    inline double wrap(double v, double m)
    {
        v = std::fmod(v, m);
        return v < 0 ? v + m : v;
    }
    /** `v` turned about the unit axis `k` by `angle` (Rodrigues). */
    Vec rotate(Vec v, Vec k, double angle);

    /** An orbit about a central body with GM `mu`, with its own perifocal axes (P towards periapsis, Q, W the normal). */
    struct Orbit
    {
        double mu = 0, e = 0, p = 0, a = 0, n = 0, period = 0, rp = 0, ra = 0, nu = 0;
        Vec P, Q, W;
        bool bound = false;
    };

    std::optional<Orbit> make_orbit(Vec r, Vec v, double mu);
    /** Orbit from elements (angles in radians): semi-major axis a, eccentricity e, inclination i, node raan, periapsis argp, true anomaly nu. */
    Orbit orbit_from_elements(double mu, double a, double e, double i, double raan, double argp, double nu);
    double mean_from_nu(const Orbit &o, double nu);
    double nu_from_mean(const Orbit &o, double m);
    /** True anomaly after `dt` seconds from `nu`. */
    double advance(const Orbit &o, double nu, double dt);
    /** Position and velocity at true anomaly `nu`. */
    void state_at(const Orbit &o, double nu, Vec &r, Vec &v);
    /** Seconds from `nu_from` until the orbit reaches `nu_to`, or -1 if a hyperbolic orbit is already past it. */
    double time_to(const Orbit &o, double nu_from, double nu_to);

    /**
     * Velocity at r1 of the single-revolution transfer to r2 in `tof` seconds. `normal` (the direction the body orbits about) makes a transfer
     * that has to sweep more than half a turn go the long way. Returns nullopt when there is no solution. `arrival` receives the velocity at r2.
     */
    std::optional<Vec> lambert(Vec r1, Vec r2, double tof, double mu, const Vec *normal = nullptr, Vec *arrival = nullptr);
} // namespace km
