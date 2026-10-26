#pragma once

#include "json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace kosmos
{
    /**
     * A camera the player sets up from the Camera editor. An orbit camera circles what it is attached to and always looks at it;
     * a mounted camera sits at an offset on it and looks along a fixed direction, or at something else.
     */
    struct UserCamera
    {
        enum Attach { flown_ship = 0, other_ship = 1, body = 2 };
        enum LookAt { look_ahead = 0, look_flown_ship = 1, look_ship = 2, look_body = 3 };

        std::string name = "Camera";
        bool orbit = true;
        int attach = flown_ship;
        std::string attach_name;
        double fov = 50.0;

        /** Orbit: degrees round and above the horizon, metres away (0 frames the object), and degrees per second of spin. */
        double yaw = 40.0, pitch = 15.0, distance = 0.0, spin = 0.0;

        /** Mounted: metres right, up and back from the object's centre (a ship's own axes; a body's east, up and south), the way it faces in degrees, and what it looks at instead. */
        double x = 0.0, y = 0.0, z = 0.0, look_yaw = 0.0, look_pitch = 0.0;
        int look_at = look_ahead;
        std::string look_name;
    };

    inline std::string user_camera_path(const std::string &user_dir)
    {
        return user_dir.empty() ? std::string() : (std::filesystem::path(user_dir) / "cameras.json").string();
    }

    inline std::vector<UserCamera> default_user_cameras()
    {
        UserCamera external;
        external.name = "External";
        return {external};
    }

    inline std::vector<UserCamera> load_user_cameras(const std::string &path)
    {
        std::ifstream in(path);
        if (path.empty() || !in)
            return default_user_cameras();
        nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
        if (!j.is_array())
            return default_user_cameras();
        std::vector<UserCamera> out;
        for (const nlohmann::json &e : j)
        {
            if (!e.is_object())
                continue;
            UserCamera c;
            c.name = e.value("name", c.name);
            c.orbit = e.value("orbit", c.orbit);
            c.attach = std::clamp(e.value("attach", c.attach), 0, 2);
            c.attach_name = e.value("attach_name", c.attach_name);
            c.fov = std::clamp(e.value("fov", c.fov), 5.0, 120.0);
            c.yaw = e.value("yaw", c.yaw);
            c.pitch = e.value("pitch", c.pitch);
            c.distance = std::max(0.0, e.value("distance", c.distance));
            c.spin = e.value("spin", c.spin);
            c.x = e.value("x", c.x);
            c.y = e.value("y", c.y);
            c.z = e.value("z", c.z);
            c.look_yaw = e.value("look_yaw", c.look_yaw);
            c.look_pitch = e.value("look_pitch", c.look_pitch);
            c.look_at = std::clamp(e.value("look_at", c.look_at), 0, 3);
            c.look_name = e.value("look_name", c.look_name);
            out.push_back(c);
        }
        return out;
    }

    inline void save_user_cameras(const std::string &path, const std::vector<UserCamera> &cameras)
    {
        if (path.empty())
            return;
        nlohmann::json j = nlohmann::json::array();
        for (const UserCamera &c : cameras)
            j.push_back({{"name", c.name}, {"orbit", c.orbit}, {"attach", c.attach}, {"attach_name", c.attach_name}, {"fov", c.fov},
                         {"yaw", c.yaw}, {"pitch", c.pitch}, {"distance", c.distance}, {"spin", c.spin},
                         {"x", c.x}, {"y", c.y}, {"z", c.z}, {"look_yaw", c.look_yaw}, {"look_pitch", c.look_pitch},
                         {"look_at", c.look_at}, {"look_name", c.look_name}});
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
        std::ofstream out(path);
        if (out)
            out << j.dump(1) << "\n";
    }
}
