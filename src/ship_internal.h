#pragma once

// Helpers shared by the ship_*.cpp files; not part of the public Ship interface.

#include "ship.h"

namespace kosmos::ship_detail
{
    inline constexpr double pi = 3.14159265358979323846;
    inline constexpr double g0 = 9.80665;

    // Rotation whose nose (-Z) points along `nose` and whose roof (+Y) is as close to `roof` as possible.
    inline sl::dquat orientation_from(const sl::dvec3 &nose, const sl::dvec3 &roof)
    {
        const sl::dvec3 z = -glm::normalize(nose);
        sl::dvec3 x = glm::cross(roof, z);
        if (glm::length(x) < 1e-9)
            x = glm::cross(sl::dvec3(0.0, 0.0, 1.0), z);
        x = glm::normalize(x);
        const sl::dvec3 y = glm::cross(z, x);
        return glm::normalize(glm::quat_cast(sl::dmat3(x, y, z)));
    }

    inline double atmosphere_top(const Body &body)
    {
        return body.atmosphere_density > 0.0 ? body.atmosphere_scale_height * 10.0 : 0.0;
    }

    // The turn of a planet's ground about its pole since time zero, the same one the physics world applies to the terrain.
    inline sl::dquat ground_spin(const Universe &universe, int id)
    {
        const sl::FlightBody *planet = universe.world.body(id);
        return glm::angleAxis(planet->spin_phase + planet->spin_rate * universe.time(), glm::normalize(planet->spin_axis));
    }

    // Bits of the RCS control groups; see EngineSpec::rcs_groups.
    enum RcsGroup : unsigned
    {
        g_left = 1, g_right = 2, g_up = 4, g_down = 8, g_forward = 16, g_back = 32,
        g_pitch_up = 64, g_pitch_down = 128, g_yaw_left = 256, g_yaw_right = 512, g_bank_left = 1024, g_bank_right = 2048
    };

    inline unsigned rcs_group_bit(const std::string &name)
    {
        static const char *names[] = {"left", "right", "up", "down", "forward", "back", "pitch-up", "pitch-down", "yaw-left", "yaw-right", "bank-left", "bank-right"};
        for (unsigned i = 0; i < 12; ++i)
            if (name == names[i])
                return 1u << i;
        return 0;
    }
} // namespace kosmos::ship_detail
