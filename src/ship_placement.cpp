#include "ship.h"
#include "ship_internal.h"

#include <algorithm>
#include <cmath>

namespace kosmos
{
    using namespace ship_detail;

    void Ship::place_on_surface(int body_id, const sl::dvec3 &direction, double heading_degrees, double ground_height)
    {
        const Body &planet = universe_.bodies[static_cast<std::size_t>(body_id)];
        const sl::FlightBody &pb = *universe_.world.body(body_id);
        sl::FlightBody &b = body();
        const sl::dvec3 up = glm::normalize(direction);
        const sl::dvec3 east = glm::normalize(glm::cross(pb.spin_axis, up));
        const sl::dvec3 north = glm::cross(up, east);
        const bool level = spec_.aircraft() || spec_.horizontal;
        // A rocket stands with its roof to the west, so pitching up (nose toward the roof) turns it east, with the body's spin;
        // `pad_turn` swings it round from there.
        const double heading = (level ? heading_degrees : 270.0 + spec_.pad_turn) * pi / 180.0;
        const sl::dvec3 roof = north * std::cos(heading) + east * std::sin(heading);

        double lowest = 0.0;
        if (level)
        {
            for (const sl::GearContact &g : b.gear)
                lowest = std::max(lowest, -g.position.y);
        }
        else
            for (const StageSpec &st : spec_.stages)
            {
                lowest = std::max(lowest, st.z + st.length * 0.5);
                for (const sl::dvec3 &g : st.gear)
                    lowest = std::max(lowest, g.z);
            }
        // Gear that carries the whole ship compresses about 0.3 m, unless that is too soft to stop a tall, narrow
        // ship toppling: the springs' restoring torque must beat gravity's by a safe margin.
        const double surface_gravity = planet.gm / (planet.radius * planet.radius);
        const int gear_count = static_cast<int>(b.gear.size());
        double compression = 0.0;
        if (gear_count > 0)
        {
            const double mass = b.mass();
            double height = 0.0, spread = 0.0;
            for (const sl::GearContact &g : b.gear)
            {
                height += std::fabs(g.position.z) / gear_count;
                spread += (g.position.x * g.position.x + g.position.y * g.position.y) / gear_count;
            }
            const double rest_compression = spec_.gear_rest_compression > 0.0 ? spec_.gear_rest_compression :
                                            (spec_.aircraft() || spec_.ground() ? 0.12 : 0.3);
            gear_k_ = mass * surface_gravity / (rest_compression * gear_count);
            if (spread > 1e-6 && !level)
                gear_k_ = std::max(gear_k_, 6.0 * mass * surface_gravity * height / (gear_count * spread));
            compression = mass * surface_gravity / (gear_count * gear_k_);
            const double per_gear_mass = mass / gear_count;
            for (sl::GearContact &g : b.gear)
            {
                g.stiffness = gear_k_;
                g.damping = 1.2 * std::sqrt(gear_k_ * per_gear_mass);
            }
            if (spec_.gear_rest_compression > 0.0)
                gear_sized_body_ = body_id;
        }
        b.position = pb.position + up * (planet.radius + ground_height + lowest - compression);
        b.velocity = pb.velocity + glm::cross(pb.spin_axis * planet.spin, b.position - pb.position);
        b.orientation = orientation_from(level ? roof : up, level ? up : -roof); // rockets lie heads-down, so the roof ends up skyward as the ascent pitches over
        b.angular_velocity = sl::dvec3(0.0);
        crashed_ = false;
        health_ = 1.0;
        rcs_fuel_ = spec_.rcs_propellant;
        shield_ = spec_.shield.capacity;
        pinned_ = resting_ = false;
        update_telemetry();
    }

    void Ship::place_airborne(int body_id, const sl::dvec3 &direction, double heading_degrees, double ground_height, double altitude, double speed)
    {
        place_on_surface(body_id, direction, heading_degrees, ground_height);
        sl::FlightBody &b = body();
        const sl::dvec3 up = glm::normalize(direction);
        b.position += up * altitude;
        b.velocity += b.direction_to_world({0.0, 0.0, -1.0}) * speed;
        update_telemetry();
    }

