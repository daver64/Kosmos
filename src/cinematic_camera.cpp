#include "cinematic_camera.h"

#include "json.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <utility>

namespace kosmos::app
{
    bool load_cinematic_camera(const std::string &path, std::vector<CinematicCameraKey> &keys, std::string &error)
    {
        std::ifstream in(path);
        if (!in)
        {
            error = "cannot open camera timeline " + path;
            return false;
        }
        const auto json = nlohmann::json::parse(in, nullptr, false);
        if (in.bad() || !json.is_array() || json.empty())
        {
            error = "camera timeline must be a non-empty JSON array: " + path;
            return false;
        }
        std::vector<CinematicCameraKey> loaded;
        for (const auto &entry : json)
        {
            if (!entry.is_object())
            {
                error = "camera key must be an object: " + path;
                return false;
            }
            for (const char *name : {"time", "yaw", "pitch", "distance", "fov"})
                if (!entry.contains(name) || !entry[name].is_number())
                {
                    error = std::string("camera key requires numeric ") + name + ": " + path;
                    return false;
                }
            CinematicCameraKey key{entry["time"].get<double>(), entry["yaw"].get<double>(),
                                   entry["pitch"].get<double>(), entry["distance"].get<double>(),
                                   entry["fov"].get<double>()};
            if (!std::isfinite(key.time) || !std::isfinite(key.yaw) || !std::isfinite(key.pitch) ||
                !std::isfinite(key.distance) || !std::isfinite(key.fov) || key.time < 0.0 ||
                key.pitch < -89.0 || key.pitch > 89.0 || key.distance < 8.0 || key.distance > 3.0e9 ||
                key.fov < 10.0 || key.fov > 120.0 || (!loaded.empty() && key.time <= loaded.back().time))
            {
                error = "invalid or out-of-order camera key: " + path;
                return false;
            }
            if (!loaded.empty())
                key.yaw = loaded.back().yaw + std::remainder(key.yaw - loaded.back().yaw, 360.0);
            loaded.push_back(key);
        }
        keys = std::move(loaded);
        return true;
    }

    CinematicCameraKey cinematic_camera_at(const std::vector<CinematicCameraKey> &keys, double time)
    {
        const auto next = std::upper_bound(keys.begin(), keys.end(), time,
                                           [](double t, const CinematicCameraKey &k) { return t < k.time; });
        if (next == keys.begin())
            return keys.front();
        if (next == keys.end())
            return keys.back();
        const CinematicCameraKey &a = *(next - 1), &b = *next;
        const double t = (time - a.time) / (b.time - a.time);
        const double w = t * t * (3.0 - 2.0 * t);
        const auto blend = [w](double x, double y) { return x + (y - x) * w; };
        return {time, blend(a.yaw, b.yaw), blend(a.pitch, b.pitch),
                blend(a.distance, b.distance), blend(a.fov, b.fov)};
    }
}
