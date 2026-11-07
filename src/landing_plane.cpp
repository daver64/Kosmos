#include "landing_plane.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <glm/gtc/quaternion.hpp>

namespace kosmos
{
    namespace
    {
        bool finite(const sl::dvec3 &v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }
    }

    LandingPlaneChange landing_plane_change(const sl::dvec3 &position, const sl::dvec3 &velocity,
                                            const sl::dvec3 &target_direction)
    {
        LandingPlaneChange result;
        if (!finite(position) || !finite(velocity) || !finite(target_direction))
            return result;
        const double radius = glm::length(position);
        const double target_length = glm::length(target_direction);
        if (!std::isfinite(radius) || !std::isfinite(target_length) || radius < 1e-6 || target_length < 1e-12)
            return result;
        const sl::dvec3 up = position / radius;
        const sl::dvec3 transverse = velocity - up * glm::dot(velocity, up);
        const double speed = glm::length(transverse);
        const sl::dvec3 target = target_direction / target_length;
        const sl::dvec3 target_normal = glm::cross(up, target);
        const double normal_length = glm::length(target_normal);
        if (!std::isfinite(speed) || speed < 1e-6 || normal_length < 1e-8)
            return result;

        const sl::dvec3 current_normal = glm::cross(up, transverse / speed);
        sl::dvec3 desired_normal = target_normal / normal_length;
        if (glm::dot(current_normal, desired_normal) < 0.0)
            desired_normal = -desired_normal;
        const sl::dvec3 desired_transverse = glm::cross(desired_normal, up) * speed;
        result.delta_v = desired_transverse - transverse;
        result.angle = std::acos(std::clamp(glm::dot(current_normal, desired_normal), -1.0, 1.0));
        result.valid = true;
        return result;
    }

    double landing_plane_burn_level(double remaining, double acceleration, double interval)
    {
        return std::min(1.0, remaining / (acceleration * std::max(interval, 1e-6)));
    }

    LandingPlaneBurn landing_plane_burn(double delta_v, double mass, double fuel, double reserve,
                                       double thrust, double flow)
    {
        LandingPlaneBurn result;
        if (!std::isfinite(delta_v) || delta_v < 0.0 || !std::isfinite(mass) || mass <= 0.0 ||
            !std::isfinite(fuel) || fuel < 0.0 || fuel >= mass || !std::isfinite(reserve) || reserve < 0.0 ||
            !std::isfinite(thrust) || thrust <= 0.0 || !std::isfinite(flow) || flow <= 0.0)
            return result;
        const double exhaust = thrust / flow;
        const double ratio = delta_v / exhaust;
        if (!std::isfinite(exhaust) || exhaust <= 0.0 || !std::isfinite(ratio))
            return result;
        const double fraction = -std::expm1(-ratio);
        result.fuel = mass * fraction;
        result.duration = result.fuel / flow;
        const double center_fraction = ratio < 1e-3 ?
            0.5 + ratio / 12.0 - ratio * ratio * ratio / 720.0 :
            1.0 / fraction - 1.0 / ratio;
        result.impulse_center = result.duration * center_fraction;
        if (!std::isfinite(result.fuel) || !std::isfinite(result.duration) ||
            !std::isfinite(result.impulse_center))
            return LandingPlaneBurn{};
        result.valid = true;
        result.sufficient_fuel = reserve <= fuel && result.fuel <= fuel - reserve;
        return result;
    }

