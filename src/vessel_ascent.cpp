#include "sim.h"

#include <algorithm>
#include <cmath>

namespace kosmos
{
    namespace
    {
        bool ascent_docking_allowed(Sim &sim, int active)
        {
            if (!sim.nav.docked(active))
                return true;
            std::vector<int> group{active};
            const auto links = sim.nav.dock_links();
            for (std::size_t i = 0; i < group.size(); ++i)
                for (const auto &link : links)
                {
                    const int other = link[0] == group[i] ? link[2] : link[2] == group[i] ? link[0] : -1;
                    if (other < 0 || std::find(group.begin(), group.end(), other) != group.end())
                        continue;
                    if (other >= sim.ship_count())
                        return false;
                    const Ship *ship = sim.ship(other);
                    if (!ship || ship->spec().kind != "payload" || sim.link_claims.count(other) ||
                        std::any_of(ship->spec().stages.begin(), ship->spec().stages.end(),
                                    [](const StageSpec &stage) { return !stage.engines.empty(); }))
                        return false;
                    group.push_back(other);
                }
            return true;
        }
    }

    void Sim::cancel_vessel_ascent(const std::string &reason)
    {
        if (ascent_ship_ < 0)
            return;
        ascent_ship_ = -1;
        ascent_phase_.clear();
        props.set_bool("/autopilot/vessel-ascent/active", false);
        props.set_string("/autopilot/vessel-ascent/state", reason);
        props.set_string("/autopilot/status", reason);
        props.set_double("/controls/throttle", 0.0);
        props.set_double("/controls/hover", 0.0);
        props.set_double("/controls/pitch", 0.0);
        props.set_double("/controls/yaw", 0.0);
        props.set_double("/controls/roll", 0.0);
        if (ascent_profile_ == "Deltaglider")
            props.set_double("/controls/gimbal-pitch", ascent_saved_gimbal_);
        props.set_string("/controls/sas", "stability");
        log(reason);
    }

