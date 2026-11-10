#include "landing_descent.h"
#include "landing_plane.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/quaternion.hpp>

namespace kosmos
{
    bool landing_descent_captured(double distance, double altitude, double horizontal_speed, double vertical_speed)
    {
        constexpr double max_distance_m = 1500.0;
        constexpr double min_altitude_m = 300.0;
        constexpr double max_altitude_m = 3000.0;
        constexpr double max_horizontal_speed_mps = 40.0;
        constexpr double max_vertical_speed_mps = 25.0;
        return distance < max_distance_m && altitude > min_altitude_m && altitude < max_altitude_m &&
               horizontal_speed < max_horizontal_speed_mps && std::abs(vertical_speed) < max_vertical_speed_mps;
    }

    double landing_descent_remaining_seconds(double elapsed)
    {
        constexpr double planned_descent_seconds = 650.0;
        constexpr double minimum_remaining_seconds = 60.0;
        return std::max(planned_descent_seconds - elapsed, minimum_remaining_seconds);
    }

    namespace
    {
        bool finite(const sl::dvec3 &v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }
    }

    LandingDescentCommand landing_descent_command(const sl::dvec3 &position, const sl::dvec3 &velocity,
                                                 const sl::dvec3 &target_direction, const sl::dvec3 &spin_axis,
                                                 double spin_rate, double mu, double target_radius, double remaining)
    {
        LandingDescentCommand result;
        if (!finite(position) || !finite(velocity) || !finite(target_direction) || !finite(spin_axis) ||
            !std::isfinite(spin_rate) || !std::isfinite(mu) || mu <= 0.0 ||
            !std::isfinite(target_radius) || target_radius <= 0.0 || !std::isfinite(remaining) || remaining <= 0.0)
            return result;
        const double radius = glm::length(position);
        const double target_length = glm::length(target_direction);
        const double axis_length = glm::length(spin_axis);
        const double rotation = spin_rate * remaining;
        if (!std::isfinite(radius) || radius < 1e-6 || !std::isfinite(target_length) || target_length < 1e-12 ||
            !std::isfinite(axis_length) || axis_length < 1e-12 || !std::isfinite(rotation))
            return result;
        const sl::dvec3 pole = spin_axis / axis_length;
        const sl::dvec3 target = glm::angleAxis(std::remainder(rotation, 6.283185307179586), pole) *
                                 (target_direction / target_length) * target_radius;
        const sl::dvec3 target_velocity = glm::cross(pole * spin_rate, target);
        const sl::dvec3 gravity = position * (-mu / (radius * radius * radius));
        result.acceleration = 6.0 * (target - position - velocity * remaining) / (remaining * remaining) -
                              2.0 * (target_velocity - velocity) / remaining - gravity;
        result.valid = finite(result.acceleration);
        return result;
    }

    LandingApproachCommand landing_approach_command(const sl::dvec3 &position, const sl::dvec3 &surface_velocity,
                                                   const sl::dvec3 &target_direction, double altitude,
                                                   double vertical_speed, double gravity, double acceleration)
    {
        LandingApproachCommand result;
        const double radius = glm::length(position);
        const double target_length = glm::length(target_direction);
        if (!finite(position) || !finite(surface_velocity) || !finite(target_direction) ||
            !std::isfinite(radius) || radius < 1e-6 || !std::isfinite(target_length) || target_length < 1e-12 ||
            !std::isfinite(altitude) || !std::isfinite(vertical_speed) || !std::isfinite(gravity) || gravity < 0.0 ||
            !std::isfinite(acceleration) || acceleration <= 0.0)
            return result;
        const sl::dvec3 up = position / radius;
        const sl::dvec3 target = target_direction / target_length;
        const sl::dvec3 horizontal = surface_velocity - up * glm::dot(surface_velocity, up);
        const double hspeed = glm::length(horizontal);
        result.distance = radius * std::acos(std::clamp(glm::dot(up, target), -1.0, 1.0));
        sl::dvec3 toward = target - up * glm::dot(up, target);
        if (glm::length(toward) > 1e-9)
            toward = glm::normalize(toward);
        const double wanted_speed = std::min(30.0, result.distance * 0.08);
        sl::dvec3 horizontal_accel = (toward * wanted_speed - horizontal) * 0.12;
        const double max_horizontal = std::min(2.0, std::max(0.0, (acceleration - gravity) * 0.3));
        if (glm::length(horizontal_accel) > max_horizontal)
            horizontal_accel *= max_horizontal / glm::length(horizontal_accel);
        double wanted_vs = result.distance > 30.0 || hspeed > 3.0 ?
                               std::clamp((500.0 - altitude) * 0.03, -5.0, 5.0) :
                               -std::clamp(altitude * 0.025, 2.0, 15.0);
        if (altitude > 1000.0)
            wanted_vs = -std::clamp(altitude * 0.03, 2.0, 50.0);
        const double vertical_accel = gravity + std::clamp((wanted_vs - vertical_speed) * 0.25, -gravity * 0.8, acceleration);
        const sl::dvec3 wanted = horizontal_accel + up * vertical_accel;
        const double magnitude = glm::length(wanted);
        if (!std::isfinite(magnitude) || magnitude < 1e-12)
            return result;
        result.direction = wanted / magnitude;
        result.level = std::clamp(magnitude / acceleration, 0.0, 1.0);
        result.valid = true;
        return result;
    }

