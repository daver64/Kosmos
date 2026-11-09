#include "ship.h"
#include "ship_internal.h"

#include <algorithm>
#include <cmath>

namespace kosmos
{
    using namespace ship_detail;

    bool Ship::deploy_parachute()
    {
        if (chutes_.size() != spec_.parachutes.size())
            chutes_.assign(spec_.parachutes.size(), ChuteRuntime());
        for (ChuteRuntime &c : chutes_)
            if (c.state == ChuteState::stowed)
            {
                c.state = ChuteState::reefed;
                c.time = 0.0;
                std::fprintf(stderr, "[kosmos] %s: parachute deployed\n", name.c_str());
                return true;
            }
        return false;
    }

    void Ship::cut_parachutes()
    {
        for (ChuteRuntime &c : chutes_)
            if (c.state != ChuteState::stowed && c.state != ChuteState::gone)
                c.state = ChuteState::gone;
    }

    void Ship::step_parachutes(double dt, const sl::dvec3 &air, double rho)
    {
        if (spec_.parachutes.empty())
            return;
        if (chutes_.size() != spec_.parachutes.size())
            chutes_.assign(spec_.parachutes.size(), ChuteRuntime());
        sl::FlightBody &b = body();
        const double speed = glm::length(air);
        const double q = 0.5 * rho * speed * speed;
        for (std::size_t i = 0; i < chutes_.size(); ++i)
        {
            const ParachuteSpec &spec = spec_.parachutes[i];
            ChuteRuntime &c = chutes_[i];
            if (telemetry_.altitude > spec.arm_altitude)
                chutes_armed_ = true;
            if (chutes_armed_ && c.state == ChuteState::stowed && spec.auto_altitude > 0.0 && telemetry_.altitude < spec.auto_altitude &&
                (spec.auto_speed <= 0.0 || speed < spec.auto_speed))
                deploy_parachute();
            if (c.state == ChuteState::stowed || c.state == ChuteState::gone)
                continue;
            if (q > spec.max_q * (c.state == ChuteState::reefed ? 1.5 : 1.0))
            {
                c.state = ChuteState::gone;
                std::fprintf(stderr, "[kosmos] %s: %s torn away at %.0f Pa\n", name.c_str(), spec.name.c_str(), q);
                continue;
            }
            if (spec.cut_on_landing && (telemetry_.landed || telemetry_.altitude < 1.0))
            {
                c.state = ChuteState::gone;
                continue;
            }
            c.time += dt;
            if (c.state == ChuteState::reefed && c.time >= spec.reef_time)
            {
                c.state = ChuteState::opening;
                c.fill = 0.0;
            }
            if (c.state == ChuteState::opening)
            {
                c.fill += dt / std::max(spec.inflation_time, 0.1);
                if (c.fill >= 1.0)
                    c.state = ChuteState::open;
            }
            const double area = chute_drag_area(spec, c);
            if (area <= 0.0 || speed < 0.5)
                continue;
            const sl::dvec3 pull = -air / speed * (q * area);
            // The pull acts through the centre of mass; the canopy's own pendulum stability is a
            // damped torque that swings the attachment point toward the upwind direction.
            universe_.world.apply_force(body_, pull);
            const sl::dvec3 lever = spec.position - com_;
            if (glm::length(lever) > 1e-3)
            {
                const sl::dvec3 attach = glm::normalize(lever);
                const sl::dvec3 upwind = b.direction_to_local(-air / speed);
                const double w0 = 1.2, zeta = 1.0;
                const sl::dvec3 accel = w0 * w0 * glm::cross(attach, upwind) - 2.0 * zeta * w0 * b.angular_velocity;
                universe_.world.apply_torque(body_, b.direction_to_world(accel * b.inertia()));
            }
        }
    }

