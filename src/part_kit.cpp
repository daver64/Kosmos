#include "part_kit.h"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace kosmos
{
    namespace
    {
        constexpr double pi = 3.14159265358979323846;

        struct Placement
        {
            double z = 0.0;      // where the kit's bottom node sits on the ship's Z axis
            double radial = 0.0; // distance from the axis for strap-ons
            double angle = 0.0;  // degrees about the axis
            double scale = 1.0;
        };

        nlohmann::json place_point(const nlohmann::json &p, const Placement &at)
        {
            const double x = p[0].get<double>() * at.scale + at.radial, y = p[1].get<double>() * at.scale, z = p[2].get<double>() * at.scale + at.z;
            const double c = std::cos(at.angle * pi / 180.0), s = std::sin(at.angle * pi / 180.0);
            return nlohmann::json::array({x * c - y * s, x * s + y * c, z});
        }

        nlohmann::json place_direction(const nlohmann::json &d, const Placement &at)
        {
            const double x = d[0].get<double>(), y = d[1].get<double>();
            const double c = std::cos(at.angle * pi / 180.0), s = std::sin(at.angle * pi / 180.0);
            return nlohmann::json::array({x * c - y * s, x * s + y * c, d[2].get<double>()});
        }

        // Positions and directions in a kit entry follow it into the stack; everything else is copied unchanged.
        nlohmann::json place_entry(nlohmann::json entry, const Placement &at, bool is_part)
        {
            if (entry.is_array() && entry.size() >= 3)
                return place_point(entry, at);
            if (!entry.is_object())
                return entry;
            if (entry.contains("position"))
                entry["position"] = place_point(entry["position"], at);
            else if (entry.contains("x") && entry.contains("z"))
            {
                const nlohmann::json p = place_point(nlohmann::json::array({entry.value("x", 0.0), entry.value("y", 0.0), entry.value("z", 0.0)}), at);
                entry["x"] = p[0];
                entry["y"] = p[1];
                entry["z"] = p[2];
            }
            if (entry.contains("direction"))
                entry["direction"] = place_direction(entry["direction"], at);
            if (is_part)
            {
                // A procedural prop is placed by its own z and offset; the stack position joins the offset.
                const nlohmann::json off = place_point(entry.value("offset", nlohmann::json::array({0.0, 0.0, 0.0})), {at.z, at.radial, at.angle, 1.0});
                entry["offset"] = off;
                if (entry.contains("scale") && at.scale != 1.0)
                    entry["scale"] = at.scale;
            }
            else if (entry.contains("step_x"))
            {
                for (const char *k : {"step_x", "step_y", "step_z"})
                    entry[k] = entry.value(k, 0.0) * at.scale;
            }
            return entry;
        }
    } // namespace

    std::filesystem::path find_part_kit(const std::filesystem::path &addon_dir, const std::string &name)
    {
        namespace fs = std::filesystem;
        std::string kit = name;
        fs::path addons = addon_dir.parent_path();
        fs::path home = addon_dir;
        const std::size_t colon = name.find(':');
        if (colon != std::string::npos)
        {
            home = addons / name.substr(0, colon);
            kit = name.substr(colon + 1);
        }
        for (const fs::path &base : {home / "parts", addons / "parts", addons.parent_path() / "data" / "parts"})
        {
            const fs::path file = base / kit / "part.json";
            std::error_code ec;
            if (fs::exists(file, ec))
                return file;
        }
        return {};
    }

    bool expand_part_kits(nlohmann::json &ship, const std::filesystem::path &addon_dir, std::string &error)
    {
        if (!ship.contains("stages") || !ship["stages"].is_array())
            return true;
        double cursor = ship.value("stack_z", 0.0);
        for (nlohmann::json &stage : ship["stages"])
        {
            if (!stage.contains("kits"))
                continue;
            cursor = stage.value("stack_z", cursor);
            const double base = cursor;
            double dry = stage.value("dry_mass", 0.0), propellant = stage.value("propellant", 0.0), radius = 0.0;
            for (const nlohmann::json &item : stage["kits"])
            {
                const std::string name = item.is_string() ? item.get<std::string>() : item.value("kit", "");
                const std::filesystem::path file = find_part_kit(addon_dir, name);
                if (file.empty())
                {
                    error = "part kit '" + name + "' not found (looked in parts/ folders)";
                    return false;
                }
                nlohmann::json kit;
                {
                    std::ifstream in(file);
                    if (!in)
                    {
                        error = "cannot open " + file.string();
                        return false;
                    }
                    try
                    {
                        in >> kit;
                    }
                    catch (const std::exception &e)
                    {
                        error = file.string() + ": " + e.what();
                        return false;
                    }
                }
                const double scale = item.is_object() ? item.value("scale", 1.0) : 1.0;
                const double length = kit.value("length", 1.0) * scale, kit_radius = kit.value("radius", 1.0) * scale;
                const double radial = item.is_object() ? item.value("radial", 0.0) : 0.0;
                const int count = radial > 0.0 ? std::max(1, item.value("count", 1)) : 1;
                const double first = item.is_object() ? item.value("angle", 0.0) : 0.0;
                for (int i = 0; i < count; ++i)
                {
                    Placement at{cursor, radial, first + 360.0 * i / count, scale};
                    dry += kit.value("dry_mass", 0.0);
                    propellant += kit.value("propellant", 0.0);
                    if (kit.contains("model"))
                    {
                        nlohmann::json part = {{"model", (file.parent_path() / kit["model"].get<std::string>()).generic_string()}};
                        part["offset"] = place_point(kit.value("offset", nlohmann::json::array({0.0, 0.0, 0.0})), at);
                        if (scale != 1.0 || kit.contains("scale"))
                        {
                            const double ks = kit.value("scale", 1.0) * scale;
                            part["scale"] = nlohmann::json::array({ks, ks, ks});
                        }
                        part["rotate"] = nlohmann::json::array({0.0, 0.0, at.angle});
                        stage["parts"].push_back(part);
                    }
                    for (const nlohmann::json &p : kit.value("parts", nlohmann::json::array()))
                        stage["parts"].push_back(place_entry(p, at, true));
                    for (const char *key : {"engines", "lights", "ports"})
                    {
                        nlohmann::json &target = std::string(key) == "engines" ? stage["engines"] : ship[key];
                        for (const nlohmann::json &e : kit.value(key, nlohmann::json::array()))
                            target.push_back(place_entry(e, at, false));
                    }
                    for (const nlohmann::json &g : kit.value("gear", nlohmann::json::array()))
                        stage["gear"].push_back(place_point(g, at));
                }
                radius = std::max(radius, radial > 0.0 ? radial + kit_radius : kit_radius);
                if (radial <= 0.0)
                    cursor -= length + (item.is_object() ? item.value("gap", 0.0) : 0.0);
            }
            stage["dry_mass"] = dry;
            stage["propellant"] = propellant;
            if (!stage.contains("length"))
                stage["length"] = std::max(base - cursor, 0.1);
            if (!stage.contains("z"))
                stage["z"] = 0.5 * (base + cursor);
            if (!stage.contains("radius"))
                stage["radius"] = std::max(radius, 0.1);
            stage.erase("kits");
        }
        return true;
    }
}
