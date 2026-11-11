#pragma once

#include "scenario.h"

#include <array>
#include <string>
#include <vector>

namespace kosmos
{
    inline constexpr std::array<const char *, 8> flight_recorded_axes = {
        "pitch", "yaw", "roll", "rcs-x", "rcs-y", "rcs-z", "pod-left", "pod-right"};
    struct FlightRecordingFrame
    {
        double elapsed = 0.0;
        Scenario scenario;
        double throttle = 0.0, hover = 0.0;
        std::array<double, 8> axes{};
        std::string sas = "stability";
        double camera_yaw = 0.0, camera_pitch = 0.0, camera_roll = 0.0, camera_distance = 0.0;
        bool cockpit = false, cockpit3d = false;
    };

    struct FlightRecording
    {
        std::vector<FlightRecordingFrame> frames;
    };

    bool save_flight_recording(const std::string &path, const FlightRecording &recording, std::string &error);
    bool load_flight_recording(const std::string &path, FlightRecording &recording, std::string &error);
}
