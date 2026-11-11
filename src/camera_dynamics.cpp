#include "camera_dynamics.h"

#include <algorithm>
#include <cmath>

namespace kosmos
{
    sl::dvec3 orbit_camera_view_up(const sl::dvec3 &up, const sl::dvec3 &east,
                                   const sl::dvec3 &forward, double roll)
    {
        sl::dvec3 vertical = up - forward * glm::dot(up, forward);
        if (glm::length(vertical) < 1.0e-9)
            vertical = east - forward * glm::dot(east, forward);
        vertical = glm::normalize(vertical);
        return vertical * std::cos(roll) + glm::cross(forward, vertical) * std::sin(roll);
    }

    void reframe_orbit_camera(const sl::dvec3 &old_up, const sl::dvec3 &old_east,
                              const sl::dvec3 &new_up, const sl::dvec3 &new_east,
                              double &yaw, double &pitch, double &roll)
    {
        const sl::dvec3 old_north = glm::cross(old_up, old_east);
        const sl::dvec3 offset = (old_east * std::sin(yaw) + old_north * std::cos(yaw)) * std::cos(pitch) +
                                 old_up * std::sin(pitch);
        const sl::dvec3 forward = -offset;
        const sl::dvec3 old_view_up = orbit_camera_view_up(old_up, old_east, forward, roll);
        const sl::dvec3 new_view_up = orbit_camera_view_up(new_up, new_east, forward, 0.0);
        roll = std::atan2(glm::dot(forward, glm::cross(new_view_up, old_view_up)),
                          glm::dot(new_view_up, old_view_up));
        const sl::dvec3 new_north = glm::cross(new_up, new_east);
        const double vertical = std::clamp(glm::dot(offset, new_up), -1.0, 1.0);
        const double east = glm::dot(offset, new_east);
        const double north = glm::dot(offset, new_north);
        pitch = std::atan2(vertical, std::hypot(east, north));
        if (std::hypot(east, north) > 1.0e-12)
            yaw = std::atan2(east, north);
    }

    namespace
    {
        // Washout time constant for sustained acceleration, in seconds.
        constexpr double washout = 4.0;
        // Looser fore-aft and up-down than side to side, as a head is.
        const double axis_frequency[3] = {1.15, 1.0, 0.9};
        constexpr double max_substep = 1.0 / 240.0;
    } // namespace

    void CameraDynamics::reset()
    {
        x_ = v_ = baseline_ = sl::dvec3(0.0);
        primed_ = false;
    }

    void CameraDynamics::relax(double dt, const HeadMotion &motion)
    {
        primed_ = false;
        step(dt, baseline_, motion);
        primed_ = false;
    }

    void CameraDynamics::step(double dt, const sl::dvec3 &felt, const HeadMotion &motion)
    {
        if (dt <= 0.0)
            return;
        if (!primed_)
        {
            baseline_ = felt;
            primed_ = true;
        }
        baseline_ += (felt - baseline_) * (1.0 - std::exp(-dt / washout));
        const sl::dvec3 a = felt - baseline_;
        const int n = std::max(1, static_cast<int>(std::ceil(dt / max_substep)));
        const double h = dt / n;
        for (int i = 0; i < n; ++i)
            for (int k = 0; k < 3; ++k)
            {
                const double w = 2.0 * 3.14159265358979323846 * std::max(0.1, motion.frequency) * axis_frequency[k];
                // The eye is pushed the opposite way to the ship's acceleration; semi-implicit Euler stays stable for any damping.
                v_[k] += (-a[k] - w * w * x_[k] - 2.0 * motion.damping * w * v_[k]) * h;
                x_[k] += v_[k] * h;
            }
    }

    sl::dvec3 CameraDynamics::offset(const HeadMotion &motion) const
    {
        const double limit = std::max(0.001, motion.max_offset);
        sl::dvec3 out(0.0);
        for (int k = 0; k < 3; ++k)
            out[k] = limit * std::tanh(motion.strength * x_[k] / limit);
        return out;
    }
} // namespace kosmos
