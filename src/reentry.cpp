#include "reentry.h"

#include "reentry_json.h"

#include <algorithm>
#include <cmath>

namespace kosmos
{
    namespace
    {
        constexpr double stefan_boltzmann = 5.670374419e-8;
        constexpr double pi = 3.14159265358979323846;

        sl::dvec3 vec3(const nlohmann::json &j, const sl::dvec3 &fallback)
        {
            if (!j.is_array() || j.size() < 3)
                return fallback;
            return {j[0].get<double>(), j[1].get<double>(), j[2].get<double>()};
        }

        sl::dvec3 unit(const sl::dvec3 &v, const sl::dvec3 &fallback)
        {
            const double l = glm::length(v);
            return l > 1.0e-12 ? v / l : fallback;
        }
    } // namespace

    double stagnation_flux(double rho, double v, double rn, double k)
    {
        if (rho <= 0.0 || v <= 0.0 || rn <= 0.0)
            return 0.0;
        return k * std::sqrt(rho / rn) * v * v * v;
    }

    double step_thermal(const ThermalSpec &spec, ThermalState &state, double rho, double v, double windward, double ambient, double dt)
    {
        if (!spec.enabled || dt <= 0.0)
            return 0.0;
        if (state.shield < 0.0)
            state.shield = spec.shield_mass;
        const double cone = std::cos(spec.shield_cone * pi / 180.0);
        const bool leading = windward >= cone;
        const bool has_shield = spec.shield_mass <= 0.0 || state.shield > 0.0;
        state.shielded = leading && has_shield;
        const double stag = stagnation_flux(rho, v, spec.nose_radius, spec.k);
        state.flux = stag;
        state.peak_flux = std::max(state.peak_flux, stag);
        state.blackout = stag > spec.blackout_flux;

        const double area = std::max(spec.heat_area, 0.01);
        const double capacity = spec.heat_capacity * area;
        const double power = stag * spec.flux_factor * area;
        const double limit = state.shielded ? spec.max_temp : spec.bare_max_temp;
        double damage = 0.0;
        const int steps = std::clamp(static_cast<int>(std::ceil(dt / 0.25)), 1, 400);
        const double h = dt / steps;
        for (int i = 0; i < steps; ++i)
        {
            double into_skin = power;
            if (state.shielded && spec.shield_mass > 0.0)
            {
                // The ablator carries the heat off as it burns; only the leak gets through.
                const double burnt = std::min(state.shield, power * h / spec.ablation_heat);
                state.shield -= burnt;
                into_skin = power * spec.leak;
            }
            const double radiated = spec.emissivity * stefan_boltzmann * (std::pow(state.temp, 4.0) - std::pow(ambient, 4.0)) * area;
            state.temp = std::max(ambient, state.temp + (into_skin - radiated) * h / capacity);
            state.peak_temp = std::max(state.peak_temp, state.temp);
            if (state.temp > limit)
                damage += (state.temp - limit) / limit * 0.3 * h;
        }
        return damage / dt;
    }

    AeroResult hypersonic_aero(const HypersonicSpec &spec, const sl::dvec3 &velocity, double rho, const sl::dvec3 &rate)
    {
        AeroResult out;
        const double v = glm::length(velocity);
        if (!spec.enabled || rho <= 0.0 || v < spec.blend_lo)
            return out;
        const double t = std::clamp((v - spec.blend_lo) / std::max(spec.blend_hi - spec.blend_lo, 1.0), 0.0, 1.0);
        out.blend = t * t * (3.0 - 2.0 * t);
        const sl::dvec3 vhat = velocity / v;
        const sl::dvec3 axis = unit(spec.axis, sl::dvec3(0.0, -1.0, 0.0));
        const double q = 0.5 * rho * v * v;
        const double c = glm::dot(axis, vhat);
        const double normal = c > 0.0 ? spec.cd_max * c * c : -spec.cd_back * c * c;
        sl::dvec3 force = -axis * (q * spec.area * normal) - vhat * (q * spec.area * spec.cd_skin);

        // Moment toward the trim attitude: the flow should arrive at `trim_deg` from the axis, towards the trim direction.
        const sl::dvec3 side = unit(spec.trim_dir - axis * glm::dot(spec.trim_dir, axis), sl::dvec3(0.0, 0.0, -1.0));
        const double angle = spec.trim_deg * pi / 180.0;
        const sl::dvec3 target = axis * std::cos(angle) + side * std::sin(angle);
        sl::dvec3 torque = glm::cross(target, vhat) * (q * spec.area * spec.length * spec.cm);
        torque -= rate * (spec.damping * q * spec.area * spec.length * spec.length / v);
        out.force = force * out.blend;
        out.torque = torque * out.blend;
        return out;
    }

