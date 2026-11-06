#include "sim.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace kosmos
{
    bool parse_burn_date(const std::string &text, double &jd)
    {
        if (!parse_epoch(text, jd) || !std::isfinite(jd))
            return false;
        const char *begin = text.c_str();
        while (*begin == ' ' || *begin == '\t')
            ++begin;
        int year = 0, month = 0, day = 0, used = 0;
        if (std::sscanf(begin, "%d-%d-%d%n", &year, &month, &day, &used) != 3)
            return true;
        int actual_year, actual_month, actual_day;
        double hours;
        civil_from_julian(julian_from_civil(year, month, day), actual_year, actual_month, actual_day, hours);
        if (year != actual_year || month != actual_month || day != actual_day)
            return false;
        const char *rest = begin + used;
        while (*rest == ' ' || *rest == 'T' || *rest == 't')
            ++rest;
        int hour = 0, minute = 0;
        if (std::sscanf(rest, "%d:%d", &hour, &minute) == 2 && hour >= 24)
            return false;
        // The planner accepts UTC, not offsets which parse_epoch otherwise accepts.
        return std::strchr(rest, '+') == nullptr && std::strchr(rest, '-') == nullptr;
    }

    namespace
    {
        bool burn_engine_available(const Ship &ship, const std::string &group)
        {
            if (ship.active_stage() < 0 || ship.active_stage() >= static_cast<int>(ship.spec().stages.size()))
                return false;
            bool found = false;
            for (const EngineSpec &engine : ship.spec().stages[static_cast<std::size_t>(ship.active_stage())].engines)
                if (!engine.pod && engine.rcs_groups.empty() && engine.hover == (group == "hover") && engine.thrust > 0.0)
                {
                    if (engine.type == EngineType::solid || engine.type == EngineType::hyperjump || engine.air_breathing)
                        return false;
                    found = true;
                }
            return found;
        }

        bool valid_sas(const std::string &sas)
        {
            return sas == "stability" || sas == "prograde" || sas == "retrograde" || sas == "radial-out" ||
                   sas == "radial-in" || sas == "normal" || sas == "antinormal" || sas == "attitude";
        }
    }

    void Sim::cancel_timed_burn(const std::string &reason)
    {
        if (!props.get_bool("/autopilot/timed/armed"))
            return;
        props.set_bool("/autopilot/timed/armed", false);
        props.set_string("/autopilot/timed/state", reason);
        props.set_string("/autopilot/status", reason);
        props.set_double("/controls/throttle", 0.0);
        props.set_double("/controls/hover", 0.0);
        props.set_string("/controls/sas", "stability");
        log(reason);
    }

    bool Sim::arm_timed_burn(double delay, double duration, double throttle, const std::string &engine, const std::string &sas)
    {
        Ship *ship = active_ship();
        std::string error;
        if (!std::isfinite(delay) || !std::isfinite(duration) || !std::isfinite(throttle) ||
            !std::isfinite(universe.time() + delay + duration) || delay < 0.0 || duration <= 0.0 || throttle <= 0.0 || throttle > 1.0)
            error = "invalid countdown, duration or throttle";
        else if (!ship || ship->crashed() || ship->flies_as_aircraft() || ship->spec().ground())
            error = "no operational spacecraft";
        else if ((engine != "main" && engine != "hover") || !burn_engine_available(*ship, engine) || ship->telemetry().stage_fuel <= 0.0)
            error = "selected throttleable engine or fuel unavailable";
        else if (!valid_sas(sas) || !ship->attitude_control_available() || (ship->spec().rcs_modes && props.get_string("/controls/rcs-mode", "rot") == "off"))
            error = "attitude control unavailable";
        else if (sas == "attitude" && (!std::isfinite(props.get_double("/controls/target-pitch")) ||
                                       !std::isfinite(props.get_double("/controls/target-heading"))))
            error = "invalid target attitude";
        else if (nav.docked(active_index()) || props.get_int("/ships/ship[" + std::to_string(active_index()) + "]/chutes/open") > 0)
            error = "undock and stow parachutes first";
        else if (std::abs(props.get_double("/controls/pod-left")) > 0.001 || std::abs(props.get_double("/controls/pod-right")) > 0.001 ||
                 std::abs(props.get_double("/controls/rcs-x")) + std::abs(props.get_double("/controls/rcs-y")) +
                         std::abs(props.get_double("/controls/rcs-z")) > 0.02)
            error = "release manual RCS and pod thrust first";
        else if (const auto it = link_claims.find(active_index()); it != link_claims.end() && !it->second.passthrough)
            error = "vessel controlled by an addon";
        if (!error.empty())
        {
            if (!props.get_bool("/autopilot/timed/armed"))
                props.set_string("/autopilot/timed/state", "Not armed: " + error);
            log("Timed burn not armed: " + error);
            return false;
        }
        cancel_autoland("Autoland cancelled: timed burn armed");
        cancel_timed_burn("Timed burn replaced");
        props.set_bool("/autopilot/enabled", false);
        props.set_bool("/autopilot/burn/armed", false);
        props.set_double("/controls/throttle", 0.0);
        props.set_double("/controls/hover", 0.0);
        props.set_string("/controls/sas", sas);
        props.set_double("/autopilot/timed/start", universe.time() + delay);
        props.set_double("/autopilot/timed/duration", duration);
        props.set_double("/autopilot/timed/throttle", throttle);
        props.set_string("/autopilot/timed/engine", engine);
        props.set_string("/autopilot/timed/sas", sas);
        props.set_double("/autopilot/timed/pitch", props.get_double("/controls/target-pitch"));
        props.set_double("/autopilot/timed/heading", props.get_double("/controls/target-heading"));
        props.set_int("/autopilot/timed/stage", ship->active_stage());
        props.set_string("/autopilot/timed/state", "Waiting");
        props.set_bool("/autopilot/timed/armed", true);
        props.set_int("/sim/warp-index", 0);
        log("Timed burn armed");
        return true;
    }

    void Sim::run_timed_burn()
    {
        if (!props.get_bool("/autopilot/timed/armed"))
            return;
        Ship *ship = active_ship();
        const std::string engine = props.get_string("/autopilot/timed/engine");
        const std::string sas = props.get_string("/autopilot/timed/sas");
        if (!ship || ship->crashed() || ship->active_stage() != props.get_int("/autopilot/timed/stage") ||
            !burn_engine_available(*ship, engine) || ship->telemetry().stage_fuel <= 0.0)
        {
            cancel_timed_burn("Timed burn aborted: engine or fuel unavailable");
            return;
        }
        if (props.get_bool("/autopilot/enabled") || props.get_bool("/autopilot/burn/armed") || props.get_bool("/autopilot/land/armed") ||
            !ship->attitude_control_available() ||
            (ship->spec().rcs_modes && props.get_string("/controls/rcs-mode", "rot") == "off"))
        {
            cancel_timed_burn("Timed burn cancelled: control changed");
            return;
        }
        const std::string current_sas = props.get_string("/controls/sas");
        if (current_sas != sas || (sas == "attitude" &&
            (props.get_double("/controls/target-pitch") != props.get_double("/autopilot/timed/pitch") ||
             props.get_double("/controls/target-heading") != props.get_double("/autopilot/timed/heading"))))
        {
            cancel_timed_burn("Timed burn cancelled: attitude changed");
            props.set_string("/controls/sas", current_sas);
            return;
        }
        if (nav.docked(active_index()) || props.get_int("/ships/ship[" + std::to_string(active_index()) + "]/chutes/open") > 0)
        {
            cancel_timed_burn("Timed burn aborted: docked or parachute open");
            return;
        }
        const auto claim = link_claims.find(active_index());
        if (claim != link_claims.end() && !claim->second.passthrough)
        {
            cancel_timed_burn("Timed burn aborted: vessel controlled by an addon");
            return;
        }
        props.set_int("/sim/warp-index", 0);
        const double start = props.get_double("/autopilot/timed/start");
        const double duration = props.get_double("/autopilot/timed/duration");
        const double now = universe.time();
        const bool burning = props.get_string("/autopilot/timed/state") == "Burning";
        const double expected = burning ? props.get_double("/autopilot/timed/throttle") : 0.0;
        if (std::abs(props.get_double(engine == "hover" ? "/controls/hover" : "/controls/throttle") - expected) > 1e-6 ||
            std::abs(props.get_double(engine == "hover" ? "/controls/throttle" : "/controls/hover")) > 1e-6 ||
            props.get_bool("/controls/stage") || std::abs(props.get_double("/controls/pod-left")) > 0.001 ||
            std::abs(props.get_double("/controls/pod-right")) > 0.001 ||
            std::abs(props.get_double("/controls/rcs-x")) + std::abs(props.get_double("/controls/rcs-y")) +
                    std::abs(props.get_double("/controls/rcs-z")) > 0.02)
        {
            cancel_timed_burn("Timed burn cancelled: manual control changed");
            return;
        }
        props.set_double("/autopilot/timed/remaining", std::max(0.0, now < start ? start - now : start + duration - now));
        if (now < start)
        {
            props.set_string("/autopilot/status", "Timed burn: waiting");
            return;
        }
        if (now >= start + duration)
        {
            cancel_timed_burn("Timed burn complete");
            return;
        }
        if (sas != "stability")
        {
            Controls controls;
            controls.sas = sas;
            controls.target_pitch = props.get_double("/controls/target-pitch");
            controls.target_heading = props.get_double("/controls/target-heading");
            bool valid = false;
            const sl::dvec3 target = ship->sas_direction(controls, valid);
            const sl::dvec3 nose = ship->body().direction_to_world(sl::dvec3(0, 0, -1));
            const double alignment = glm::dot(nose, target);
            if (!valid || !std::isfinite(alignment) || alignment < 0.985)
            {
                cancel_timed_burn("Timed burn aborted: attitude not aligned");
                return;
            }
        }
        props.set_string("/autopilot/timed/state", "Burning");
        props.set_string("/autopilot/status", "Timed burn: burning");
        props.set_double(engine == "hover" ? "/controls/hover" : "/controls/throttle", props.get_double("/autopilot/timed/throttle"));
    }
}
