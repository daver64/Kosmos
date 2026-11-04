#include "transfer_plan.h"

#include <cmath>

namespace kosmos
{
    namespace
    {
        constexpr double pi = 3.14159265358979323846;

        // Floor modulo, as Lua's % behaves: the result takes the divisor's sign.
        double wrap(double value, double period) { return value - std::floor(value / period) * period; }
    }

    HohmannPlan plan_hohmann(double mu, double r1, double r2, double phase)
    {
        HohmannPlan p;
        if (!(mu > 0.0) || !(r1 > 0.0) || !(r2 > 0.0))
            return p;
        const double a = (r1 + r2) / 2.0;
        const double v1 = std::sqrt(mu / r1), v2 = std::sqrt(mu / r2);
        p.dv1 = v1 * (std::sqrt(2.0 * r2 / (r1 + r2)) - 1.0);
        p.dv2 = v2 * (1.0 - std::sqrt(2.0 * r1 / (r1 + r2)));
        p.tof = pi * std::sqrt(a * a * a / mu);
        const double ns = std::sqrt(mu / (r1 * r1 * r1)), nt = std::sqrt(mu / (r2 * r2 * r2));
        p.lead = pi - nt * p.tof;
        p.phase = phase;
        if (std::fabs(ns - nt) > 1e-12)
        {
            p.synodic = 2.0 * pi / std::fabs(ns - nt);
            p.wait = wrap((p.lead - phase) / (nt - ns), p.synodic);
            p.has_window = true;
        }
        return p;
    }

    double window_wait(const HohmannPlan &plan, int index) { return plan.wait + index * plan.synodic; }
}