    double chute_drag_area(const ParachuteSpec &spec, const ChuteRuntime &runtime)
    {
        switch (runtime.state)
        {
        case ChuteState::reefed:
            return spec.reef_cd_area * std::min(1.0, runtime.time / 1.5);
        case ChuteState::opening:
            return spec.reef_cd_area + (spec.cd_area - spec.reef_cd_area) * std::clamp(runtime.fill, 0.0, 1.0);
        case ChuteState::open:
            return spec.cd_area;
        default:
            return 0.0;
        }
    }

    void read_thermal(const nlohmann::json &j, ThermalSpec &s)
    {
        if (!j.is_object())
            return;
        s.enabled = j.value("enabled", true);
        s.nose_radius = j.value("nose_radius", s.nose_radius);
        s.heat_area = j.value("heat_area", s.heat_area);
        s.heat_capacity = j.value("heat_capacity", s.heat_capacity);
        s.emissivity = j.value("emissivity", s.emissivity);
        s.flux_factor = j.value("flux_factor", s.flux_factor);
        s.max_temp = j.value("max_temp", s.max_temp);
        s.bare_max_temp = j.value("bare_max_temp", s.bare_max_temp);
        s.shield_mass = j.value("shield_mass", s.shield_mass);
        s.ablation_heat = j.value("ablation_heat", s.ablation_heat);
        s.leak = j.value("leak", s.leak);
        s.shield_axis = unit(vec3(j.value("shield_axis", nlohmann::json()), s.shield_axis), s.shield_axis);
        s.shield_cone = j.value("shield_cone_deg", s.shield_cone);
        s.k = j.value("k", s.k);
        s.blackout_flux = j.value("blackout_flux", s.blackout_flux);
    }

    void read_hypersonic(const nlohmann::json &j, HypersonicSpec &s)
    {
        if (!j.is_object())
            return;
        s.enabled = j.value("enabled", true);
        s.area = j.value("area", s.area);
        s.length = j.value("length", s.length);
        s.cd_max = j.value("cd_max", s.cd_max);
        s.cd_back = j.value("cd_back", s.cd_back);
        s.cd_skin = j.value("cd_skin", s.cd_skin);
        s.axis = unit(vec3(j.value("axis", nlohmann::json()), s.axis), s.axis);
        s.trim_dir = unit(vec3(j.value("trim_dir", nlohmann::json()), s.trim_dir), s.trim_dir);
        s.trim_deg = j.value("trim_deg", s.trim_deg);
        s.cm = j.value("cm", s.cm);
        s.damping = j.value("damping", s.damping);
        s.blend_lo = j.value("blend_lo", s.blend_lo);
        s.blend_hi = j.value("blend_hi", s.blend_hi);
    }

    void read_parachutes(const nlohmann::json &json, std::vector<ParachuteSpec> &list)
    {
        if (!json.is_array())
            return;
        for (const nlohmann::json &j : json)
        {
            ParachuteSpec s;
            s.name = j.value("name", s.name);
            s.position = vec3(j.value("position", nlohmann::json()), s.position);
            s.cd_area = j.value("cd_area", s.cd_area);
            s.reef_cd_area = j.value("reef_cd_area", s.reef_cd_area);
            s.reef_time = j.value("reef_time", s.reef_time);
            s.inflation_time = j.value("inflation_time", s.inflation_time);
            s.max_q = j.value("max_q", s.max_q);
            s.auto_altitude = j.value("auto_altitude", s.auto_altitude);
            s.auto_speed = j.value("auto_speed", s.auto_speed);
            s.canopy_radius = j.value("canopy_radius", std::sqrt(s.cd_area / 1.5 / pi));
            s.cut_on_landing = j.value("cut_on_landing", s.cut_on_landing);
            s.model = j.value("model", s.model);
            s.model_radius = std::max(j.value("model_radius", s.model_radius), 1e-3);
            list.push_back(std::move(s));
        }
    }
} // namespace kosmos
