#include "equipment.h"

#include "json.hpp"
#include "reentry_json.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace kosmos
{
    namespace
    {
        namespace fs = std::filesystem;

        glm::vec3 vec3_of(const nlohmann::json &j, const glm::vec3 &fallback)
        {
            if (!j.is_array() || j.size() < 3)
                return fallback;
            return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
        }

        EquipKind kind_of(const std::string &s)
        {
            if (s == "weapon")
                return EquipKind::weapon;
            if (s == "explosive")
                return EquipKind::explosive;
            if (s == "ordnance")
                return EquipKind::ordnance;
            return EquipKind::instrument;
        }

        EquipmentSpec parse(const nlohmann::json &j)
        {
            EquipmentSpec e;
            e.id = j.value("id", "");
            e.name = j.value("name", e.id);
            e.description = j.value("description", "");
            e.kind = kind_of(j.value("kind", "instrument"));
            e.mass = j.value("mass", e.mass);
            e.standard = j.value("standard", false);
            e.fire = j.value("fire", e.fire);
            e.rate = std::max(0.05, j.value("rate", e.rate));
            e.damage = j.value("damage", e.damage);
            e.ship_damage = j.value("ship_damage", e.ship_damage);
            e.range = j.value("range", e.range);
            e.speed = j.value("speed", e.speed);
            e.spread = j.value("spread", e.spread);
            e.ammo = j.value("ammo", e.ammo);
            e.effect = j.value("effect", e.effect);
            e.fuse = j.value("fuse", e.fuse);
            e.fuse_time = j.value("fuse_time", e.fuse_time);
            e.burst_height = j.value("burst_height", e.burst_height);
            e.prop = j.value("prop", e.prop);
            e.length = j.value("length", e.length);
            e.radius = j.value("radius", e.radius);
            e.drag_area = j.value("drag_area", e.drag_area);
            e.blast_radius = j.value("blast_radius", e.blast_radius);
            e.blast_damage = j.value("blast_damage", e.blast_damage);
            e.blast_effect = j.value("blast_effect", e.blast_effect);
            e.yield = j.value("yield", e.yield);
            e.thermal_radius = j.value("thermal_radius", e.thermal_radius);
            if (j.contains("thermal"))
                read_thermal(j["thermal"], e.thermal);
            e.ship = j.value("ship", e.ship);
            e.sensor = j.value("sensor", e.sensor);
            if (j.contains("shapes"))
                for (const nlohmann::json &js : j["shapes"])
                {
                    ViewShape s;
                    const std::string type = js.value("type", "box");
                    s.type = type == "cylinder" ? ViewShape::cylinder : type == "sphere" ? ViewShape::sphere : ViewShape::box;
                    s.position = vec3_of(js.value("position", nlohmann::json()), s.position);
                    s.size = vec3_of(js.value("size", nlohmann::json()), s.size);
                    if (js.contains("colour") && js["colour"].is_array() && js["colour"].size() >= 3)
                        for (int c = 0; c < 3; ++c)
                            s.colour[static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(std::clamp(js["colour"][static_cast<std::size_t>(c)].get<int>(), 0, 255));
                    s.glow = js.value("glow", false);
                    e.shapes.push_back(s);
                }
            return e;
        }
    } // namespace

    void EquipmentLibrary::load(const std::vector<std::string> &addon_directories, std::vector<std::string> &problems)
    {
        items_.clear();
        for (const std::string &directory : addon_directories)
        {
            const fs::path folder = fs::path(directory) / "equipment";
            std::error_code ec;
            if (!fs::is_directory(folder, ec))
                continue;
            std::vector<fs::path> files;
            for (const fs::directory_entry &entry : fs::directory_iterator(folder, ec))
                if (entry.is_regular_file() && entry.path().extension() == ".json")
                    files.push_back(entry.path());
            std::sort(files.begin(), files.end());
            for (const fs::path &file : files)
            {
                try
                {
                    std::ifstream in(file);
                    const nlohmann::json j = nlohmann::json::parse(in);
                    std::vector<nlohmann::json> entries;
                    if (j.contains("equipment"))
                        entries.assign(j["equipment"].begin(), j["equipment"].end());
                    else
                        entries.push_back(j);
                    for (const nlohmann::json &je : entries)
                    {
                        EquipmentSpec e = parse(je);
                        if (e.id.empty())
                            continue;
                        const auto same = std::find_if(items_.begin(), items_.end(), [&](const EquipmentSpec &o) { return o.id == e.id; });
                        if (same != items_.end())
                            *same = std::move(e);
                        else
                            items_.push_back(std::move(e));
                    }
                }
                catch (const std::exception &error)
                {
                    problems.push_back(file.string() + ": " + error.what());
                }
            }
        }
    }

    const EquipmentSpec *EquipmentLibrary::find(const std::string &id) const
    {
        for (const EquipmentSpec &e : items_)
            if (e.id == id)
                return &e;
        return nullptr;
    }
} // namespace kosmos
