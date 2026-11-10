#pragma once

#include "surface3d.h"
#include "proptree.h"

#include "json.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace kosmos
{
    /**
     * A lamp on a ship or at a base. Positions are in the owner's frame: the ship frame (nose along -Z, +X to the right),
     * or the base frame (x right, z forward, y up, metres). A spot light points along `direction`; at a base it can
     * instead aim at a point (`aim`) or at the nearest ship (`aim_ship`).
     */
    struct LightSpec
    {
        double x = 0.0, y = 0.0, z = 0.0;
        int count = 1; // copies for a row of lamps
        double step_x = 0.0, step_y = 0.0, step_z = 0.0;
        sl::Colour colour{255, 240, 205, 255};
        float intensity = 1.0f;
        float range = 40.0f;
        // Spot: a zero outer angle makes a point light.
        float inner = 0.0f, outer = 0.0f;
        double dx = 0.0, dy = -1.0, dz = 0.0;
        bool has_aim = false, aim_ship = false;
        double ax = 0.0, ay = 0.0, az = 0.0;
        /** steady, strobe (two sharp flashes), beacon (slow pulse) or blink (on and off). */
        std::string pattern = "steady";
        double period = 1.5, phase = 0.0;
        /** Size of the visible bulb glow in metres; 0 hides it. */
        float glow = 0.4f;
        /** Keep a lamp eligible by day as well as at night. */
        bool always = false;
        bool shadow = false;
        /** Ship navigation lights (running lights and strobes) are drawn subdued. Defaults to on for point lamps; spots are never navigation. */
        bool navigation = false;
        /** Optional property controlling a ship lamp independently of the navigation or landing lights. */
        std::string control;
        /** Ships: lit only while this stage has not been jettisoned (-1 for always). */
        int stage = -1;
    };

    inline bool ship_light_enabled(const LightSpec &light, const sl::PropertyTree &props, bool daylight_on_ground = false)
    {
        if (daylight_on_ground && !light.always)
            return false;
        const std::string path = light.control.empty()
            ? (light.navigation ? "/controls/lighting/nav" : "/controls/lighting/lamps")
            : light.control;
        return props.get_bool(path, !light.navigation);
    }

    /** How bright a lamp is at a moment, 0 to 1, from its blink pattern. */
    inline float light_pattern(const LightSpec &l, double time)
    {
        if (l.pattern == "steady")
            return 1.0f;
        const double p = std::fmod(time / std::max(0.05, l.period) + l.phase, 1.0);
        const double u = p < 0.0 ? p + 1.0 : p;
        if (l.pattern == "strobe")
            return (u < 0.04 || (u > 0.14 && u < 0.18)) ? 1.0f : 0.0f;
        if (l.pattern == "beacon")
            return static_cast<float>(0.5 - 0.5 * std::cos(u * 6.283185307179586));
        return u < 0.5 ? 1.0f : 0.0f;
    }

    inline LightSpec parse_light(const nlohmann::json &j)
    {
        LightSpec l;
        const auto vec = [&](const char *key, double &a, double &b, double &c)
        {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 3)
            {
                a = j[key][0].get<double>();
                b = j[key][1].get<double>();
                c = j[key][2].get<double>();
                return true;
            }
            return false;
        };
        if (!vec("position", l.x, l.y, l.z))
        {
            l.x = j.value("x", l.x);
            l.y = j.value("y", l.y);
            l.z = j.value("z", l.z);
        }
        l.count = std::max(1, j.value("count", 1));
        l.step_x = j.value("step_x", 0.0);
        l.step_y = j.value("step_y", 0.0);
        l.step_z = j.value("step_z", 0.0);
        if (j.contains("colour") && j["colour"].is_array() && j["colour"].size() >= 3)
            l.colour = {static_cast<std::uint8_t>(j["colour"][0].get<int>()), static_cast<std::uint8_t>(j["colour"][1].get<int>()),
                        static_cast<std::uint8_t>(j["colour"][2].get<int>()), 255};
        l.intensity = j.value("intensity", l.intensity);
        l.range = j.value("range", l.range);
        l.inner = j.value("inner", l.inner);
        l.outer = j.value("outer", l.outer);
        vec("direction", l.dx, l.dy, l.dz);
        if (j.contains("aim") && j["aim"].is_string())
            l.aim_ship = j["aim"].get<std::string>() == "ship";
        else if (j.contains("aim"))
            l.has_aim = vec("aim", l.ax, l.ay, l.az);
        if (j.contains("aim_point"))
            l.has_aim = vec("aim_point", l.ax, l.ay, l.az);
        l.pattern = j.value("pattern", l.pattern);
        l.period = j.value("period", l.period);
        l.phase = j.value("phase", l.phase);
        l.glow = j.value("glow", l.glow);
        l.always = j.value("always", false);
        l.shadow = j.value("shadow", false);
        l.navigation = j.value("navigation", l.outer <= 0.0f);
        l.control = j.value("control", std::string());
        l.stage = j.value("stage", -1);
        return l;
    }

    inline nlohmann::json light_to_json(const LightSpec &l)
    {
        nlohmann::json j = {{"x", l.x}, {"y", l.y}, {"z", l.z}};
        const LightSpec d;
        if (l.count != 1)
            j["count"] = l.count;
        if (l.step_x != 0.0) j["step_x"] = l.step_x;
        if (l.step_y != 0.0) j["step_y"] = l.step_y;
        if (l.step_z != 0.0) j["step_z"] = l.step_z;
        j["colour"] = {static_cast<int>(l.colour.red), static_cast<int>(l.colour.green), static_cast<int>(l.colour.blue)};
        if (l.intensity != d.intensity) j["intensity"] = l.intensity;
        if (l.range != d.range) j["range"] = l.range;
        if (l.outer > 0.0f)
        {
            j["inner"] = l.inner;
            j["outer"] = l.outer;
        }
        if (l.aim_ship)
            j["aim"] = "ship";
        if (l.has_aim)
            j["aim_point"] = {l.ax, l.ay, l.az};
        if (!l.aim_ship && !l.has_aim && l.outer > 0.0f)
            j["direction"] = {l.dx, l.dy, l.dz};
        if (l.pattern != d.pattern) j["pattern"] = l.pattern;
        if (l.period != d.period) j["period"] = l.period;
        if (l.phase != d.phase) j["phase"] = l.phase;
        if (l.glow != d.glow) j["glow"] = l.glow;
        if (l.always) j["always"] = true;
        if (l.shadow) j["shadow"] = true;
        if (l.navigation != (l.outer <= 0.0f)) j["navigation"] = l.navigation;
        if (!l.control.empty()) j["control"] = l.control;
        if (l.stage >= 0) j["stage"] = l.stage;
        return j;
    }

    /** A lamp placed in the camera-relative world, ready to hand to simlib and to draw as a glow. */
    struct PlacedLight
    {
        sl::LocalLight3D light;
        glm::vec3 glow_colour{1.0f};
        float glow_size = 0.0f; // metres; 0 for none
        float glow_strength = 0.0f;
        float distance = 0.0f;
        float far_size = 0.007f; // minimum glow size as a fraction of distance
    };
}