    void Ship::place_relative(int body_id, const sl::dvec3 &position, const sl::dvec3 &velocity, const sl::dquat &orientation)
    {
        const sl::FlightBody &pb = *universe_.world.body(body_id);
        sl::FlightBody &b = body();
        b.position = pb.position + position;
        b.velocity = pb.velocity + velocity;
        b.orientation = orientation;
        b.angular_velocity = sl::dvec3(0.0);
        crashed_ = false;
        health_ = 1.0;
        rcs_fuel_ = spec_.rcs_propellant;
        shield_ = spec_.shield.capacity;
        pinned_ = resting_ = false;
        update_telemetry();
    }

    void Ship::place_in_orbit(int body_id, double altitude, double inclination_degrees, double phase_degrees)
    {
        gear_position_ = 0.0;
        const Body &planet = universe_.bodies[static_cast<std::size_t>(body_id)];
        const sl::FlightBody &pb = *universe_.world.body(body_id);
        sl::FlightBody &b = body();
        const double r = planet.radius + altitude;
        const double inc = inclination_degrees * pi / 180.0;
        const double phase = phase_degrees * pi / 180.0;
        const sl::dvec3 up0(1.0, 0.0, 0.0);
        const sl::dvec3 prograde0(0.0, std::sin(inc), -std::cos(inc));
        const sl::dvec3 up = up0 * std::cos(phase) + prograde0 * std::sin(phase);
        const sl::dvec3 prograde = prograde0 * std::cos(phase) - up0 * std::sin(phase);
        b.position = pb.position + up * r;
        b.velocity = pb.velocity + prograde * std::sqrt(planet.gm / r);
        b.orientation = orientation_from(prograde, up);
        b.angular_velocity = sl::dvec3(0.0);
        crashed_ = false;
        health_ = 1.0;
        rcs_fuel_ = spec_.rcs_propellant;
        shield_ = spec_.shield.capacity;
        pinned_ = resting_ = false;
        update_telemetry();
    }

    void Ship::place_on_ellipse(int body_id, double periapsis, double apoapsis, double true_anomaly_degrees, int toward)
    {
        const Body &planet = universe_.bodies[static_cast<std::size_t>(body_id)];
        const sl::FlightBody &pb = *universe_.world.body(body_id);
        sl::FlightBody &b = body();
        const double rp = planet.radius + periapsis, ra = planet.radius + std::max(apoapsis, periapsis);
        const double a = 0.5 * (rp + ra), e = (ra - rp) / (ra + rp), mu = planet.gm;
        const double nu = true_anomaly_degrees * pi / 180.0;
        const double p = a * (1.0 - e * e);

        sl::dvec3 normal(0.0, 1.0, 0.0), apse(-1.0, 0.0, 0.0); // normal of the orbit plane, direction of periapsis
        if (toward >= 0 && toward != body_id)
        {
            const sl::FlightBody &tb = *universe_.world.body(toward);
            const sl::dvec3 m = tb.position - pb.position, vm = tb.velocity - pb.velocity;
            normal = glm::normalize(glm::cross(m, vm));
            // Time from the starting anomaly to apoapsis, by Kepler's equation; the target moves on meanwhile.
            const double E = 2.0 * std::atan2(std::sqrt(1.0 - e) * std::sin(0.5 * nu), std::sqrt(1.0 + e) * std::cos(0.5 * nu));
            const double M = E - e * std::sin(E);
            double M_now = std::fmod(M + 2.0 * pi, 2.0 * pi);
            const double n = std::sqrt(mu / (a * a * a));
            const double flight = (pi - M_now) / n;
            const double omega = glm::length(vm) / glm::length(m);
            const sl::dvec3 ahead = m * std::cos(omega * flight) + glm::cross(normal, m) * std::sin(omega * flight);
            apse = -glm::normalize(ahead);
        }
        else
            normal = glm::normalize(normal);
        const sl::dvec3 side = glm::cross(normal, apse); // 90 degrees ahead of periapsis
        const double r = p / (1.0 + e * std::cos(nu));
        const sl::dvec3 up = apse * std::cos(nu) + side * std::sin(nu);
        const sl::dvec3 prograde = side * std::cos(nu) - apse * std::sin(nu);
        const double h = std::sqrt(mu * p);
        const double vr = mu / h * e * std::sin(nu), vt = mu / h * (1.0 + e * std::cos(nu));
        b.position = pb.position + up * r;
        b.velocity = pb.velocity + up * vr + prograde * vt;
        b.orientation = orientation_from(prograde, up);
        b.angular_velocity = sl::dvec3(0.0);
        crashed_ = false;
        health_ = 1.0;
        rcs_fuel_ = spec_.rcs_propellant;
        shield_ = spec_.shield.capacity;
        pinned_ = resting_ = false;
        update_telemetry();
    }