    void Sim::run_vessel_ascent(double dt)
    {
        const std::string request = props.get_string("/autopilot/vessel-ascent/request");
        if (!request.empty())
        {
            props.set_string("/autopilot/vessel-ascent/request", "");
            if (request == "stop")
                cancel_vessel_ascent("Ascent cancelled");
            else if (ascent_ship_ < 0)
            {
                Ship *ship = active_ship();
                const std::string name = ship ? ship->spec().name : "";
                const bool valid = (request == "Ares" && name == "Ares Spider") ||
                                   (request == "Deltaglider" && (name == "CVEL Deltaglider" || name == "CVEL Deltaglider (Nuclear)")) ||
                                   (request == "Shuttle-A" && (name == "CVEL Shuttle-A" || name == "CVEL Shuttle-A (Nuclear)" ||
                                                               name == "CVEL Shuttle-AQ"));
                const double target = props.get_double("/autopilot/vessel-ascent/target-km");
                const double heading = props.get_double("/autopilot/vessel-ascent/heading");
                std::string error;
                if (!valid || !ship || ship->crashed())
                    error = "wrong vessel";
                else if (ship->telemetry().soi < 0 || ship->telemetry().soi >= static_cast<int>(universe.bodies.size()) ||
                         !universe.world.body(ship->telemetry().soi))
                    error = "no central body";
                else if (request == "Ares" && universe.bodies[static_cast<std::size_t>(ship->telemetry().soi)].name != "Mars")
                    error = "Ares profile requires Mars";
                else if (request != "Ares" && universe.bodies[static_cast<std::size_t>(ship->telemetry().soi)].name != "Earth")
                    error = "profile requires Earth";
                else if (!std::isfinite(target) || target < 150 || target > 1000 || !std::isfinite(heading))
                    error = "invalid target or heading";
                else if (target * 1000.0 < universe.bodies[static_cast<std::size_t>(ship->telemetry().soi)].atmosphere_scale_height * 10.0 + 5000.0)
                    error = "target is inside atmosphere";
                else if (ship->telemetry().altitude > 1000 || ship->telemetry().surface_speed > 20)
                    error = "start near the surface and at rest";
                else if (!ship->attitude_control_available() || (ship->spec().rcs_modes && props.get_string("/controls/rcs-mode", "rot") == "off"))
                    error = "attitude control unavailable";
                else if (ship->telemetry().stage_fuel < 1)
                    error = "insufficient fuel";
                else if (!ascent_docking_allowed(*this, active_))
                    error = "docked vessel or controlled payload";
                else if (link_claims.count(active_))
                    error = "addon controls vessel";
                else if (ship->active_stage() < 0 || ship->active_stage() >= static_cast<int>(ship->spec().stages.size()))
                    error = "no active engine stage";
                else
                {
                    bool main = false, hover = false;
                    double liftoff_thrust = 0.0;
                    for (const EngineSpec &engine : ship->spec().stages[static_cast<std::size_t>(ship->active_stage())].engines)
                    {
                        if (engine.pod || !engine.rcs_groups.empty() || engine.thrust <= 0.0 ||
                            engine.air_breathing || engine.type == EngineType::solid || engine.type == EngineType::hyperjump)
                            continue;
                        if (engine.hover)
                        {
                            hover = true;
                            liftoff_thrust += engine.thrust;
                        }
                        else
                            main = true;
                    }
                    const Body &body = universe.bodies[static_cast<std::size_t>(ship->telemetry().soi)];
                    const double radius = glm::length(ship->body().position - universe.world.body(ship->telemetry().soi)->position);
                    if ((request == "Ares" && !hover) || (request == "Deltaglider" && !main) ||
                        (request == "Shuttle-A" && (!main || !hover)) ||
                        (request != "Deltaglider" && (radius <= 0.0 || liftoff_thrust <= ship->telemetry().mass * body.gm / (radius * radius))))
                        error = "required engine or liftoff thrust unavailable";
                }
                if (!error.empty())
                {
                    props.set_string("/autopilot/vessel-ascent/state", "Not started: " + error);
                    log("Ascent not started: " + error);
                    return;
                }
                cancel_autoland("Autoland cancelled: ascent started");
                cancel_timed_burn("Timed burn cancelled: ascent started");
                props.set_bool("/autopilot/enabled", false);
                props.set_bool("/autopilot/burn/armed", false);
                props.set_double("/controls/throttle", 0.0);
                props.set_double("/controls/hover", 0.0);
                ascent_ship_ = active_;
                ascent_body_ = ship->telemetry().soi;
                ascent_stage_ = ship->active_stage();
                props.set_bool("/autopilot/vessel-ascent/active", true);
                ascent_profile_ = request;
                ascent_phase_ = "Liftoff";
                ascent_heading_ = std::fmod(std::fmod(heading, 360.0) + 360.0, 360.0);
                ascent_launch_heading_ = ship->telemetry().heading;
                ascent_target_ = target * 1000.0;
                ascent_pitch_ = request == "Ares" ? 90.0 : 0.0;
                ascent_start_ = universe.time();
                ascent_throttle_ = ascent_hover_ = 0.0;
                ascent_gimbal_ = ascent_saved_gimbal_ = props.get_double("/controls/gimbal-pitch");
                props.set_string("/controls/sas", "attitude");
                log(request + " ascent started");
            }
        }
        if (ascent_ship_ < 0)
            return;
        const double requested_throttle = props.get_double("/controls/throttle");
        const double requested_hover = props.get_double("/controls/hover");
        const double requested_gimbal = props.get_double("/controls/gimbal-pitch");
        const std::string requested_sas = props.get_string("/controls/sas");
        const bool throttle_changed = std::abs(requested_throttle - ascent_throttle_) > 0.001;
        const bool hover_changed = std::abs(requested_hover - ascent_hover_) > 0.001;
        const bool sas_changed = requested_sas != "attitude";
        const bool gimbal_changed = ascent_profile_ == "Deltaglider" && std::abs(requested_gimbal - ascent_gimbal_) > 0.001;
        if (throttle_changed || hover_changed || sas_changed || gimbal_changed)
        {
            cancel_vessel_ascent("Ascent cancelled: manual control changed");
            if (throttle_changed)
                props.set_double("/controls/throttle", requested_throttle);
            if (hover_changed)
                props.set_double("/controls/hover", requested_hover);
            if (sas_changed)
                props.set_string("/controls/sas", requested_sas);
            if (gimbal_changed)
                props.set_double("/controls/gimbal-pitch", requested_gimbal);
            return;
        }
        Ship *ship = active_ship();
        if (!ship || active_ != ascent_ship_ || ship->crashed() || ship->telemetry().stage_fuel <= 0.0 ||
            ship->telemetry().soi != ascent_body_ || ship->active_stage() != ascent_stage_ ||
            !ship->attitude_control_available() || props.get_string("/controls/rcs-mode", "rot") == "off" ||
            props.get_bool("/autopilot/enabled") || props.get_bool("/autopilot/land/armed") ||
            props.get_bool("/autopilot/burn/armed") || props.get_bool("/autopilot/timed/armed") ||
            props.get_bool("/controls/stage") || props.get_int("/controls/parachute") != 0 ||
            std::abs(props.get_double("/controls/rcs-x")) + std::abs(props.get_double("/controls/rcs-y")) +
                std::abs(props.get_double("/controls/rcs-z")) > 0.02 ||
            std::abs(props.get_double("/controls/pod-left")) + std::abs(props.get_double("/controls/pod-right")) > 0.02 ||
            link_claims.count(active_))
        {
            cancel_vessel_ascent("Ascent aborted: control, vessel or fuel unavailable");
            return;
        }
        if (!ascent_docking_allowed(*this, active_))
        {
            cancel_vessel_ascent("Ascent aborted: unsafe docking or payload control");
            return;
        }
        const Telemetry &t = ship->telemetry();
        if (t.soi < 0 || t.soi >= static_cast<int>(universe.bodies.size()) || !universe.world.body(t.soi))
        {
            cancel_vessel_ascent("Ascent aborted: no central body");
            return;
        }
        const double elapsed = universe.time() - ascent_start_;
        if (elapsed > 15.0 && t.altitude < 0.0 && t.vertical_speed < -5.0)
        {
            cancel_vessel_ascent("Ascent aborted: descending into ground");
            return;
        }
        props.set_int("/sim/warp-index", 0);
        props.set_string("/controls/sas", "attitude");
        const bool ares = ascent_profile_ == "Ares";
        const Body &planet = universe.bodies[static_cast<std::size_t>(t.soi)];
        const auto *planet_body = universe.world.body(t.soi);
        const sl::dvec3 r = ship->body().position - planet_body->position;
        const double radius = glm::length(r);
        if (!std::isfinite(radius) || radius <= 0.0 || !std::isfinite(planet.gm) || planet.gm <= 0.0 ||
            !std::isfinite(t.mass) || t.mass <= 0.0)
        {
            cancel_vessel_ascent("Ascent aborted: invalid orbital state");
            return;
        }
        const double gravity = planet.gm / (radius * radius);
        const sl::dvec3 up = r / radius;
        const sl::dvec3 v = ship->body().velocity - planet_body->velocity;
        if (ascent_phase_ != "Liftoff" && glm::dot(v, v) >= 2.0 * planet.gm / radius)
        {
            cancel_vessel_ascent("Ascent aborted: escape trajectory");
            return;
        }
        const double radial = glm::dot(v, up);
        const sl::dvec3 tangential = v - up * radial;
        const bool shuttle_tower_clear = ascent_profile_ != "Shuttle-A" || t.altitude > 150.0;
        double heading = shuttle_tower_clear ? ascent_heading_ : ascent_launch_heading_;
        if ((ascent_phase_ == "Coast" || ascent_phase_ == "Circularise") && glm::length(tangential) > 1.0)
        {
            sl::dvec3 east = glm::cross(planet_body->spin_axis, up);
            if (glm::length(east) > 1e-8)
            {
                east = glm::normalize(east);
                const sl::dvec3 north = glm::cross(up, east);
                heading = std::fmod(glm::degrees(std::atan2(glm::dot(tangential, east), glm::dot(tangential, north))) + 360.0, 360.0);
            }
        }
        props.set_double("/controls/target-heading", heading);
        double hover = 0.0, main = 0.0, pitch = ascent_pitch_;
        if (ascent_phase_ == "Liftoff")
        {
            if (ascent_profile_ == "Deltaglider")
                main = 1.0;
            else
                hover = 1.0;
            if ((ascent_profile_ == "Deltaglider" && t.surface_speed > 45.0) ||
                (ascent_profile_ != "Deltaglider" && t.altitude > (ares ? 200.0 : 150.0)))
                ascent_phase_ = "Climb";
        }
        if (ascent_phase_ == "Climb")
        {
            const double transition_alt = ares ? 35000.0 : ascent_profile_ == "Deltaglider" ? 45000.0 : 35000.0;
            const double fraction = std::clamp(t.altitude / transition_alt, 0.0, 1.0);
            pitch = ares ? 90.0 - 65.0 * std::sqrt(fraction) : ascent_profile_ == "Deltaglider" ? 12.0 + 13.0 * fraction : 25.0 + 25.0 * (1.0 - fraction);
            if (ares)
                hover = 1.0;
            else
            {
                main = 1.0;
                hover = ascent_profile_ == "Deltaglider" ? 0.0 : std::clamp(1.0 - t.altitude / 5000.0, 0.0, 1.0);
            }
            if (t.altitude > transition_alt)
                ascent_phase_ = "Orbit burn";
        }
        if (ascent_phase_ == "Orbit burn")
        {
            const double target_vs = std::clamp((ascent_target_ - t.altitude) * 0.003, 10.0, 220.0);
            const double horizontal = std::sqrt(std::max(0.0, glm::dot(v, v) - radial * radial));
            const double accel = (ares ? 320000.0 : ascent_profile_ == "Deltaglider" ? 400000.0 : 2128720.0) / t.mass;
            const double vertical_accel = gravity - horizontal * horizontal / radius + 0.04 * (target_vs - t.vertical_speed);
            pitch = glm::degrees(std::asin(std::clamp(vertical_accel / accel, -0.15, 0.8)));
            if (ares) hover = 1.0; else main = 1.0;
            if (t.bound && t.apoapsis >= ascent_target_ && t.vertical_speed > 0.0)
                ascent_phase_ = "Coast";
        }
        if (ascent_phase_ == "Coast")
        {
            hover = main = 0.0;
            pitch = 0.0;
            const double heading_error = std::remainder(heading - t.heading, 360.0);
            // Drag may have lowered the predicted apoapsis after the first cutoff.
            if (t.altitude > planet.atmosphere_scale_height * 12.0 && t.bound &&
                t.apoapsis < ascent_target_ - 1000.0 && t.vertical_speed > 20.0 &&
                std::abs(heading_error) < 3.0 && std::abs(t.pitch) < 3.0)
            {
                const double level = std::clamp((ascent_target_ - t.apoapsis) / 100000.0, 0.02, 0.2);
                if (ares) hover = level; else main = level;
            }
            // Waiting close to apoapsis avoids raising the opposite side too far during circularisation.
            if (t.vertical_speed <= 1.0)
            {
                if (!t.bound || t.apoapsis < ascent_target_ - 5000.0 || t.apoapsis > ascent_target_ + 15000.0)
                {
                    cancel_vessel_ascent("Ascent aborted: target apoapsis missed");
                    return;
                }
                ascent_phase_ = "Circularise";
            }
        }
        if (ascent_phase_ == "Circularise")
        {
            pitch = 0.0;
            if (!t.bound || t.apoapsis > ascent_target_ + 15000.0)
            {
                cancel_vessel_ascent("Ascent aborted: orbit overshoot");
                return;
            }
            const double error = ascent_target_ - t.periapsis;
            if (error < 5000.0 && error > -5000.0 && t.apoapsis >= ascent_target_ - 5000.0)
            {
                cancel_vessel_ascent("Ascent complete: orbit reached");
                return;
            }
            if (error <= -5000.0)
            {
                cancel_vessel_ascent("Ascent aborted: periapsis overshoot");
                return;
            }
            const double heading_error = std::remainder(heading - t.heading, 360.0);
            if (std::abs(heading_error) < 3.0 && std::abs(t.pitch) < 3.0)
            {
                const double level = std::clamp(error / 200000.0, 0.01, 0.4);
                if (ares) hover = level; else main = level;
            }
        }
        if (!ares && (ascent_phase_ == "Climb" || ascent_phase_ == "Orbit burn"))
            main *= std::clamp(1.0 - std::max(0.0, t.dynamic_pressure - 70000.0) / 80000.0, 0.3, 1.0);
        const double change = std::max(0.0, dt) * 1.5;
        ascent_pitch_ += std::clamp(pitch - ascent_pitch_, -change, change);
        props.set_double("/controls/target-pitch", ascent_pitch_);
        if (ship->flies_as_aircraft())
        {
            const double pitch_rate = ship->body().angular_velocity.x;
            props.set_double("/controls/pitch", std::clamp((ascent_pitch_ - t.pitch) * 0.1 - pitch_rate * 0.5, -1.0, 1.0));
            props.set_double("/controls/roll", std::clamp(t.roll * 0.04, -0.5, 0.5));
            props.set_double("/controls/yaw", 0.0);
        }
        else
        {
            props.set_double("/controls/pitch", 0.0);
            props.set_double("/controls/roll", 0.0);
            props.set_double("/controls/yaw", 0.0);
        }
        props.set_double("/controls/hover", hover);
        props.set_double("/controls/throttle", main);
        if (ascent_profile_ == "Deltaglider")
        {
            ascent_gimbal_ = ship->main_pitch_trim(props.get_double("/controls/gimbal-yaw"));
            props.set_double("/controls/gimbal-pitch", ascent_gimbal_);
        }
        ascent_hover_ = hover;
        ascent_throttle_ = main;
        if (t.altitude > 100.0)
            props.set_double("/controls/gear", 0.0);
        if (ares && t.altitude > 100.0)
            props.set_double("/controls/anim/legs", 1.0);
        props.set_string("/autopilot/vessel-ascent/state", ascent_phase_);
        props.set_string("/autopilot/status", ascent_profile_ + " ascent: " + ascent_phase_);
    }
}