    LandingTrajectoryPrediction landing_trajectory_predict(const LandingTrajectoryInput &in)
    {
        LandingTrajectoryPrediction result;
        if (!finite(in.position) || !finite(in.velocity) || !finite(in.target_direction) || !finite(in.spin_axis) ||
            !finite(in.plane_delta_v) || !std::isfinite(in.spin_rate) || !std::isfinite(in.mu) || in.mu <= 0.0 ||
            !std::isfinite(in.radius) || in.radius <= 0.0 || !std::isfinite(in.target_elevation) ||
            in.radius + in.target_elevation <= 0.0 || !std::isfinite(in.drag_area) || in.drag_area < 0.0 ||
            !std::isfinite(in.atmosphere_density) || in.atmosphere_density < 0.0 ||
            !std::isfinite(in.atmosphere_scale_height) || in.atmosphere_scale_height <= 0.0 ||
            !std::isfinite(in.sea_level_thrust_ratio) || in.sea_level_thrust_ratio <= 0.0 ||
            !std::isfinite(in.torque_accel) || in.torque_accel < 0.0 ||
            !std::isfinite(in.max_rate) || in.max_rate < 0.0 ||
            (in.lifting_surfaces && in.atmosphere_density > 0.0))
            return result;
        const double axis_length = glm::length(in.spin_axis), target_length = glm::length(in.target_direction);
        const double dv = glm::length(in.plane_delta_v);
        const LandingPlaneBurn burn = landing_plane_burn(dv, in.mass, in.fuel, in.reserve, in.thrust, in.flow);
        if (!burn.valid || !burn.sufficient_fuel || burn.duration > 240.0 ||
            !std::isfinite(axis_length) || !std::isfinite(target_length) ||
            axis_length < 1e-12 || target_length < 1e-12)
            return result;
        result.supported = true;
        const sl::dvec3 pole = in.spin_axis / axis_length;
        const sl::dvec3 initial_target = in.target_direction / target_length;
        const sl::dvec3 burn_direction = dv > 0.0 ? in.plane_delta_v / dv : sl::dvec3(0.0);
        sl::dvec3 r = in.position, v = in.velocity;
        double mass = in.mass, fuel = in.fuel, time = 0.0, burn_elapsed = 0.0;
        const double step = 0.1;
        sl::dvec3 thrust_direction = dv > 0.0 ? burn_direction : glm::normalize(r);
        sl::dvec3 angular_velocity(0.0);
        const auto target_at = [&](double t) {
            return glm::angleAxis(std::remainder(in.spin_rate * t, 6.283185307179586), pole) * initial_target;
        };
        auto advance = [&](double dt, const sl::dvec3 &direction, double level) {
            const double radius = glm::length(r);
            if (!std::isfinite(radius) || radius <= in.radius || !finite(v) || mass <= 0.0)
                return false;
            const double used = in.flow * level * dt;
            if (used > fuel - in.reserve)
                return false;
            const sl::dvec3 relative_air = v - glm::cross(pole * in.spin_rate, r);
            const double density = in.atmosphere_density *
                                   std::exp(-std::max(0.0, radius - in.radius) / in.atmosphere_scale_height);
            const double thrust_ratio = 1.0 + (in.sea_level_thrust_ratio - 1.0) *
                                                 std::clamp(density / 1.225, 0.0, 1.0);
            const sl::dvec3 gravity = -in.mu * r / (radius * radius * radius);
            const sl::dvec3 drag = -0.5 * density * in.drag_area * glm::length(relative_air) * relative_air / mass;
            const sl::dvec3 acceleration = gravity + drag + direction * (in.thrust * thrust_ratio * level / mass);
            r += v * dt + acceleration * (0.5 * dt * dt);
            v += acceleration * dt;
            mass -= used;
            fuel -= used;
            time += dt;
            return finite(r) && finite(v);
        };
        while (burn_elapsed < burn.duration)
        {
            const double dt = std::min(step, burn.duration - burn_elapsed);
            if (!advance(dt, burn_direction, 1.0))
                return result;
            burn_elapsed += dt;
        }
        const double descent_start = time;
        while (time - descent_start < landing_descent_timeout_seconds)
        {
            const double radius = glm::length(r);
            if (!std::isfinite(radius) || radius <= in.radius + in.target_elevation)
                break;
            const sl::dvec3 up = r / radius;
            const sl::dvec3 target = target_at(time);
            const double distance = radius * std::acos(std::clamp(glm::dot(up, target), -1.0, 1.0));
            const sl::dvec3 surface_v = v - glm::cross(pole * in.spin_rate, r);
            const double vertical = glm::dot(surface_v, up);
            const double horizontal = glm::length(surface_v - up * vertical);
            const double altitude = radius - in.radius - in.target_elevation;
            if (landing_descent_captured(distance, altitude, horizontal, vertical))
            {
                result.captured = true;
                result.fuel = fuel;
                result.time = time;
                result.distance = distance;
                result.altitude = altitude;
                return result;
            }
            const double remaining = landing_descent_remaining_seconds(time - descent_start);
            const auto command = landing_descent_command(r, v, target, pole, in.spin_rate, in.mu,
                                                         in.radius + in.target_elevation + landing_capture_target_height_m, remaining);
            const double magnitude = glm::length(command.acceleration);
            if (!command.valid || !std::isfinite(magnitude) || magnitude < 1e-8)
                break;
            const sl::dvec3 desired = command.acceleration / magnitude;
            if (in.torque_accel > 0.0 && in.max_rate > 0.0)
            {
                sl::dvec3 axis = glm::cross(thrust_direction, desired);
                const double sine = glm::length(axis);
                const double cosine = std::clamp(glm::dot(thrust_direction, desired), -1.0, 1.0);
                sl::dvec3 wanted_rate(0.0);
                if (sine > 1e-9)
                    wanted_rate = axis / sine * std::min(std::atan2(sine, cosine) * 0.35, in.max_rate);
                else if (cosine < 0.0)
                {
                    axis = glm::cross(thrust_direction, std::abs(thrust_direction.y) < 0.9 ?
                                      sl::dvec3(0, 1, 0) : sl::dvec3(1, 0, 0));
                    wanted_rate = glm::normalize(axis) * in.max_rate;
                }
                sl::dvec3 rate_change = (wanted_rate - angular_velocity) * 2.0;
                const double rate_change_length = glm::length(rate_change);
                if (rate_change_length > 1.0)
                    rate_change /= rate_change_length;
                angular_velocity += rate_change * in.torque_accel * step;
                const double speed = glm::length(angular_velocity);
                if (speed > 1e-12)
                    thrust_direction = glm::normalize(glm::angleAxis(speed * step, angular_velocity / speed) * thrust_direction);
            }
            else
                thrust_direction = desired;
            const double level = glm::dot(thrust_direction, desired) > 0.98 ?
                                     std::clamp(magnitude * mass / in.thrust, 0.0, 1.0) : 0.0;
            if (!advance(step, thrust_direction, level))
                break;
        }
        return result;
    }
}
