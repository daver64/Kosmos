#include "ship_io.h"

#include "equipment.h"
#include "mesh_io.h"

#include <fstream>
#include <set>

namespace cad
{
    namespace
    {
        V3 read_v3(const json &j, const V3 &fallback)
        {
            if (!j.is_array() || j.size() < 3 || !j[0].is_number())
                return fallback;
            return {j[0].get<double>(), j[1].get<double>(), j[2].get<double>()};
        }

        json to_json(const V3 &v)
        {
            auto round = [](double x) { return std::fabs(x) < 1e-9 ? 0.0 : std::round(x * 1e6) / 1e6; };
            return json::array({round(v.x), round(v.y), round(v.z)});
        }

        bool lower_ends_with(std::string s, const std::string &suffix)
        {
            for (char &c : s)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
        }

        // Section name in a stage (engines, gear) or at the root, per marker kind.
        bool stage_section(const std::string &kind) { return kind == "engine" || kind == "gear"; }
    }

    std::string slug(const std::string &text)
    {
        std::string out;
        for (char c : text)
            out += std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : '_';
        return out.empty() ? "object" : out;
    }

    bool load_ship(const fs::path &ship_json, Scene &scene, std::string &error)
    {
        std::ifstream in(ship_json);
        if (!in)
        {
            error = "cannot read " + ship_json.string();
            return false;
        }
        json root = json::parse(in, nullptr, false);
        if (root.is_discarded() || !root.is_object())
        {
            error = "not a ship.json";
            return false;
        }
        if (!root.contains("stages") || !root["stages"].is_array())
        {
            error = "this is not a ship.json (it has no \"stages\" list)";
            return false;
        }
        Scene s;
        s.ship_file = fs::absolute(ship_json).string();
        const fs::path dir = ship_json.parent_path();
        std::string warnings;

        if (root.contains("stages") && root["stages"].is_array())
        {
            int si = 0;
            for (json &stage : root["stages"])
            {
                if (stage.contains("parts") && stage["parts"].is_array())
                {
                    json kept = json::array();
                    int pi = 0;
                    for (json &part : stage["parts"])
                    {
                        const std::string model = part.value("model", "");
                        const fs::path file = dir / model;
                        // OBJ and glTF/GLB parts open as editable meshes; saving writes them back as OBJ.
                        const bool obj = lower_ends_with(model, ".obj") || lower_ends_with(model, ".glb") || lower_ends_with(model, ".gltf");
                        const size_t before = s.objects.size();
                        std::string err;
                        if (!model.empty() && obj && fs::exists(file) && import_mesh_file(file, s, err))
                        {
                            json rest = part;
                            for (const char *k : {"model", "offset", "rotate", "scale"})
                                rest.erase(k);
                            Xform xf;
                            xf.pos = read_v3(part.value("offset", json()), {});
                            xf.rot = read_v3(part.value("rotate", json()), {});
                            if (part.contains("scale"))
                                xf.scale = part["scale"].is_array() ? read_v3(part["scale"], {1, 1, 1}) : V3(part["scale"].get<double>(), part["scale"].get<double>(), part["scale"].get<double>());
                            for (size_t i = before; i < s.objects.size(); ++i)
                            {
                                s.objects[i].xf = xf;
                                s.objects[i].stage = si;
                                s.objects[i].part = rest;
                                s.objects[i].source = model;
                            }
                        }
                        else
                        {
                            if (!model.empty())
                                warnings += "kept " + model + " as it is (" + (obj ? (err.empty() ? "missing" : err) : "not an OBJ or glTF model") + ")\n";
                            kept.push_back(part);
                        }
                        ++pi;
                    }
                    stage["parts"] = kept;
                }
                for (const char *section : {"engines", "gear"})
                {
                    if (!stage.contains(section) || !stage[section].is_array())
                        continue;
                    const std::string kind = std::string(section) == "engines" ? "engine" : "gear";
                    int i = 0;
                    for (const json &entry : stage[section])
                        s.markers.push_back(marker_from_entry(kind, entry, si, std::string(section) + ":" + std::to_string(i++)));
                    stage.erase(section);
                }
                ++si;
            }
        }
        const std::pair<const char *, const char *> root_sections[] = {{"ports", "port"}, {"vessels", "vessel"}, {"wheels", "wheel"}};
        for (const auto &[section, kind] : root_sections)
        {
            if (!root.contains(section) || !root[section].is_array())
                continue;
            int i = 0;
            for (const json &entry : root[section])
                s.markers.push_back(marker_from_entry(kind, entry, 0, std::string(section) + ":" + std::to_string(i++)));
            root.erase(section);
        }
        if (root.contains("systems") && root["systems"].is_array())
        {
            int i = 0;
            for (const json &entry : root["systems"])
                s.markers.push_back(marker_from_entry("sys:" + entry.value("type", "load"), entry, 0, "systems:" + std::to_string(i++)));
            root.erase("systems");
        }
        s.ship_root = std::move(root);
        s.ship_note = warnings;
        scene = std::move(s);
        return true;
    }

