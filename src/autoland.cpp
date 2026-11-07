#include "sim.h"

#include <algorithm>
#include <cmath>

namespace kosmos
{
    void Sim::cancel_autoland(const std::string &reason)
    {
        if (!props.get_bool("/autopilot/land/armed"))
            return;
        props.set_bool("/autopilot/land/armed", false);
        props.set_string("/autopilot/land/state", reason);
        props.set_string("/autopilot/status", reason);
        props.set_double("/controls/hover", 0.0);
        props.set_string("/controls/sas", "stability");
        log(reason);
    }

    void Sim::run_autoland()
    {
        if (!props.get_bool("/autopilot/land/armed"))
            return;

        auto finish = [&](const std::string &message) {
            cancel_autoland(message);
        };
        if (props.get_bool("/autopilot/enabled") || props.get_bool("/autopilot/burn/armed"))
        {
            finish("Autoland cancelled: another autopilot engaged");
            return;
        }
        const std::string previous_phase = props.get_string("/autopilot/land/state");
        const std::string sas = props.get_string("/controls/sas");
        if ((previous_phase == "Deorbit" || previous_phase == "Entry" || previous_phase == "Braking" ||
             previous_phase == "Target approach") && sas != "attitude")
        {
            finish("Autoland cancelled: manual SAS");
            props.set_string("/controls/sas", sas);
            return;
        }
        Ship *ship = active_ship();
        if (!ship || ship->crashed())
        {
            finish("Autoland aborted: no operational vessel");
            return;
        }
        const Telemetry &t = ship->telemetry();
        if (t.soi < 0 || universe.bodies[static_cast<std::size_t>(t.soi)].name != "Mars")
        {
            finish("Autoland aborted: only Mars is supported");
            return;
        }
        if (ship->flies_as_aircraft() || ship->spec().horizontal || ship->body().gear.empty() ||
            ship->active_stage() >= static_cast<int>(ship->spec().stages.size()))
        {
            finish("Autoland aborted: unsupported vessel");
            return;
        }
        if (!ship->attitude_control_available() || (ship->spec().rcs_modes && props.get_string("/controls/rcs-mode", "rot") == "off"))
        {
            finish("Autoland aborted: attitude control unavailable");
            return;
        }
        if (props.get_int("/ships/ship[" + std::to_string(active_index()) + "]/chutes/open") > 0)
        {
            finish("Autoland aborted: parachute open");
            return;
        }
        if (nav.docked(active_index()))
        {
            finish("Autoland aborted: undock first");
            return;
        }
        const auto claim = link_claims.find(active_index());
        if (claim != link_claims.end() && !claim->second.passthrough)
        {
            finish("Autoland aborted: vessel controlled by an addon");
            return;
        }
        const StageSpec &stage = ship->spec().stages[static_cast<std::size_t>(ship->active_stage())];
        double thrust = 0.0;
        for (const EngineSpec &engine : stage.engines)
        {
            if (!engine.hover)
                continue;
            if (glm::length(engine.direction) < 1.0e-8 || glm::dot(glm::normalize(engine.direction), sl::dvec3(0, 0, -1)) < 0.999 ||
                engine.air_breathing || engine.type == EngineType::solid || engine.type == EngineType::hyperjump)
            {
                finish("Autoland aborted: hover engines must be throttleable rockets pointing along the nose");
                return;
            }
            thrust += engine.thrust;
        }
        const sl::FlightBody &planet = *universe.world.body(t.soi);
        const sl::dvec3 r = ship->body().position - planet.position;
        const double radius = glm::length(r);
        const sl::dvec3 up = r / radius;
        const double gravity = universe.bodies[static_cast<std::size_t>(t.soi)].gm / (radius * radius);
        if (t.landed && t.surface_speed < 2.0)
        {
            if (previous_phase != "Deorbit" && previous_phase != "Entry" && previous_phase != "Braking" &&
                previous_phase != "Target approach" && props.get_bool("/autopilot/land/targeted"))
                finish("Autoland aborted: target landing must start airborne");
            else
                finish(previous_phase == "Target approach" && props.get_double("/autopilot/land/distance") > 50.0 ?
                           "Autoland: landed away from target" : "Autoland: landed");
            return;
        }
        if (t.mass <= 0.0 || thrust <= t.mass * gravity * 1.1 || t.stage_fuel < 1.0)
        {
            finish("Autoland aborted: insufficient hover thrust or fuel");
            return;
        }
        props.set_double("/controls/throttle", 0.0);
        props.set_double("/controls/gear", 1.0);
        if (ship->spec().name == "Ares Spider")
            props.set_double("/controls/anim/legs", 0.0);
        if (props.get_int("/sim/warp-index") > 3)
            props.set_int("/sim/warp-index", 3);

        const sl::dvec3 velocity = ship->relative_velocity();
        const sl::dvec3 horizontal = velocity - up * glm::dot(velocity, up);
        const double hspeed = glm::length(horizontal);
        std::string phase = props.get_string("/autopilot/land/state");
        if (phase != "Deorbit" && phase != "Entry" && phase != "Braking" && phase != "Target approach")
        {
            landing_targeted_ = props.get_bool("/autopilot/land/targeted");
            if (landing_targeted_)
            {
                landing_latitude_ = props.get_double("/autopilot/land/latitude");
                landing_longitude_ = props.get_double("/autopilot/land/longitude");
                if (!std::isfinite(landing_latitude_) || !std::isfinite(landing_longitude_) ||
                    std::abs(landing_latitude_) > 90.0 || std::abs(landing_longitude_) > 180.0)
                {
                    finish("Autoland aborted: invalid target coordinates");
                    return;
                }
                const sl::dvec3 target = surface_direction(universe, t.soi, landing_latitude_, landing_longitude_);
                const double distance = radius * std::acos(std::clamp(glm::dot(up, target), -1.0, 1.0));
                if (distance > 2000.0 || t.altitude < 300.0 || t.altitude > 3000.0 ||
                    hspeed > 50.0 || std::abs(t.vertical_speed) > 30.0)
                {
                    finish("Autoland aborted: target requires 300-3000 m altitude, within 2 km, horizontal speed below 50 m/s and vertical speed below 30 m/s");
                    return;
                }
                phase = "Target approach";
            }
            else if (!t.bound || t.altitude < 50000.0)
            {
                finish("Autoland aborted: start from a bound Mars orbit above 50 km");
                return;
            }
            else
                phase = t.periapsis > -20000.0 ? "Deorbit" : "Entry";
        }
        sl::dvec3 direction = up;
        double level = 0.0;
        const double accel = thrust / t.mass;
        if (phase == "Deorbit")
        {
            if (t.periapsis <= -20000.0)
                phase = "Entry";
            else
            {
                const sl::dvec3 orbital = ship->body().velocity - planet.velocity;
                direction = -glm::normalize(orbital);
                level = std::clamp((t.periapsis + 20000.0) / 30000.0, 0.05, 1.0);
            }
        }
        if (phase == "Entry")
        {
            const double vertical_stop = t.vertical_speed * t.vertical_speed / (2.0 * std::max(accel - gravity, 0.1));
            if (t.altitude < 30000.0 || (t.vertical_speed < 0.0 && t.altitude < vertical_stop * 2.0 + 3000.0))
                phase = "Braking";
            else
                direction = up;
        }
        if (phase == "Braking")
        {
            const double wanted_vs = -std::clamp(t.altitude * 0.025, 2.0, 350.0);
            const sl::dvec3 horizontal_accel = -horizontal * 0.08;
            const double vertical_accel = gravity + std::clamp((wanted_vs - t.vertical_speed) * 0.25, -gravity * 0.8, accel);
            const sl::dvec3 wanted = horizontal_accel + up * vertical_accel;
            direction = glm::normalize(wanted);
            level = std::clamp(glm::length(wanted) / accel, 0.0, 1.0);
        }
        if (phase == "Target approach")
        {
            const sl::dvec3 target = surface_direction(universe, t.soi, landing_latitude_, landing_longitude_);
            const double angle = std::acos(std::clamp(glm::dot(up, target), -1.0, 1.0));
            const double distance = radius * angle;
            props.set_double("/autopilot/land/distance", distance);
            sl::dvec3 toward = target - up * glm::dot(up, target);
            if (glm::length(toward) > 1e-9)
                toward = glm::normalize(toward);
            const double wanted_speed = std::min(30.0, distance * 0.08);
            sl::dvec3 horizontal_accel = (toward * wanted_speed - horizontal) * 0.12;
            const double max_horizontal = std::min(2.0, std::max(0.0, (accel - gravity) * 0.3));
            if (glm::length(horizontal_accel) > max_horizontal)
                horizontal_accel *= max_horizontal / glm::length(horizontal_accel);
            const double wanted_vs = distance > 30.0 || hspeed > 3.0 ?
                                         std::clamp((500.0 - t.altitude) * 0.03, -5.0, 5.0) :
                                         -std::clamp(t.altitude * 0.025, 2.0, 15.0);
            const double vertical_accel = gravity + std::clamp((wanted_vs - t.vertical_speed) * 0.25, -gravity * 0.8, accel);
            const sl::dvec3 wanted = horizontal_accel + up * vertical_accel;
            direction = glm::normalize(wanted);
            level = std::clamp(glm::length(wanted) / accel, 0.0, 1.0);
        }
        sl::dvec3 east = glm::cross(planet.spin_axis, up);
        if (glm::length(east) < 1.0e-8)
            east = glm::cross(sl::dvec3(1, 0, 0), up);
        east = glm::normalize(east);
        const sl::dvec3 north = glm::cross(up, east);
        const double pitch = glm::degrees(std::asin(std::clamp(glm::dot(direction, up), -1.0, 1.0)));
        const double de = glm::dot(direction, east), dn = glm::dot(direction, north);
        double heading = props.get_double("/controls/target-heading");
        if (std::hypot(de, dn) > 0.01)
            heading = std::fmod(glm::degrees(std::atan2(de, dn)) + 360.0, 360.0);
        if (heading >= 359.5)
            heading = 0.0;
        props.set_string("/controls/sas", "attitude");
        props.set_double("/controls/target-pitch", pitch);
        props.set_double("/controls/target-heading", heading);
        const sl::dvec3 nose = ship->body().direction_to_world(sl::dvec3(0, 0, -1));
        if (glm::dot(nose, direction) < 0.98)
            level = 0.0;
        props.set_double("/controls/hover", level);
        props.set_string("/autopilot/land/state", phase);
        props.set_string("/autopilot/status", "Autoland: " + phase);
    }
}