    std::vector<LandingPlaneWindow> landing_plane_windows(double mu, const sl::dvec3 &position,
                                                         const sl::dvec3 &velocity, const sl::dvec3 &target_direction,
                                                         const sl::dvec3 &spin_axis, double spin_rate,
                                                         double flight_time, double lead_angle, double max_wait)
    {
        std::vector<LandingPlaneWindow> windows;
        if (!finite(position) || !finite(velocity) || !finite(target_direction) || !finite(spin_axis) ||
            !std::isfinite(mu) || mu <= 0.0 || !std::isfinite(spin_rate) ||
            !std::isfinite(flight_time) || flight_time < 0.0 || !std::isfinite(max_wait) || max_wait <= 0.0 ||
            !std::isfinite(lead_angle) || lead_angle <= 0.0 || lead_angle >= 1.5707963267948966)
            return windows;
        const double axis_length = glm::length(spin_axis), target_length = glm::length(target_direction);
        if (!std::isfinite(axis_length) || !std::isfinite(target_length) || axis_length < 1e-12 || target_length < 1e-12)
            return windows;
        sl::OrbitElements orbit;
        if (!sl::orbit_from_state(mu, position, velocity, orbit) || !std::isfinite(orbit.eccentricity) ||
            orbit.eccentricity < 0.0 || orbit.eccentricity >= 1.0 ||
            !std::isfinite(orbit.semi_major_axis) || orbit.semi_major_axis <= 0.0)
            return windows;
        const double period = orbit.period(mu);
        if (!std::isfinite(period) || period <= 0.0 || max_wait > period * 14.0)
            return windows;
        const sl::dvec3 axis = spin_axis / axis_length;
        const sl::dvec3 target = target_direction / target_length;
        const double wanted_cosine = std::cos(lead_angle);
        auto evaluate = [&](double wait, sl::OrbitState &state, sl::dvec3 &arrival) {
            state = sl::state_from_orbit(mu, orbit, wait);
            const double rotation = spin_rate * (wait + flight_time);
            if (!std::isfinite(rotation))
                return std::numeric_limits<double>::quiet_NaN();
            arrival = glm::angleAxis(std::remainder(rotation, 6.283185307179586), axis) * target;
            const double radius = glm::length(state.position);
            return radius > 0.0 ? glm::dot(state.position / radius, arrival) - wanted_cosine :
                                  std::numeric_limits<double>::quiet_NaN();
        };
        auto consider = [&](double wait) {
            sl::OrbitState state;
            sl::dvec3 arrival;
            evaluate(wait, state, arrival);
            const sl::dvec3 up = glm::normalize(state.position);
            const sl::dvec3 transverse = state.velocity - up * glm::dot(state.velocity, up);
            if (glm::dot(transverse, arrival) <= 0.0)
                return;
            const LandingPlaneChange change = landing_plane_change(state.position, state.velocity, arrival);
            if (!change.valid)
                return;
            LandingPlaneWindow window;
            window.valid = true;
            window.wait = wait;
            window.departure_position = state.position;
            window.departure_velocity = state.velocity;
            window.arrival_target = arrival;
            window.change = change;
            windows.push_back(window);
        };
        const double max_orbit_rate = (6.283185307179586 / period) * std::sqrt(1.0 + orbit.eccentricity) /
                                      std::pow(1.0 - orbit.eccentricity, 1.5);
        const double requested_steps = std::ceil(max_wait * (max_orbit_rate + std::abs(spin_rate)) *
                                                 (720.0 / 6.283185307179586));
        if (!std::isfinite(requested_steps) || requested_steps > 100000.0)
            return windows;
        const int steps = std::max(1, static_cast<int>(requested_steps));
        sl::OrbitState state;
        sl::dvec3 arrival;
        double before = evaluate(0.0, state, arrival);
        if (std::abs(before) < 1e-12)
            consider(0.0);
        for (int i = 1; i <= steps; ++i)
        {
            double lo = max_wait * (static_cast<double>(i - 1) / steps);
            double hi = max_wait * (static_cast<double>(i) / steps);
            const double after = evaluate(hi, state, arrival);
            if (std::isfinite(before) && std::isfinite(after) && ((before < 0.0 && after >= 0.0) ||
                                                                    (before > 0.0 && after <= 0.0)))
            {
                double flo = before;
                for (int j = 0; j < 50; ++j)
                {
                    const double mid = 0.5 * (lo + hi);
                    const double fm = evaluate(mid, state, arrival);
                    if ((flo < 0.0 && fm < 0.0) || (flo > 0.0 && fm > 0.0))
                    {
                        lo = mid;
                        flo = fm;
                    }
                    else
                        hi = mid;
                }
                consider(0.5 * (lo + hi));
            }
            before = after;
        }
        return windows;
    }

    LandingPlaneWindow landing_plane_window(double mu, const sl::dvec3 &position, const sl::dvec3 &velocity,
                                            const sl::dvec3 &target_direction, const sl::dvec3 &spin_axis,
                                            double spin_rate, double flight_time, double lead_angle, double max_wait)
    {
        const auto windows = landing_plane_windows(mu, position, velocity, target_direction, spin_axis,
                                                   spin_rate, flight_time, lead_angle, max_wait);
        const auto best = std::min_element(windows.begin(), windows.end(), [](const auto &a, const auto &b) {
            return glm::length(a.change.delta_v) < glm::length(b.change.delta_v);
        });
        return best == windows.end() ? LandingPlaneWindow{} : *best;
    }

    std::vector<LandingCandidate> landing_candidates(double mu, const sl::dvec3 &position,
                                                     const sl::dvec3 &velocity, const sl::dvec3 &target_direction,
                                                     const sl::dvec3 &spin_axis, double spin_rate,
                                                     double flight_time, double max_wait,
                                                     const std::vector<double> &lead_angles,
                                                     double mass, double fuel, double reserve,
                                                     double thrust, double flow,
                                                     double alignment_time, double max_burn_duration)
    {
        std::vector<LandingCandidate> candidates;
        if (lead_angles.size() > 64 || !std::isfinite(alignment_time) || alignment_time < 0.0 ||
            !std::isfinite(max_burn_duration) || max_burn_duration < 0.0 ||
            !landing_plane_burn(0.0, mass, fuel, reserve, thrust, flow).valid ||
            reserve > fuel)
            return candidates;
        for (const double lead : lead_angles)
            if (!std::isfinite(lead) || lead <= 0.0 || lead >= 1.5707963267948966)
                return {};
        for (std::size_t i = 0; i < lead_angles.size(); ++i)
        {
            const double lead = lead_angles[i];
            if (std::find(lead_angles.begin(), lead_angles.begin() + static_cast<std::ptrdiff_t>(i), lead) !=
                lead_angles.begin() + static_cast<std::ptrdiff_t>(i))
                continue;
            const auto windows = landing_plane_windows(mu, position, velocity, target_direction, spin_axis,
                                                       spin_rate, flight_time, lead, max_wait);
            for (const auto &window : windows)
            {
                const auto burn = landing_plane_burn(glm::length(window.change.delta_v), mass, fuel, reserve, thrust, flow);
                const double ignition = window.wait - burn.impulse_center;
                if (!burn.valid || !burn.sufficient_fuel || burn.duration > max_burn_duration ||
                    !std::isfinite(ignition) || ignition < alignment_time)
                    continue;
                candidates.push_back({window, burn, lead, ignition});
            }
        }
        std::stable_sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) {
            return a.burn.fuel != b.burn.fuel ? a.burn.fuel < b.burn.fuel : a.ignition_wait < b.ignition_wait;
        });
        return candidates;
    }
}