    void Ship::step_reentry(double dt)
    {
        const bool wanted = spec_.thermal.enabled || spec_.hypersonic.enabled || !spec_.parachutes.empty();
        if (!wanted || dt <= 0.0 || crashed_ || pinned_ || on_rails() || telemetry_.soi < 0)
            return;
        sl::FlightBody &b = body();
        if (b.mode != sl::FlightBodyMode::dynamic)
            return;
        const double rho = universe_.world.air_density_at(b.position);
        const sl::dvec3 air = b.velocity - universe_.world.air_velocity_at(b.position);
        const double speed = glm::length(air);
        const sl::dvec3 local = b.direction_to_local(air);

        if (spec_.hypersonic.enabled)
        {
            if (!aero_scaled_)
            {
                aero_scaled_ = true;
                base_drag_area_ = b.drag_area;
                base_surface_area_.clear();
                for (const sl::LiftingSurface &s : b.surfaces)
                    base_surface_area_.push_back(s.area);
            }
            const AeroResult aero = hypersonic_aero(spec_.hypersonic, local, rho, b.angular_velocity, std::max({b.inertia().x, b.inertia().y, b.inertia().z}));
            b.drag_area = base_drag_area_ * (1.0 - aero.blend);
            for (std::size_t i = 0; i < b.surfaces.size() && i < base_surface_area_.size(); ++i)
                b.surfaces[i].area = base_surface_area_[i] * (1.0 - aero.blend);
            // Under power the pilot holds the attitude; the trim moment would throw an ascending ship nose-up.
            const bool powered = telemetry_.thrust > 0.0 && !spec_.hypersonic.hold_low_speed;
            if (aero.blend > 0.0 || glm::length(aero.torque) > 0.0)
            {
                universe_.world.apply_force(body_, b.direction_to_world(aero.force));
                if (!powered)
                    universe_.world.apply_torque(body_, b.direction_to_world(aero.torque));
            }
        }
        if (spec_.thermal.enabled)
        {
            const double windward = speed > 1.0 ? glm::dot(spec_.thermal.shield_axis, local / speed) : 1.0;
            const double rate = step_thermal(spec_.thermal, thermal_, rho, speed, windward, 288.0, dt);
            if (rate > 0.0)
                damage(rate * dt);
        }
        step_parachutes(dt, air, rho);
    }

    void Ship::update_ground_track()
    {
        const Telemetry &t = telemetry_;
        const double now = universe_.time();
        if (t.soi == track_.soi && std::fabs(now - track_.stamp) < 2.0)
            return;
        track_.stamp = now;
        track_.soi = t.soi;
        track_.count = 0;
        track_.impact = false;
        const Body &planet = universe_.bodies[static_cast<std::size_t>(t.soi)];
        const sl::FlightBody &pb = *universe_.world.body(t.soi);
        const sl::FlightBody &b = body();
        if (planet.gm <= 0.0)
            return;
        sl::dvec3 r = b.position - pb.position, v = b.velocity - pb.velocity;
        const double mu = planet.gm;
        const double rmag0 = glm::length(r);
        const double energy = 0.5 * glm::dot(v, v) - mu / rmag0;
        // One orbit when bound, otherwise a stretch of the escape trajectory.
        double span = energy < 0.0 ? 2.0 * pi * std::sqrt(std::pow(-mu / (2.0 * energy), 3.0) / mu) : 2.0 * 3600.0;
        span = std::min(span, 40.0 * 86400.0);
        const double step = std::min(span / 2000.0, 0.02 * std::sqrt(rmag0 * rmag0 * rmag0 / mu));
        const sl::dquat tilt_inv = glm::inverse(planet.axis_tilt);
        const double angle0 = universe_.rotation_angle(t.soi);
        const auto accel = [&](const sl::dvec3 &p) { return p * (-mu / std::pow(glm::length(p), 3.0)); };
        const auto record = [&](const sl::dvec3 &p, double dt, double &lat, double &lon)
        {
            const sl::dvec3 local = glm::inverse(glm::angleAxis(angle0 + planet.spin * dt, sl::dvec3(0.0, 1.0, 0.0))) * (tilt_inv * p);
            lat = std::asin(std::clamp(local.y / glm::length(local), -1.0, 1.0)) * 180.0 / pi;
            lon = std::atan2(-local.z, local.x) * 180.0 / pi;
        };
        record(r, 0.0, track_.lat[0], track_.lon[0]);
        track_.count = 1;
        const double sample = span / track_points;
        double elapsed = 0.0, next = sample;
        const long max_steps = 4000;
        for (long n = 0; n < max_steps && track_.count <= track_points; ++n)
        {
            const sl::dvec3 k1v = accel(r), k1r = v;
            const sl::dvec3 k2v = accel(r + k1r * (step * 0.5)), k2r = v + k1v * (step * 0.5);
            const sl::dvec3 k3v = accel(r + k2r * (step * 0.5)), k3r = v + k2v * (step * 0.5);
            const sl::dvec3 k4v = accel(r + k3r * step), k4r = v + k3v * step;
            r += (k1r + 2.0 * k2r + 2.0 * k3r + k4r) * (step / 6.0);
            v += (k1v + 2.0 * k2v + 2.0 * k3v + k4v) * (step / 6.0);
            elapsed += step;
            if (glm::length(r) <= planet.radius)
            {
                track_.impact = true;
                track_.impact_time = elapsed;
                record(r, elapsed, track_.impact_lat, track_.impact_lon);
                record(r, elapsed, track_.lat[track_.count], track_.lon[track_.count]);
                ++track_.count;
                break;
            }
            if (elapsed >= next)
            {
                record(r, elapsed, track_.lat[track_.count], track_.lon[track_.count]);
                ++track_.count;
                next += sample;
            }
        }
    }

