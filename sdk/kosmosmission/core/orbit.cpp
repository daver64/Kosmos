#include "orbit.h"

namespace km
{
    Vec rotate(Vec v, Vec k, double angle)
    {
        const double c = std::cos(angle), s = std::sin(angle);
        return v * c + cross(k, v) * s + k * (dot(k, v) * (1 - c));
    }

    std::optional<Orbit> make_orbit(Vec r, Vec v, double mu)
    {
        const double rm = length(r), v2 = dot(v, v);
        const Vec h = cross(r, v);
        const double hm = length(h);
        if (hm < 1e-9 || rm < 1 || mu <= 0)
            return std::nullopt;
        Orbit o;
        o.mu = mu;
        o.W = h * (1 / hm);
        const Vec ev = (r * (v2 - mu / rm) - v * dot(r, v)) * (1 / mu);
        o.e = length(ev);
        o.P = o.e > 1e-7 ? ev * (1 / o.e) : unit(r);
        o.Q = cross(o.W, o.P);
        o.p = hm * hm / mu;
        o.bound = o.e < 1;
        o.nu = std::atan2(dot(r, o.Q), dot(r, o.P));
        o.rp = o.p / (1 + o.e);
        if (o.e < 1)
        {
            o.a = o.p / (1 - o.e * o.e);
            o.ra = o.p / (1 - o.e);
            o.n = std::sqrt(mu / (o.a * o.a * o.a));
            o.period = TAU / o.n;
        }
        else if (o.e > 1)
        {
            o.a = o.p / (1 - o.e * o.e);
            o.n = std::sqrt(mu / std::pow(-o.a, 3));
        }
        return o;
    }

    Orbit orbit_from_elements(double mu, double a, double e, double i, double raan, double argp, double nu)
    {
        Orbit o;
        o.mu = mu;
        o.a = a;
        o.e = e;
        o.bound = e < 1;
        o.p = a * (1 - e * e);
        const double co = std::cos(raan), so = std::sin(raan), ci = std::cos(i), si = std::sin(i), cw = std::cos(argp), sw = std::sin(argp);
        o.P = {co * cw - so * sw * ci, so * cw + co * sw * ci, sw * si};
        o.Q = {-co * sw - so * cw * ci, -so * sw + co * cw * ci, cw * si};
        o.W = cross(o.P, o.Q);
        o.nu = nu;
        o.rp = a * (1 - e);
        o.ra = a * (1 + e);
        o.n = std::sqrt(mu / std::pow(std::fabs(a), 3));
        o.period = o.bound ? TAU / o.n : 0;
        return o;
    }

    double mean_from_nu(const Orbit &o, double nu)
    {
        if (o.e < 1)
        {
            const double ea = 2 * std::atan(std::sqrt((1 - o.e) / (1 + o.e)) * std::tan(nu / 2));
            return ea - o.e * std::sin(ea);
        }
        const double f = 2 * std::atanh(clamp(std::sqrt((o.e - 1) / (o.e + 1)) * std::tan(nu / 2), -0.999999999, 0.999999999));
        return o.e * std::sinh(f) - f;
    }

    double nu_from_mean(const Orbit &o, double m)
    {
        if (o.e < 1)
        {
            m = wrap(m + PI, TAU) - PI;
            double ea = o.e > 0.8 ? PI : m + o.e * std::sin(m);
            for (int i = 0; i < 40; ++i)
            {
                const double d = (ea - o.e * std::sin(ea) - m) / (1 - o.e * std::cos(ea));
                ea -= d;
                if (std::fabs(d) < 1e-12)
                    break;
            }
            return 2 * std::atan(std::sqrt((1 + o.e) / (1 - o.e)) * std::tan(ea / 2));
        }
        double f = std::asinh(m / o.e);
        for (int i = 0; i < 60; ++i)
        {
            const double d = (o.e * std::sinh(f) - f - m) / (o.e * std::cosh(f) - 1);
            f -= d;
            if (std::fabs(d) < 1e-12)
                break;
        }
        return 2 * std::atan(std::sqrt((o.e + 1) / (o.e - 1)) * std::tanh(f / 2));
    }

    double advance(const Orbit &o, double nu, double dt) { return nu_from_mean(o, mean_from_nu(o, nu) + o.n * dt); }

    void state_at(const Orbit &o, double nu, Vec &r, Vec &v)
    {
        const double c = std::cos(nu), s = std::sin(nu);
        const double rad = o.p / (1 + o.e * c);
        const double k = std::sqrt(o.mu / o.p);
        r = o.P * (rad * c) + o.Q * (rad * s);
        v = o.P * (-k * s) + o.Q * (k * (o.e + c));
    }

    double time_to(const Orbit &o, double nu_from, double nu_to)
    {
        const double dm = mean_from_nu(o, nu_to) - mean_from_nu(o, nu_from);
        if (o.e < 1)
            return wrap(dm, TAU) / o.n;
        if (dm < -1e-9)
            return -1;
        return std::max(dm, 0.0) / o.n;
    }

    std::optional<Vec> lambert(Vec r1, Vec r2, double tof, double mu, const Vec *normal, Vec *arrival)
    {
        const double r1m = length(r1), r2m = length(r2);
        const double cosine = clamp(dot(r1, r2) / (r1m * r2m), -1, 1);
        double angle = std::acos(cosine);
        if (normal && dot(cross(r1, r2), *normal) < 0)
            angle = TAU - angle;
        const double sine = std::sin(angle);
        if (std::fabs(sine) < 1e-8)
            return std::nullopt;
        const double A = sine * std::sqrt(r1m * r2m / (1 - cosine));
        if (std::fabs(A) < 1e-8)
            return std::nullopt;

        const auto stumpff = [](double z, double &C, double &S) {
            if (std::fabs(z) < 1e-8)
            {
                C = 0.5;
                S = 1.0 / 6.0;
            }
            else if (z > 0)
            {
                const double q = std::sqrt(z);
                C = (1 - std::cos(q)) / z;
                S = (q - std::sin(q)) / (q * q * q);
            }
            else
            {
                const double q = std::sqrt(-z);
                C = (std::cosh(q) - 1) / -z;
                S = (std::sinh(q) - q) / (q * q * q);
            }
        };
        const auto flight_time = [&](double z, double &y) {
            double C, S;
            stumpff(z, C, S);
            if (C <= 0 || C != C)
            {
                y = 0;
                return HUGE_VAL;
            }
            y = r1m + r2m + A * (z * S - 1) / std::sqrt(C);
            if (y < 0 || y != y)
                return -1.0;
            return (std::pow(y / C, 1.5) * S + A * std::sqrt(y)) / std::sqrt(mu);
        };

        double lo = -4 * PI * PI + 1e-6, hi = 4 * PI * PI - 1e-6, y = 0;
        if (flight_time(hi, y) < tof)
            return std::nullopt;
        for (int i = 0; i < 80; ++i)
        {
            const double mid = (lo + hi) / 2;
            if (flight_time(mid, y) < tof)
                lo = mid;
            else
                hi = mid;
        }
        flight_time((lo + hi) / 2, y);
        if (y <= 0 || y != y)
            return std::nullopt;
        const double g = A * std::sqrt(y / mu);
        if (g == 0 || g != g)
            return std::nullopt;
        const Vec v1 = (r2 - r1 * (1 - y / r1m)) * (1 / g);
        if (!std::isfinite(v1.x) || !std::isfinite(v1.y) || !std::isfinite(v1.z))
            return std::nullopt;
        if (arrival)
            *arrival = (r2 * (1 - y / r2m) - r1) * (1 / g);
        return v1;
    }
} // namespace km