    void Ship::place_on_escape(int body_id, double periapsis, double true_anomaly_degrees, int toward, double flight_days)
    {
        gear_position_ = 0.0;
        const Body &planet = universe_.bodies[static_cast<std::size_t>(body_id)];
        const sl::FlightBody &pb = *universe_.world.body(body_id);
        sl::FlightBody &b = body();
        const int star_id = planet.parent;
        if (star_id < 0 || toward < 0 || toward == body_id)
        {
            place_in_orbit(body_id, periapsis, 0.0, true_anomaly_degrees);
            return;
        }
        const sl::FlightBody &sb = *universe_.world.body(star_id);
        const sl::FlightBody &tb = *universe_.world.body(toward);
        const double mu_sun = universe_.bodies[static_cast<std::size_t>(star_id)].gm;
        const sl::dvec3 r_planet = pb.position - sb.position, v_planet = pb.velocity - sb.velocity;
        const double r1 = glm::length(r_planet), r2 = glm::length(tb.position - sb.position);
        sl::dvec3 normal = glm::normalize(glm::cross(r_planet, v_planet));
        const double signed_v_inf = std::sqrt(mu_sun / r1) * (std::sqrt(2.0 * r2 / (r1 + r2)) - 1.0);
        double v_inf = std::fabs(signed_v_inf);
        sl::dvec3 u = glm::normalize(v_planet) * (signed_v_inf < 0.0 ? -1.0 : 1.0);
        const double mu = planet.gm, rp = planet.radius + periapsis;
        sl::OrbitElements target_orbit, home_orbit;
        if (flight_days > 0.0 && universe_.world.orbit_of(toward, star_id, target_orbit) && universe_.world.orbit_of(body_id, star_id, home_orbit))
        {
            // Lambert's problem by universal variables: the conic from r1 to r2 in `tof`, the short way round about `normal`.
            const sl::dvec3 base_normal = normal;
            auto lambert = [&](const sl::dvec3 &from, const sl::dvec3 &to, double tof, sl::dvec3 &v_from) {
                const double a = glm::length(from), c = glm::length(to);
                double dtheta = std::acos(std::clamp(glm::dot(from, to) / (a * c), -1.0, 1.0));
                if (glm::dot(glm::cross(from, to), base_normal) < 0.0)
                    dtheta = 2.0 * pi - dtheta;
                const double A = std::sin(dtheta) * std::sqrt(a * c / (1.0 - std::cos(dtheta)));
                auto stumpff_c = [](double z) { return std::fabs(z) < 1e-8 ? 0.5 : (z > 0.0 ? (1.0 - std::cos(std::sqrt(z))) / z : (std::cosh(std::sqrt(-z)) - 1.0) / -z); };
                auto stumpff_s = [](double z) {
                    if (std::fabs(z) < 1e-8)
                        return 1.0 / 6.0;
                    return z > 0.0 ? (std::sqrt(z) - std::sin(std::sqrt(z))) / std::pow(z, 1.5) : (std::sinh(std::sqrt(-z)) - std::sqrt(-z)) / std::pow(-z, 1.5);
                };
                auto flight_time = [&](double z, double &y) {
                    const double C = stumpff_c(z), S = stumpff_s(z);
                    y = a + c + A * (z * S - 1.0) / std::sqrt(C);
                    return y < 0.0 ? -1.0 : (std::pow(y / C, 1.5) * S + A * std::sqrt(y)) / std::sqrt(mu_sun);
                };
                double lo = -4.0 * pi * pi, hi = 4.0 * pi * pi, y = 0.0;
                for (int i = 0; i < 200; ++i)
                {
                    const double mid = 0.5 * (lo + hi);
                    if (flight_time(mid, y) < tof)
                        lo = mid;
                    else
                        hi = mid;
                }
                flight_time(0.5 * (lo + hi), y);
                v_from = (to - (1.0 - y / a) * from) / (A * std::sqrt(y / mu_sun));
            };

            const double tof = flight_days * 86400.0;
            const sl::dvec3 r_arrive = sl::state_from_orbit(mu_sun, target_orbit, tof).position;
            const double soi_radius = planet.soi < 1.0e29 ? planet.soi : 0.0;
            sl::dvec3 v_depart;
            lambert(r_planet, r_arrive, tof, v_depart);
            sl::dvec3 excess = v_depart - v_planet;
            // The transfer must leave the planet's sphere of influence rather than its centre, so aim from the exit point and time, and repeat as the asymptote settles.
            for (int pass = 0; pass < 8 && soi_radius > 0.0 && glm::length(excess) > 1.0; ++pass)
            {
                const double speed_exit2 = glm::dot(excess, excess);
                const double vinf2 = speed_exit2 - 2.0 * mu / soi_radius;
                if (vinf2 <= 0.0)
                    break;
                const double vi = std::sqrt(vinf2), ecc = 1.0 + rp * vinf2 / mu, sma = mu / vinf2;
                const sl::dvec3 asym = excess / glm::length(excess);
                const sl::dvec3 across = base_normal - asym * glm::dot(asym, base_normal);
                if (glm::length(across) < 1e-6)
                    break;
                const sl::dvec3 nrm = glm::normalize(across);
                const double alpha = std::acos(-1.0 / ecc);
                const sl::dvec3 apse = asym * std::cos(alpha) - glm::cross(nrm, asym) * std::sin(alpha);
                const double nu_exit = std::acos(std::clamp(((rp * (1.0 + ecc)) / soi_radius - 1.0) / ecc, -1.0, 1.0));
                const double hyper = std::acosh((1.0 + soi_radius / sma) / ecc);
                const double t_exit = std::sqrt(sma * sma * sma / mu) * (ecc * std::sinh(hyper) - hyper);
                if (t_exit >= tof)
                    break;
                const sl::dvec3 exit_dir = apse * std::cos(nu_exit) + glm::cross(nrm, apse) * std::sin(nu_exit);
                const sl::OrbitState home = sl::state_from_orbit(mu_sun, home_orbit, t_exit);
                lambert(home.position + exit_dir * soi_radius, r_arrive, tof - t_exit, v_depart);
                const sl::dvec3 next = v_depart - home.velocity;
                if (!std::isfinite(next.x + next.y + next.z))
                    break;
                excess = next;
                (void)vi;
            }
            if (std::isfinite(excess.x + excess.y + excess.z) && glm::length(excess) > 1.0)
            {
                const double v2 = glm::dot(excess, excess) - (soi_radius > 0.0 ? 2.0 * mu / soi_radius : 0.0);
                v_inf = std::sqrt(std::max(v2, 1.0));
                u = glm::normalize(excess);
                // The hyperbola's plane holds the asymptote, tilted as little as it can from the planet's own orbit plane.
                const sl::dvec3 across = normal - u * glm::dot(u, normal);
                if (glm::length(across) > 1e-6)
                    normal = glm::normalize(across);
            }
        }

        const double e = 1.0 + rp * v_inf * v_inf / mu;
        // The outgoing asymptote lies at true anomaly acos(-1/e) from periapsis, so periapsis is the asymptote turned back by that angle.
        const double alpha = std::acos(-1.0 / e);
        const sl::dvec3 apse = u * std::cos(alpha) - glm::cross(normal, u) * std::sin(alpha);
        const sl::dvec3 side = glm::cross(normal, apse);
        const double nu = true_anomaly_degrees * pi / 180.0;
        const double p = rp * (1.0 + e);
        const double r = p / (1.0 + e * std::cos(nu));
        const sl::dvec3 up = apse * std::cos(nu) + side * std::sin(nu);
        const sl::dvec3 prograde = side * std::cos(nu) - apse * std::sin(nu);
        const double h = std::sqrt(mu * p);
        const double vr = mu / h * e * std::sin(nu), vt = mu / h * (1.0 + e * std::cos(nu));
        b.position = pb.position + up * r;
        b.velocity = pb.velocity + up * vr + prograde * vt;
        b.orientation = orientation_from(prograde, up);
        b.angular_velocity = sl::dvec3(0.0);
        crashed_ = false;
        health_ = 1.0;
        rcs_fuel_ = spec_.rcs_propellant;
        shield_ = spec_.shield.capacity;
        pinned_ = resting_ = false;
        update_telemetry();
    }
} // namespace kosmos