    void Ship::update_telemetry()
    {
        Telemetry &t = telemetry_;
        sl::FlightBody &b = body();
        t.soi = universe_.soi_body(b.position);
        const Body &planet = universe_.bodies[static_cast<std::size_t>(t.soi)];
        const sl::FlightBody &pb = *universe_.world.body(t.soi);

        double lowest = 0.0;
        if (spec_.aircraft() || spec_.horizontal)
        {
            for (const sl::GearContact &g : b.gear)
                lowest = std::max(lowest, -g.position.y);
        }
        else
        {
            for (std::size_t s = static_cast<std::size_t>(active_stage_); s < spec_.stages.size(); ++s)
                lowest = std::max(lowest, spec_.stages[s].z + spec_.stages[s].length * 0.5);
            for (const sl::GearContact &g : b.gear)
                lowest = std::max(lowest, g.position.z);
        }
        const double origin_altitude = universe_.world.altitude_of(b.position);
        t.altitude = origin_altitude - lowest;
        t.landed = false;
        for (const sl::GearContact &g : b.gear)
            t.landed = t.landed || g.compression > 0.0;

        const sl::dvec3 r = b.position - pb.position;
        const sl::dvec3 vrel = relative_velocity();
        const sl::dvec3 up = glm::normalize(r);
        t.surface_speed = glm::length(vrel);
        {
            const sl::dvec3 air = b.velocity - universe_.world.air_velocity_at(b.position);
            const sl::dvec3 local = b.direction_to_local(air);
            const double rho = universe_.world.air_density_at(b.position);
            t.dynamic_pressure = 0.5 * rho * glm::dot(air, air);
            const double speed = glm::length(local);
            t.alpha = t.sideslip = 0.0;
            if (speed > 1.0)
            {
                t.alpha = std::atan2(local.y, -local.z) * 180.0 / pi * -1.0;
                t.sideslip = std::asin(std::clamp(local.x / speed, -1.0, 1.0)) * 180.0 / pi;
            }
        }
        t.orbital_speed = glm::length(b.velocity - pb.velocity);
        t.vertical_speed = glm::dot(vrel, up);
        t.flight_path = t.surface_speed > 1.0 ? std::asin(std::clamp(t.vertical_speed / t.surface_speed, -1.0, 1.0)) * 180.0 / pi : 0.0;

        {
            const sl::dvec3 local_pos = glm::inverse(universe_.orientation(t.soi)) * r;
            t.latitude = std::asin(std::clamp(local_pos.y / glm::length(local_pos), -1.0, 1.0)) * 180.0 / pi;
            t.longitude = std::atan2(-local_pos.z, local_pos.x) * 180.0 / pi;
            t.body_radius = planet.radius;
        }
        update_ground_track();

        sl::OrbitElements el;
        t.bound = false;
        t.apoapsis = t.periapsis = t.period = t.time_to_apoapsis = t.time_to_periapsis = 0.0;
        if (t.soi >= 0 && universe_.world.orbit_of(body_, t.soi, el))
        {
            t.eccentricity = el.eccentricity;
            t.inclination = el.inclination * 180.0 / pi;
            t.semi_major_axis = el.semi_major_axis;
            t.raan = el.raan * 180.0 / pi;
            t.arg_periapsis = el.arg_periapsis * 180.0 / pi;
            {
                // True anomaly from the eccentric (or hyperbolic) state vectors.
                const sl::dvec3 vv = b.velocity - pb.velocity;
                const sl::dvec3 e = (glm::dot(vv, vv) - planet.gm / glm::length(r)) / planet.gm * r - glm::dot(r, vv) / planet.gm * vv;
                double ta = 0.0;
                if (glm::length(e) > 1e-9)
                {
                    ta = std::acos(std::clamp(glm::dot(e, r) / (glm::length(e) * glm::length(r)), -1.0, 1.0));
                    if (glm::dot(r, vv) < 0.0)
                        ta = 2.0 * pi - ta;
                }
                t.true_anomaly = ta * 180.0 / pi;
            }
            t.bound = el.eccentricity < 1.0;
            t.periapsis = el.periapsis() - planet.radius;
            const double n = el.mean_motion(planet.gm);
            double m = std::fmod(el.mean_anomaly, 2.0 * pi);
            if (m < 0.0 && t.bound)
                m += 2.0 * pi;
            if (t.bound)
            {
                t.apoapsis = el.apoapsis() - planet.radius;
                t.period = el.period(planet.gm);
                t.time_to_apoapsis = (m < pi ? pi - m : 3.0 * pi - m) / n;
                t.time_to_periapsis = (2.0 * pi - m) / n;
            }
            else
                t.time_to_periapsis = m < 0.0 ? -m / n : 0.0;
        }

        const sl::dvec3 nose = b.direction_to_world({0.0, 0.0, -1.0});
        sl::dvec3 u, east, north;
        local_frame(u, east, north);
        t.pitch = std::asin(std::clamp(glm::dot(nose, u), -1.0, 1.0)) * 180.0 / pi;
        {
            const sl::dvec3 right = b.direction_to_world({1.0, 0.0, 0.0}), roof = b.direction_to_world({0.0, 1.0, 0.0});
            t.roll = std::atan2(-glm::dot(right, u), glm::dot(roof, u)) * 180.0 / pi;
        }
        const sl::dvec3 flat = nose - u * glm::dot(nose, u);
        if (glm::length(flat) > 0.01)
            t.heading = std::fmod(std::atan2(glm::dot(flat, east), glm::dot(flat, north)) * 180.0 / pi + 360.0, 360.0);
        if (t.heading >= 359.5)
            t.heading = 0.0;

        t.mass = b.mass();
        t.thrust = 0.0;
        for (const sl::Thruster &th : b.thrusters)
            if (th.level > 0.0 && th.tank >= 0 && b.tanks[static_cast<std::size_t>(th.tank)].mass > 0.0)
                t.thrust += th.level * th.max_thrust;
        const double g_local = planet.gm / std::max(glm::dot(r, r), 1.0);
        t.max_thrust = 0.0;
        if (!b.thrusters.empty())
            for (int i = stage_first_thruster_[static_cast<std::size_t>(active_stage_)]; i < stage_first_thruster_[static_cast<std::size_t>(burning_stage_end())]; ++i)
                t.max_thrust += b.thrusters[static_cast<std::size_t>(i)].max_thrust;
        t.twr = b.mass() > 0.0 ? t.max_thrust / (b.mass() * g_local) : 0.0;
        t.acceleration_g = t.thrust / (b.mass() * g0);
        t.stage_fuel = b.tanks[static_cast<std::size_t>(active_stage_)].mass;
        fuel_flow_ = 0.0;
        for (const sl::Thruster &th : b.thrusters)
            if (th.level > 0.0 && th.tank == active_stage_ && th.isp > 0.0 && b.tanks[static_cast<std::size_t>(th.tank)].mass > 0.0)
                fuel_flow_ += th.level * th.max_thrust / (th.isp * g0);
        t.total_fuel = 0.0;
        for (const sl::PropellantTank &tank : b.tanks)
            t.total_fuel += tank.mass;
    }
} // namespace kosmos