    json build_ship_json(const Scene &scene, const std::vector<std::string> &model_paths)
    {
        json root = scene.ship_root.is_object() ? scene.ship_root : json::object();
        const int stages = root.contains("stages") && root["stages"].is_array() ? static_cast<int>(root["stages"].size()) : 0;
        if (stages == 0)
            return root;
        auto stage_of = [&](int s) -> json & { return root["stages"][std::clamp(s, 0, stages - 1)]; };

        for (size_t oi = 0; oi < scene.objects.size(); ++oi)
        {
            const Object &o = scene.objects[oi];
            if (o.mesh.empty() || oi >= model_paths.size() || model_paths[oi].empty())
                continue;
            json part = o.part.is_object() ? o.part : json::object();
            json out = json::object();
            out["model"] = model_paths[oi];
            if (length(o.xf.pos) > 1e-9)
                out["offset"] = to_json(o.xf.pos);
            if (length(o.xf.rot) > 1e-9)
                out["rotate"] = to_json(o.xf.rot);
            if (std::fabs(o.xf.scale.x - 1) > 1e-9 || std::fabs(o.xf.scale.y - 1) > 1e-9 || std::fabs(o.xf.scale.z - 1) > 1e-9)
                out["scale"] = (o.xf.scale.x == o.xf.scale.y && o.xf.scale.y == o.xf.scale.z) ? json(o.xf.scale.x) : to_json(o.xf.scale);
            for (auto it = part.begin(); it != part.end(); ++it)
                out[it.key()] = it.value();
            json &stage = stage_of(o.stage);
            if (!stage.contains("parts") || !stage["parts"].is_array())
                stage["parts"] = json::array();
            // An OBJ with several named objects imports as several objects that all point at the same model file.
            bool duplicate = false;
            for (const json &existing : stage["parts"])
                if (existing == out)
                    duplicate = true;
            if (!duplicate)
                stage["parts"].push_back(std::move(out));
        }

        for (const Marker &m : scene.markers)
        {
            const json entry = entry_from_marker(m);
            if (stage_section(m.kind))
            {
                json &stage = stage_of(m.stage);
                const char *section = m.kind == "engine" ? "engines" : "gear";
                if (!stage.contains(section) || !stage[section].is_array())
                    stage[section] = json::array();
                stage[section].push_back(entry);
                continue;
            }
            const char *section = m.kind == "port" ? "ports" : (m.kind == "vessel" ? "vessels" : (m.kind == "wheel" ? "wheels" : "systems"));
            if (!root.contains(section) || !root[section].is_array())
                root[section] = json::array();
            root[section].push_back(entry);
        }
        return root;
    }

    bool save_ship(const Scene &scene, const fs::path &ship_json, std::string &error)
    {
        const fs::path dir = ship_json.parent_path();
        const fs::path models = dir / "models";
        std::vector<std::string> paths(scene.objects.size());
        std::set<std::string> used;
        for (size_t oi = 0; oi < scene.objects.size(); ++oi)
        {
            const Object &o = scene.objects[oi];
            if (o.mesh.empty())
                continue;
            std::string base = "cad_" + slug(o.name);
            for (int n = 2; used.count(base); ++n)
                base = "cad_" + slug(o.name) + "_" + std::to_string(n);
            used.insert(base);
            if (!export_obj(scene, models, base, error, {static_cast<int>(oi)}, false))
                return false;
            paths[oi] = "models/" + base + ".obj";
        }
        const json root = build_ship_json(scene, paths);
        std::ofstream out(ship_json);
        if (!out)
        {
            error = "cannot write " + ship_json.string();
            return false;
        }
        out << root.dump(1) << '\n';
        return static_cast<bool>(out);
    }
}
