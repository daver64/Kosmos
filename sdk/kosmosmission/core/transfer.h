// Transfer planning: Hohmann, patched-conic ejection and capture between sibling bodies (Lambert on the shared parent's frame), local transfers to a
// moon, return to the parent, plane changes and porkchop grids. The burn-delay search mirrors addons/stockmfd/orbitcomp.lua.
#pragma once

#include "bodies.h"

#include <functional>
#include <string>
#include <vector>

namespace km
{
    /** Speed to add to a circular orbit of radius r about a body of GM mu to leave it with hyperbolic excess `vinf` (or the same to capture from it). */
    double burn_from_circular(double mu, double r, double vinf);
    double plane_change_dv(double speed, double angle);

    struct Hohmann
    {
        double dv1 = 0, dv2 = 0, total = 0, tof = 0, synodic = 0;
    };
    /** Two-burn transfer between circular, coplanar orbits of radius r1 and r2. */
    Hohmann hohmann(double mu, double r1, double r2);

    struct Transfer
    {
        bool valid = false;
        std::string note;
        double depart = 0;  // seconds after the epoch (or after the start, for a local transfer)
        double wait = 0;    // the same, when the departure is a burn from the ship's own orbit
        double tof = 0;
        double vinf_dep = 0, vinf_arr = 0;
        double dv_dep = 0, dv_arr = 0, total = 0;
        Vec r_dep, v_dep; // transfer-orbit state at departure, in the central body's frame
        Vec r_arr, v_arr;
    };

    /** Position and velocity of something at time t, in a central frame. */
    using Provider = std::function<void(double t, Vec &r, Vec &v)>;

    struct Porkchop
    {
        int nx = 0, ny = 0;
        double x0 = 0, dx = 0, y0 = 0, dy = 0; // x is the departure time (or wait), y the flight time
        std::vector<double> dv;                 // nx*ny, total dV or a negative number where there is no solution
        int best = -1;
        double at(int ix, int iy) const { return dv[iy * nx + ix]; }
    };

    /** The problem a Lambert transfer solves: leave `dep` at t, arrive at `arr` at t+tof, both moving about a central body of GM `mu`. */
    struct LambertProblem
    {
        double mu = 0;
        Provider dep, arr;
        Vec normal;            // the sense of the transfer (angular momentum of the departure body or ship)
        bool dep_hyperbolic = true; // true: depart a body's parking orbit with an ejection burn; false: burn directly from the ship's orbit
        double dep_mu = 0, dep_r = 0;
        double arr_mu = 0, arr_r = 0;
    };

    Transfer evaluate(const LambertProblem &p, double depart, double tof);
    Porkchop porkchop(const LambertProblem &p, double depart0, double depart_span, double tof0, double tof_span, int nx, int ny);
    /** The cheapest point of the grid, refined with a finer search around it. */
    Transfer optimise(const LambertProblem &p, const Porkchop &grid);

    /** Ejection from a circular orbit about `from` to capture into a circular orbit about `to`, both planets (or both moons) of one parent. */
    bool sibling_problem(const Bodies &bodies, const std::string &from, const std::string &to, double r_dep, double r_arr, LambertProblem &out, std::string &err);
    /** Burn from a circular orbit (state r, v about `parent`) to capture at its moon `moon`. */
    bool moon_problem(const Bodies &bodies, const std::string &parent, const std::string &moon, Vec r, Vec v, double r_arr, LambertProblem &out, std::string &err);

    /** From a circular orbit about `moon` back to a circular orbit of radius r_pe about the moon's parent. Analytic, coplanar. */
    Transfer return_to_parent(const Bodies &bodies, const std::string &moon, double r_park, double r_pe, std::string &err);

    /** Shortcut: a plain Hohmann sibling-planet estimate (coplanar, circular) used for first guesses and cross-checks. */
    Transfer sibling_hohmann(const Bodies &bodies, const std::string &from, const std::string &to, double r_dep, double r_arr, std::string &err);
} // namespace km
