#include "sim.h"
#include "landing_descent.h"

#include <algorithm>
#include <cmath>

namespace kosmos
{
    void Sim::cancel_autoland(const std::string &reason)
    {
        const bool active = landing_body_ >= 0 || props.get_bool("/autopilot/land/armed");
        landing_body_ = -1;
        landing_stage_ = -1;
        landing_orbital_ = false;
        landing_forecast_.reset();
        landing_candidates_.clear();
        landing_last_level_ = 0.0;
        props.set_double("/autopilot/land/window", 0.0);
        if (!active)
            return;
        props.set_bool("/autopilot/land/armed", false);
        props.set_string("/autopilot/land/state", reason);
        props.set_string("/autopilot/status", reason);
        props.set_double("/controls/hover", 0.0);
        props.set_string("/controls/sas", "stability");
        log(reason);
    }

    void Sim::run_autoland(double frame)
    {
        if (!props.get_bool("/autopilot/land/armed"))
        {
            if (landing_body_ >= 0)
                cancel_autoland("Autoland cancelled");
            return;
        }

        auto finish = [&](const std::string &message) {
            cancel_autoland(message);
        };
        if (props.get_bool("/autopilot/enabled") || props.get_bool("/autopilot/burn/armed") ||
            props.get_bool("/autopilot/timed/armed"))
        {
            finish("Autoland cancelled: another autopilot engaged");
            return;
        }
        const std::string previous_phase = landing_body_ >= 0 ? props.get_string("/autopilot/land/state") : "";
        const std::string sas = props.get_string("/controls/sas");
        if ((previous_phase == "Deorbit" || previous_phase == "Entry" || previous_phase == "Braking" ||
             previous_phase == "Target approach" || previous_phase == "Plane alignment" ||
             previous_phase == "Plane burn" || previous_phase == "Powered descent") && sas != "attitude")
        {
            finish("Autoland cancelled: manual SAS");
            props.set_string("/controls/sas", sas);
            return;
        }
        if ((previous_phase == "Planning orbit" || previous_phase == "Waiting for window" ||
             previous_phase == "Checking orbit") && sas != "off")
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
        if (landing_body_ >= 0 && (t.soi != landing_body_ || ship->active_stage() != landing_stage_))
        {
            finish("Autoland aborted: body or stage changed");
            return;
        }
        const bool shuttle = ship->spec().name == "CVEL Shuttle-A" ||
                             ship->spec().name == "CVEL Shuttle-A (Nuclear)" ||
                             ship->spec().name == "CVEL Shuttle-AQ";
        const sl::dvec3 thrust_axis = shuttle ? sl::dvec3(0, 1, 0) : sl::dvec3(0, 0, -1);
        if (shuttle && !props.get_bool("/autopilot/land/targeted"))
        {
            finish("Autoland aborted: Shuttle-A requires a target");
            return;
        }
        if (t.soi < 0 || static_cast<std::size_t>(t.soi) >= universe.bodies.size() ||
            (universe.bodies[static_cast<std::size_t>(t.soi)].name != "Mars" &&
                          !(props.get_bool("/autopilot/land/targeted") &&
                            (shuttle || universe.bodies[static_cast<std::size_t>(t.soi)].name == "Moon"))))
        {
            finish("Autoland aborted: only Mars or Moon/Shuttle-A targets are supported");
            return;
        }
        if (ship->flies_as_aircraft() || (ship->spec().horizontal && !shuttle) || ship->body().gear.empty() ||
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
        double thrust = 0.0, flow = 0.0;
        bool valid_flow = true;
        for (const EngineSpec &engine : stage.engines)
        {
            if (!engine.hover)
                continue;
            const double engine_direction_length = glm::length(engine.direction);
            if (!std::isfinite(engine_direction_length) || engine_direction_length < 1.0e-8 ||
                glm::dot(engine.direction / engine_direction_length, thrust_axis) < 0.999 ||
                !std::isfinite(engine.thrust) || engine.thrust <= 0.0 ||
                engine.air_breathing || engine.type == EngineType::solid || engine.type == EngineType::hyperjump)
            {
                finish("Autoland aborted: hover engines must be throttleable rockets with the supported thrust axis");
                return;
            }
            thrust += engine.thrust;
            if (engine.isp > 0.0)
                flow += engine.thrust / (engine.isp * 9.80665);
            else
                valid_flow = false;
        }
        const sl::FlightBody &planet = *universe.world.body(t.soi);
        const sl::dvec3 r = ship->body().position - planet.position;
        const double radius = glm::length(r);
        if (!std::isfinite(radius) || radius < 1.0)
        {
            finish("Autoland aborted: invalid vessel position");
            return;
        }
        const sl::dvec3 up = r / radius;
        const double gravity = universe.bodies[static_cast<std::size_t>(t.soi)].gm / (radius * radius);
        if (landing_orbital_ && !landing_reserve_warned_ && t.stage_fuel < 100.0)
        {
            log("Autoland warning: descent fuel reserve depleted; monitor and take over");
            landing_reserve_warned_ = true;
        }
        if (t.landed && t.surface_speed < 2.0)
        {
            if (previous_phase != "Deorbit" && previous_phase != "Entry" && previous_phase != "Braking" &&
                previous_phase != "Target approach" && props.get_bool("/autopilot/land/targeted"))
                finish(landing_orbital_ ? "Autoland aborted: landed before target approach" :
                                          "Autoland aborted: target landing must start airborne");
            else
                finish(previous_phase == "Target approach" && props.get_double("/autopilot/land/distance") >
                           (landing_orbital_ ? 3000.0 : 50.0) ?
                           "Autoland: landed away from target" : "Autoland: landed");
            return;
        }
        if (!std::isfinite(t.mass) || !std::isfinite(t.stage_fuel) || !std::isfinite(gravity) ||
            t.mass <= 0.0 || thrust <= t.mass * gravity * 1.1 || t.stage_fuel < 1.0)
        {
            finish("Autoland aborted: insufficient hover thrust or fuel");
            return;
        }
        props.set_double("/controls/throttle", 0.0);
        props.set_double("/controls/gear", 1.0);
        if (ship->spec().name == "Ares Spider")
            props.set_double("/controls/anim/legs", 0.0);
        if (previous_phase != "Waiting for window" && props.get_int("/sim/warp-index") > 3)
            props.set_int("/sim/warp-index", 3);

        const sl::dvec3 velocity = ship->relative_velocity();
        const sl::dvec3 horizontal = velocity - up * glm::dot(velocity, up);
        const double hspeed = glm::length(horizontal);
        std::string phase = previous_phase;
        bool started_orbital_plan = false;
        if (phase != "Deorbit" && phase != "Entry" && phase != "Braking" && phase != "Target approach" &&
            phase != "Planning orbit" && phase != "Waiting for window" && phase != "Checking orbit" && phase != "Plane alignment" &&
            phase != "Plane burn" && phase != "Powered descent")
        {
            landing_targeted_ = props.get_bool("/autopilot/land/targeted");
            landing_orbital_ = false;
            if (landing_targeted_)
            {
                landing_latitude_ = props.get_double("/autopilot/land/latitude");
                landing_longitude_ = props.get_double("/autopilot/land/longitude");
                const std::string base_id = props.get_string("/autopilot/land/base");
                if (!base_id.empty())
                {
                    const auto found = std::find_if(bases.begin(), bases.end(), [&](const Base &base) {
                        return base.id == base_id && base.body == universe.bodies[static_cast<std::size_t>(t.soi)].name;
                    });
                    if (found == bases.end())
                    {
                        finish("Autoland aborted: selected base not found on current body");
                        return;
                    }
                    Base base = *found;
                    landing_base_ = base;
                    base.direction = base_direction(universe, t.soi, base);
                    base.pole = universe.pole(t.soi);
                    sl::dvec3 pad;
                    double heading;
                    base_pad(base, universe.bodies[static_cast<std::size_t>(t.soi)].radius, pad, heading);
                    surface_lat_lon(universe, t.soi, pad, landing_latitude_, landing_longitude_);
                }
                if (!std::isfinite(landing_latitude_) || !std::isfinite(landing_longitude_) ||
                    std::abs(landing_latitude_) > 90.0 || std::abs(landing_longitude_) > 180.0)
                {
                    finish("Autoland aborted: invalid target coordinates");
                    return;
                }
                const sl::dvec3 target = surface_direction(universe, t.soi, landing_latitude_, landing_longitude_);
                const double distance = radius * std::acos(std::clamp(glm::dot(up, target), -1.0, 1.0));
                if (distance <= 2000.0 && t.altitude >= 300.0 && t.altitude <= 3000.0 &&
                    hspeed <= 50.0 && std::abs(t.vertical_speed) <= 30.0)
                    phase = "Target approach";
                else if (t.bound && t.altitude >= 50000.0 && !base_id.empty())
                {
                    if (!shuttle && ship->spec().name != "Ares Spider")
                    {
                        finish("Autoland aborted: orbital base landing requires Ares or Shuttle-A");
                        return;
                    }
                    if (claim != link_claims.end())
                    {
                        finish("Autoland aborted: release addon vessel control before orbital planning");
                        return;
                    }
                    if (props.get_bool("/sim/settings/nonspherical-gravity"))
                    {
                        finish("Autoland aborted: orbital base planning requires spherical gravity");
                        return;
                    }
                    if (props.get_bool("/sim/settings/weather", true) && Weather::supports_body(universe, t.soi))
                    {
                        finish("Autoland aborted: disable weather for orbital base planning");
                        return;
                    }
                    if (!valid_flow || !std::isfinite(flow) || flow <= 0.0 || !std::isfinite(t.period) || t.period <= 0.0 ||
                        t.periapsis < 50000.0 ||
                        (ship->rails_margin() < 1e17 && t.periapsis <= t.altitude - ship->rails_margin()))
                    {
                        finish("Autoland aborted: unsupported orbit or hover propulsion");
                        return;
                    }
                    landing_plan_time_ = universe.time();
                    landing_checked_mass_ = t.mass;
                    landing_checked_fuel_ = t.stage_fuel;
                    landing_plan_position_ = r;
                    landing_plan_velocity_ = ship->body().velocity - planet.velocity;
                    landing_candidates_ = landing_candidates(
                        universe.bodies[static_cast<std::size_t>(t.soi)].gm, r, landing_plan_velocity_, target,
                        universe.pole(t.soi), universe.bodies[static_cast<std::size_t>(t.soi)].spin,
                        600.0, t.period * 14.0, {0.42, 0.48, 0.6, 0.36, 0.54, 0.66},
                        t.mass, t.stage_fuel, 100.0, thrust, flow, 80.0, 240.0);
                    landing_candidate_index_ = 0;
                    props.set_int("/autopilot/land/candidates", static_cast<int>(landing_candidates_.size()));
                    props.set_int("/autopilot/land/candidate", 0);
                    landing_orbital_ = true;
                    landing_forecast_changed_ = false;
                    landing_reserve_warned_ = false;
                    if (landing_candidates_.empty())
                    {
                        finish("Autoland aborted: no plane-change windows with sufficient fuel");
                        return;
                    }
                    phase = "Planning orbit";
                    started_orbital_plan = true;
                }
                else
                {
                    finish("Autoland aborted: target requires 300-3000 m altitude, within 2 km, horizontal speed below 50 m/s and vertical speed below 30 m/s");
                    return;
                }
            }
            else if (!t.bound || t.altitude < 50000.0)
            {
                finish("Autoland aborted: start from a bound Mars orbit above 50 km");
                return;
            }
            else
                phase = t.periapsis > -20000.0 ? "Deorbit" : "Entry";
            landing_body_ = t.soi;
            landing_stage_ = ship->active_stage();
        }
        if (started_orbital_plan)
        {
            props.set_int("/sim/warp-index", 0);
            props.set_double("/controls/hover", 0.0);
            props.set_string("/controls/sas", "off");
            props.set_string("/autopilot/land/state", phase);
            props.set_string("/autopilot/status", "Autoland: " + phase);
            return;
        }
        sl::dvec3 direction = up;
        double level = 0.0;
        const double accel = thrust / t.mass;
        const bool orbital_phase = phase == "Planning orbit" || phase == "Waiting for window" ||
                                   phase == "Checking orbit" || phase == "Plane alignment" ||
                                   phase == "Plane burn" || phase == "Powered descent";
        if (orbital_phase)
        {
            const bool conditions_changed = props.get_bool("/sim/settings/nonspherical-gravity") ||
                (props.get_bool("/sim/settings/weather", true) && Weather::supports_body(universe, t.soi));
            if (conditions_changed && phase != "Plane burn" && phase != "Powered descent")
            {
                finish("Autoland aborted: orbital forecast conditions changed");
                return;
            }
            if (conditions_changed && !landing_forecast_changed_)
            {
                log("Autoland warning: forecast conditions changed during descent; monitor and take over");
                landing_forecast_changed_ = true;
            }
            const double now = universe.time();
            if (phase == "Waiting for window" || phase == "Checking orbit" || phase == "Plane alignment")
                props.set_double("/autopilot/land/window", std::max(0.0, landing_ignition_ - now));
            if (phase == "Planning orbit" || phase == "Waiting for window" || phase == "Checking orbit" ||
                phase == "Plane alignment")
            {
                sl::OrbitElements initial;
                const double mu = universe.bodies[static_cast<std::size_t>(t.soi)].gm;
                if (!sl::orbit_from_state(mu, landing_plan_position_, landing_plan_velocity_, initial))
                {
                    finish("Autoland aborted: invalid planning orbit");
                    return;
                }
                if (std::abs(t.mass - landing_checked_mass_) > std::max(10.0, landing_checked_mass_ * 0.001) ||
                    std::abs(t.stage_fuel - landing_checked_fuel_) > 0.5)
                {
                    finish("Autoland aborted: vessel mass or fuel changed after forecast");
                    return;
                }
                const sl::OrbitState expected = sl::state_from_orbit(mu, initial, now - landing_plan_time_);
                const double position_error = glm::length(expected.position - r);
                const double velocity_error = glm::length(expected.velocity - (ship->body().velocity - planet.velocity));
                if (!std::isfinite(position_error) || !std::isfinite(velocity_error) ||
                    position_error > 500.0 || velocity_error > 0.5)
                {
                    finish("Autoland aborted: orbit changed since planning");
                    return;
                }
            }
            if (phase == "Planning orbit" || phase == "Checking orbit")
            {
                props.set_int("/sim/warp-index", 0);
                props.set_double("/controls/hover", 0.0);
                props.set_string("/controls/sas", "off");
                if (!landing_forecast_)
                {
                    if (phase == "Planning orbit")
                    {
                        while (landing_candidate_index_ < landing_candidates_.size() &&
                               landing_plan_time_ + landing_candidates_[landing_candidate_index_].ignition_wait - now < 150.0)
                            ++landing_candidate_index_;
                        if (landing_candidate_index_ == landing_candidates_.size())
                        {
                            finish("Autoland aborted: no feasible orbital landing found");
                            return;
                        }
                        landing_candidate_ = landing_candidates_[landing_candidate_index_++];
                        props.set_int("/autopilot/land/candidate", static_cast<int>(landing_candidate_index_));
                        landing_ignition_ = landing_plan_time_ + landing_candidate_.ignition_wait;
                    }
                    LandingCandidate candidate = landing_candidate_;
                    candidate.ignition_wait = landing_ignition_ - now;
                    landing_forecast_ = std::make_unique<LandingCandidateForecast>(
                        universe, *ship, landing_base_, t.soi, shuttle, thrust_axis, candidate, thrust, 100.0, true);
                }
                if (landing_forecast_->advance(8))
                {
                    const LandingForecastStatus status = landing_forecast_->result().status;
                    landing_forecast_.reset();
                    if (status == LandingForecastStatus::unavailable_world || status == LandingForecastStatus::unavailable_wind)
                    {
                        finish("Autoland aborted: forecast environment unavailable");
                        return;
                    }
                    if (status == LandingForecastStatus::landed &&
                        landing_ignition_ - now >= (phase == "Planning orbit" ? 130.0 : 40.0))
                    {
                        phase = phase == "Planning orbit" ? "Waiting for window" : "Plane alignment";
                        props.set_double("/autopilot/land/window", std::max(0.0, landing_ignition_ - now));
                        if (phase == "Plane alignment")
                        {
                            landing_checked_mass_ = t.mass;
                            landing_checked_fuel_ = t.stage_fuel;
                            props.set_string("/controls/sas", "attitude");
                        }
                    }
                    else if (phase == "Checking orbit")
                    {
                        finish("Autoland aborted: landing forecast no longer feasible");
                        return;
                    }
                }
                props.set_string("/autopilot/land/state", phase);
                props.set_string("/autopilot/status", "Autoland: " + phase);
                return;
            }
            if (phase == "Waiting for window")
            {
                const double left = landing_ignition_ - now;
                props.set_double("/controls/hover", 0.0);
                props.set_string("/controls/sas", "off");
                int index = std::clamp(props.get_int("/sim/warp-index"), 0, warp_levels - 1);
                while (index > 0 && warp_factor(index) * std::max(frame, 0.25) > std::max(0.0, left - 130.0) * 0.25)
                    --index;
                props.set_int("/sim/warp-index", index);
                if (left <= 130.0)
                    phase = "Checking orbit";
                props.set_string("/autopilot/land/state", phase);
                props.set_string("/autopilot/status", "Autoland: " + phase);
                return;
            }
            const double dv = glm::length(landing_candidate_.window.change.delta_v);
            if (phase == "Plane alignment" || phase == "Plane burn")
            {
                const sl::dvec3 initial_velocity = landing_candidate_.window.departure_velocity;
                direction = dv > 1e-8 ? landing_candidate_.window.change.delta_v / dv : -glm::normalize(initial_velocity);
                const double alignment = glm::dot(ship->body().direction_to_world(thrust_axis), direction);
                if (phase == "Plane alignment" && now >= landing_ignition_)
                {
                    if (now - landing_ignition_ > 1.0 || !std::isfinite(alignment) || alignment < 0.995)
                    {
                        finish("Autoland aborted: plane-burn alignment missed");
                        return;
                    }
                    phase = "Plane burn";
                    props.set_double("/autopilot/land/window", 0.0);
                    landing_burn_delivered_ = 0.0;
                    landing_last_time_ = now;
                    landing_last_level_ = 0.0;
                }
                if (phase == "Plane burn")
                {
                    const double elapsed = std::max(0.0, now - landing_last_time_);
                    if (landing_last_mass_ > 0.0)
                        landing_burn_delivered_ += thrust * landing_last_level_ / landing_last_mass_ * elapsed;
                    landing_last_time_ = now;
                    if (landing_burn_delivered_ + 0.01 >= dv)
                    {
                        phase = "Powered descent";
                        landing_descent_start_ = now;
                        landing_last_level_ = 0.0;
                    }
                    else if (!std::isfinite(alignment) || alignment < 0.995 ||
                             now > landing_ignition_ + landing_candidate_.burn.duration + 1.0)
                    {
                        finish("Autoland aborted: plane burn failed");
                        return;
                    }
                    else
                    {
                        const double interval = frame * warp_factor(props.get_int("/sim/warp-index"));
                        level = landing_plane_burn_level(dv - landing_burn_delivered_, accel, interval);
                        landing_last_level_ = level;
                        landing_last_mass_ = t.mass;
                    }
                }
            }
            if (phase == "Powered descent")
            {
                const sl::dvec3 target = surface_direction(universe, t.soi, landing_latitude_, landing_longitude_);
                const double distance = radius * std::acos(std::clamp(glm::dot(up, target), -1.0, 1.0));
                props.set_double("/autopilot/land/distance", distance);
                if (distance < 1500.0 && t.altitude > 300.0 && t.altitude < 3000.0 &&
                    hspeed < 40.0 && std::abs(t.vertical_speed) < 25.0)
                    phase = "Target approach";
                else
                {
                    if (now - landing_descent_start_ > 1800.0)
                    {
                        finish("Autoland aborted: powered descent timed out");
                        return;
                    }
                    const Body &body = universe.bodies[static_cast<std::size_t>(t.soi)];
                    const auto command = landing_descent_command(r, ship->body().velocity - planet.velocity, target,
                                                                 universe.pole(t.soi), body.spin, body.gm,
                                                                 body.radius + landing_base_.height + landing_base_.pad_deck + 2500.0,
                                                                 std::max(650.0 - (now - landing_descent_start_), 60.0));
                    const double magnitude = glm::length(command.acceleration);
                    if (!command.valid || !std::isfinite(magnitude) || magnitude < 1e-8)
                    {
                        finish("Autoland aborted: invalid descent guidance");
                        return;
                    }
                    direction = command.acceleration / magnitude;
                    level = std::clamp(magnitude / accel, 0.0, 1.0);
                }
            }
        }
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
            if (landing_orbital_ && !landing_forecast_changed_ &&
                (props.get_bool("/sim/settings/nonspherical-gravity") ||
                 (props.get_bool("/sim/settings/weather", true) && Weather::supports_body(universe, t.soi))))
            {
                log("Autoland warning: forecast conditions changed during descent; monitor and take over");
                landing_forecast_changed_ = true;
            }
            const sl::dvec3 target = surface_direction(universe, t.soi, landing_latitude_, landing_longitude_);
            const auto approach = landing_approach_command(r, velocity, target, t.altitude, t.vertical_speed, gravity, accel);
            if (!approach.valid)
            {
                finish("Autoland aborted: invalid target guidance");
                return;
            }
            const double distance = landing_orbital_ ?
                radius * std::acos(std::clamp(glm::dot(up, base_direction(universe, t.soi, landing_base_)), -1.0, 1.0)) :
                approach.distance;
            props.set_double("/autopilot/land/distance", distance);
            direction = approach.direction;
            level = approach.level;
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
        landing_axis_ = thrust_axis;
        landing_direction_ = direction;
        const sl::dvec3 nose = ship->body().direction_to_world(thrust_axis);
        if (glm::dot(nose, direction) < 0.98)
            level = 0.0;
        props.set_double("/controls/hover", level);
        props.set_string("/autopilot/land/state", phase);
        props.set_string("/autopilot/status", "Autoland: " + phase);
    }
}
