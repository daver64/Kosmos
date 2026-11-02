#pragma once

// Constants and small helpers shared across the application files.

#include <limits>
#include <string>

namespace kosmos::app
{
    // Returned by the start-up steps when the run carries on; any other value is the process exit code.
    constexpr int keep_going = std::numeric_limits<int>::min();

    constexpr double km = 1000.0;

    constexpr double pi = 3.14159265358979323846;

    extern int screen_w;

    extern int screen_h;

    constexpr double cloud_height = 60.0e3;

    const char *const sas_modes[] = {"stability", "prograde", "retrograde", "radial-out", "radial-in", "normal", "antinormal", "off"};

    constexpr int sas_mode_count = sizeof(sas_modes) / sizeof(sas_modes[0]);

    std::string format_time(double seconds);

    std::string format_distance(double metres);
} // namespace kosmos::app
