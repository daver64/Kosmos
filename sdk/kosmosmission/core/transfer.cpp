#include "transfer.h"

namespace km
{
    double burn_from_circular(double mu, double r, double vinf) { return std::sqrt(vinf * vinf + 2 * mu / r) - std::sqrt(mu / r); }
    double plane_change_dv(double speed, double angle) { return 2 * speed * std::sin(std::fabs(angle) / 2); }

    Hohmann hohmann(double mu, double r1, double r2)
    {
        Hohmann h;
        const double a = (r1 + r2) / 2;
        h.dv1 = std::sqrt(mu / r1) * (std::sqrt(2 * r2 / (r1 + r2)) - 1);
        h.dv2 = std::sqrt(mu / r2) * (1 - std::sqrt(2 * r1 / (r1 + r2)));
        h.total = std::fabs(h.dv1) + std::fabs(h.dv2);
        h.tof = PI * std::sqrt(a * a * a / mu);
        const double n1 = std::sqrt(mu / (r1 * r1 * r1)), n2 = std::sqrt(mu / (r2 * r2 * r2));
        h.synodic = std::fabs(n1 - n2) > 1e-15 ? TAU / std::fabs(n1 - n2) : 0;
        return h;
    }

    Transfer evaluate(const LambertProblem &p, double depart, double tof)
    {
        Transfer t;
        t.depart = depart;
        t.tof = tof;
        if (tof <= 0)
            return t;
        Vec r1, v1, r2, v2;
        p.dep(depart, r1, v1);
        p.arr(depart + tof, r2, v2);
        Vec va;
        const auto vt = lambert(r1, r2, tof, p.mu, &p.normal, &va);
        if (!vt)
            return t;
        t.r_dep = r1;
        t.v_dep = *vt;
        t.r_arr = r2;
        t.v_arr = va;
        const double excess_dep = length(*vt - v1);
        t.vinf_arr = length(va - v2);
        t.vinf_dep = excess_dep;
        t.dv_dep = p.dep_hyperbolic ? burn_from_circular(p.dep_mu, p.dep_r, excess_dep) : excess_dep;
        t.dv_arr = burn_from_circular(p.arr_mu, p.arr_r, t.vinf_arr);
        t.total = t.dv_dep + t.dv_arr;
        t.valid = std::isfinite(t.total);
        return t;
    }

    Porkchop porkchop(const LambertProblem &p, double depart0, double depart_span, double tof0, double tof_span, int nx, int ny)
    {
        Porkchop g;
        g.nx = nx;
        g.ny = ny;
        g.x0 = depart0;
        g.y0 = tof0;
        g.dx = depart_span / std::max(1, nx - 1);
        g.dy = tof_span / std::max(1, ny - 1);
        g.dv.assign(size_t(nx) * ny, -1.0);
        double best = 1e30;
        for (int iy = 0; iy < ny; ++iy)
            for (int ix = 0; ix < nx; ++ix)
            {
                const Transfer t = evaluate(p, g.x0 + ix * g.dx, g.y0 + iy * g.dy);
                if (!t.valid)
                    continue;
                g.dv[size_t(iy) * nx + ix] = t.total;
                if (t.total < best)
                {
                    best = t.total;
                    g.best = iy * nx + ix;
                }
            }
        return g;
    }

    Transfer optimise(const LambertProblem &p, const Porkchop &g)
    {
        if (g.best < 0)
            return Transfer{};
        double cx = g.x0 + (g.best % g.nx) * g.dx, cy = g.y0 + (g.best / g.nx) * g.dy;
        double hx = g.dx, hy = g.dy;
        Transfer best = evaluate(p, cx, cy);
        for (int pass = 0; pass < 4; ++pass)
        {
            const double bx = cx, by = cy;
            for (int j = -4; j <= 4; ++j)
                for (int i = -4; i <= 4; ++i)
                {
                    const Transfer t = evaluate(p, bx + i * hx / 4, std::max(by + j * hy / 4, 60.0));
                    if (t.valid && (!best.valid || t.total < best.total))
                    {
                        best = t;
                        cx = t.depart;
                        cy = t.tof;
                    }
                }
            hx /= 4;
            hy /= 4;
        }
        return best;
    }

