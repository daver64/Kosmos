#include "weapon_specs.h"

#include "json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace kosmos
{
    namespace
    {
        namespace fs = std::filesystem;

        void colour_of(const nlohmann::json &j, const char *key, int out[3])
        {
            if (!j.contains(key) || !j[key].is_array() || j[key].size() < 3)
                return;
            for (std::size_t i = 0; i < 3; ++i)
                out[i] = std::clamp(j[key][i].get<int>(), 0, 255);
        }

        WeaponSpec parse(const nlohmann::json &j)
        {
            WeaponSpec w;
            w.id = j.value("id", "");
            w.name = j.value("name", w.id);
            w.fire = j.value("fire", w.fire);
            if (w.fire != "pulse" && w.fire != "beam" && w.fire != "bolt" && w.fire != "flak")
                throw std::runtime_error("weapon '" + w.id + "': fire must be pulse, beam, bolt or flak");
            w.rate = std::clamp(j.value("rate", w.rate), 0.05, 60.0);
            w.ship_damage = std::max(0.0, j.value("ship_damage", w.ship_damage));
            w.range = std::clamp(j.value("range", w.range), 10.0, 5.0e6);
            w.speed = std::clamp(j.value("speed", w.speed), 10.0, 3.0e5);
            w.spread = std::clamp(j.value("spread", w.spread), 0.0, 0.5);
            w.count = std::clamp(j.value("count", w.count), 1, 64);
            w.cone = std::clamp(j.value("cone", w.cone), 0.0, 1.0);
            w.duration = std::clamp(j.value("duration", w.duration), 0.02, 5.0);
            w.max_time = std::max(0.1, j.value("max_time", w.max_time));
            w.cooldown = std::max(0.0, j.value("cooldown", w.cooldown));
            w.lifetime = std::max(0.0, j.value("lifetime", w.lifetime));
            w.impact = j.value("impact", w.impact);
            w.impact_scale = std::clamp(j.value("impact_scale", w.impact_scale), 0.1, 50.0);
            if (j.contains("look"))
            {
                const nlohmann::json &l = j["look"];
                colour_of(l, "colour", w.look.colour);
                colour_of(l, "core", w.look.core);
                w.look.width = std::clamp(l.value("width", w.look.width), 0.01, 100.0);
                w.look.glow = std::clamp(l.value("glow", w.look.glow), 1.0, 20.0);
                w.look.length = std::clamp(l.value("length", w.look.length), 0.1, 500.0);
                w.look.sparkle = std::clamp(l.value("sparkle", w.look.sparkle), 0.0, 1.0);
                w.look.light = std::clamp(l.value("light", w.look.light), 0.0, 20.0);
                w.look.light_range = std::clamp(l.value("light_range", w.look.light_range), 1.0, 5000.0);
            }
            return w;
        }
    } // namespace

    void WeaponLibrary::load(const std::vector<std::string> &addon_directories, std::vector<std::string> &problems)
    {
        items_.clear();
        for (const std::string &directory : addon_directories)
        {
            const fs::path folder = fs::path(directory) / "weapons";
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
                    if (j.contains("weapons"))
                        entries.assign(j["weapons"].begin(), j["weapons"].end());
                    else
                        entries.push_back(j);
                    for (const nlohmann::json &je : entries)
                    {
                        WeaponSpec w = parse(je);
                        if (w.id.empty())
                            continue;
                        const auto same = std::find_if(items_.begin(), items_.end(), [&](const WeaponSpec &o) { return o.id == w.id; });
                        if (same != items_.end())
                            *same = std::move(w);
                        else
                            items_.push_back(std::move(w));
                    }
                }
                catch (const std::exception &error)
                {
                    problems.push_back(file.string() + ": " + error.what());
                }
            }
        }
    }

    const WeaponSpec *WeaponLibrary::find(const std::string &id) const
    {
        for (const WeaponSpec &w : items_)
            if (w.id == id)
                return &w;
        return nullptr;
    }
} // namespace kosmos
