#pragma once

#include <string>
#include <vector>

namespace kosmos::app
{
    struct CinematicCameraKey
    {
        double time, yaw, pitch, distance, fov;
    };

    bool load_cinematic_camera(const std::string &path, std::vector<CinematicCameraKey> &keys, std::string &error);
    CinematicCameraKey cinematic_camera_at(const std::vector<CinematicCameraKey> &keys, double time);
}