    bool sibling_problem(const Bodies &bodies, const std::string &from, const std::string &to, double r_dep, double r_arr, LambertProblem &out, std::string &err)
    {
        const Body *a = bodies.find(from), *b = bodies.find(to);
        if (!a || !b || !a->has_orbit || !b->has_orbit)
        {
            err = "unknown body";
            return false;
        }
        if (a->parent != b->parent)
        {
            err = "these bodies orbit different parents; route the mission through the parent body";
            return false;
        }
        const Body *parent = bodies.find(a->parent);
        out = LambertProblem{};
        out.mu = parent->gm;
        out.dep = [&bodies, a](double t, Vec &r, Vec &v) { bodies.state(*a, t, r, v); };
        out.arr = [&bodies, b](double t, Vec &r, Vec &v) { bodies.state(*b, t, r, v); };
        Vec r0, v0;
        bodies.state(*a, 0, r0, v0);
        out.normal = unit(cross(r0, v0));
        out.dep_hyperbolic = true;
        out.dep_mu = a->gm;
        out.dep_r = r_dep;
        out.arr_mu = b->gm;
        out.arr_r = r_arr;
        return true;
    }

    bool moon_problem(const Bodies &bodies, const std::string &parent, const std::string &moon, Vec r, Vec v, double r_arr, LambertProblem &out, std::string &err)
    {
        const Body *p = bodies.find(parent), *m = bodies.find(moon);
        if (!p || !m || m->parent != parent)
        {
            err = moon + " does not orbit " + parent;
            return false;
        }
        const auto ship = make_orbit(r, v, p->gm);
        if (!ship || !ship->bound)
        {
            err = "the ship is not in a closed orbit about " + parent;
            return false;
        }
        const Orbit so = *ship;
        out = LambertProblem{};
        out.mu = p->gm;
        out.dep = [so](double t, Vec &rr, Vec &vv) { state_at(so, advance(so, so.nu, t), rr, vv); };
        out.arr = [&bodies, m](double t, Vec &rr, Vec &vv) { bodies.state(*m, t, rr, vv); };
        out.normal = so.W;
        out.dep_hyperbolic = false;
        out.arr_mu = m->gm;
        out.arr_r = r_arr;
        return true;
    }

    Transfer return_to_parent(const Bodies &bodies, const std::string &moon, double r_park, double r_pe, std::string &err)
    {
        Transfer t;
        const Body *m = bodies.find(moon);
        const Body *p = m ? bodies.find(m->parent) : nullptr;
        if (!m || !p || !m->has_orbit)
        {
            err = "no parent body";
            return t;
        }
        const double rm = m->a, at = (rm + r_pe) / 2;
        const double v_apo = std::sqrt(p->gm * (2 / rm - 1 / at)), v_pe = std::sqrt(p->gm * (2 / r_pe - 1 / at));
        t.vinf_dep = std::sqrt(p->gm / rm) - v_apo;
        t.dv_dep = burn_from_circular(m->gm, r_park, t.vinf_dep);
        t.dv_arr = v_pe - std::sqrt(p->gm / r_pe);
        t.total = t.dv_dep + t.dv_arr;
        t.tof = PI * std::sqrt(at * at * at / p->gm);
        t.valid = true;
        t.note = "Escape retrograde to the moon's motion; circularise at perigee.";
        return t;
    }

    Transfer sibling_hohmann(const Bodies &bodies, const std::string &from, const std::string &to, double r_dep, double r_arr, std::string &err)
    {
        Transfer t;
        const Body *a = bodies.find(from), *b = bodies.find(to);
        const Body *p = a ? bodies.find(a->parent) : nullptr;
        if (!a || !b || !p || a->parent != b->parent)
        {
            err = "no common parent";
            return t;
        }
        const Hohmann h = hohmann(p->gm, a->a, b->a);
        t.vinf_dep = std::fabs(h.dv1);
        t.vinf_arr = std::fabs(h.dv2);
        t.dv_dep = burn_from_circular(a->gm, r_dep, t.vinf_dep);
        t.dv_arr = burn_from_circular(b->gm, r_arr, t.vinf_arr);
        t.total = t.dv_dep + t.dv_arr;
        t.tof = h.tof;
        t.valid = true;
        return t;
    }
} // namespace km
