#include <ctime>
#include "model.h"
#include "../../../src/part_kit.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <set>

namespace shipedit
{
    namespace
    {
        constexpr double g0 = 9.80665;
        constexpr std::size_t undo_limit = 200;

        bool read_json(const fs::path &file, json &out, std::string &error)
        {
            std::ifstream in(file);
            if (!in)
            {
                error = "cannot open " + file.string();
                return false;
            }
            try
            {
                in >> out;
            }
            catch (const std::exception &e)
            {
                error = file.filename().string() + ": " + e.what();
                return false;
            }
            return true;
        }

        void shift_z(json &value, double dz)
        {
            if (value.is_array() && value.size() >= 3 && value[2].is_number())
                value[2] = value[2].get<double>() + dz;
        }

        void shift_contents(json &stage, double dz)
        {
            for (json &part : stage["parts"])
            {
                if (part.contains("model"))
                {
                    if (!part.contains("offset"))
                        part["offset"] = json::array({0.0, 0.0, 0.0});
                    shift_z(part["offset"], dz);
                }
                else
                    part["z"] = part.value("z", 0.0) + dz;
            }
            for (json &engine : stage["engines"])
            {
                if (!engine.contains("position"))
                    engine["position"] = json::array({0.0, 0.0, 0.0});
                shift_z(engine["position"], dz);
            }
            for (json &gear : stage["gear"])
                shift_z(gear, dz);
            if (stage.contains("rcs_visual") && stage["rcs_visual"].is_object())
                for (const char *key : {"front_z", "rear_z"})
                    if (stage["rcs_visual"].contains(key) && stage["rcs_visual"][key].is_number())
                        stage["rcs_visual"][key] = stage["rcs_visual"][key].get<double>() + dz;
        }

        bool under(const fs::path &file, const fs::path &base, fs::path &relative)
        {
            std::error_code ec;
            const fs::path canonical = fs::canonical(file, ec);
            if (ec)
                return false;
            relative = canonical.lexically_relative(base);
            return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
        }

        struct StageAssets
        {
            fs::path root;
            std::string prefix;
            std::map<std::string, fs::path> files;
            std::string warning;

            bool add(const fs::path &source, std::string &relative, std::string &error, bool required = true)
            {
                std::error_code ec;
                if (!fs::is_regular_file(source, ec))
                {
                    if (required)
                    {
                        error = "Missing stage asset: " + source.string();
                        return false;
                    }
                    warning += "\nMissing optional model asset: " + source.string();
                    return true;
                }
                fs::path rel;
                if (!under(source, root, rel))
                {
                    error = "Stage asset is outside the Kosmos tree: " + source.string();
                    return false;
                }
                relative = (fs::path(prefix) / rel).generic_string();
                const fs::path canonical = fs::canonical(source, ec);
                if (ec)
                {
                    error = "Cannot resolve stage asset " + source.string() + ": " + ec.message();
                    return false;
                }
                files[relative] = canonical;
                return true;
            }

            bool model(const fs::path &source, std::string &relative, std::string &error)
            {
                if (!add(source, relative, error))
                    return false;
                if (source.extension() == ".obj" || source.extension() == ".OBJ")
                {
                    std::ifstream obj(source);
                    if (!obj)
                    {
                        error = "Cannot read model: " + source.string();
                        return false;
                    }
                    std::string line;
                    while (std::getline(obj, line))
                    {
                        std::istringstream words(line);
                        std::string key, name;
                        if (!(words >> key) || key != "mtllib")
                            continue;
                        std::getline(words >> std::ws, name);
                        if (!name.empty() && name.back() == '\r')
                            name.pop_back();
                        if (name.empty())
                            continue;
                        if (fs::path(name).is_absolute())
                        {
                            error = "The model uses an absolute material path: " + name;
                            return false;
                        }
                        const fs::path mtl = source.parent_path() / name;
                        std::string ignored;
                        if (!add(mtl, ignored, error, false))
                            return false;
                        if (ignored.empty())
                            continue;
                        std::ifstream material(mtl);
                        if (!material)
                        {
                            error = "Cannot read material: " + mtl.string();
                            return false;
                        }
                        while (std::getline(material, line))
                        {
                            std::istringstream fields(line);
                            std::string type, token, last;
                            fields >> type;
                            if (type != "map_Kd" && type != "norm" && type != "map_Bump" && type != "map_bump" && type != "bump")
                                continue;
                            while (fields >> token)
                                last = token;
                            if (!last.empty() && fs::path(last).is_absolute())
                            {
                                error = "The model uses an absolute texture path: " + last;
                                return false;
                            }
                            if (!last.empty() && !add(source.parent_path() / last, ignored, error, false))
                                return false;
                        }
                        if (material.bad())
                        {
                            error = "Cannot read material: " + mtl.string();
                            return false;
                        }
                    }
                    if (obj.bad())
                    {
                        error = "Cannot read model: " + source.string();
                        return false;
                    }
                }
                else if (source.extension() == ".glb" || source.extension() == ".GLB")
                {
                    std::error_code ec;
                    std::ifstream in(source, std::ios::binary);
                    unsigned char header[20]{};
                    if (!in.read(reinterpret_cast<char *>(header), sizeof header) ||
                        std::string(reinterpret_cast<char *>(header), 4) != "glTF")
                    {
                        error = "Invalid GLB: " + source.string();
                        return false;
                    }
                    const std::uint32_t length = std::uint32_t(header[12]) | (std::uint32_t(header[13]) << 8) |
                                                 (std::uint32_t(header[14]) << 16) | (std::uint32_t(header[15]) << 24);
                    const auto size = fs::file_size(source, ec);
                    if (ec || size < sizeof header || std::string(reinterpret_cast<char *>(header + 16), 4) != "JSON" ||
                        length > size - sizeof header)
                    {
                        error = "Invalid GLB JSON chunk: " + source.string();
                        return false;
                    }
                    std::string text(length, '\0');
                    if (!in.read(text.data(), length))
                    {
                        error = "Cannot read GLB JSON: " + source.string();
                        return false;
                    }
                    const json gltf = json::parse(text, nullptr, false);
                    if (!gltf.is_object())
                    {
                        error = "Invalid GLB JSON: " + source.string();
                        return false;
                    }
                    for (const char *key : {"buffers", "images"})
                    {
                        if (!gltf.contains(key))
                            continue;
                        if (!gltf[key].is_array())
                        {
                            error = "Invalid GLB " + std::string(key) + ": " + source.string();
                            return false;
                        }
                        for (const json &item : gltf[key])
                            if (item.is_object() && item.contains("uri") && item["uri"].is_string())
                            {
                                const std::string uri = item["uri"].get<std::string>();
                                if (uri.rfind("data:", 0) == 0)
                                    continue;
                                std::string decoded;
                                for (std::size_t i = 0; i < uri.size(); ++i)
                                {
                                    if (uri[i] == '%' && i + 2 < uri.size() && std::isxdigit(static_cast<unsigned char>(uri[i + 1])) && std::isxdigit(static_cast<unsigned char>(uri[i + 2])))
                                    {
                                        decoded += static_cast<char>(std::stoi(uri.substr(i + 1, 2), nullptr, 16));
                                        i += 2;
                                    }
                                    else
                                        decoded += uri[i];
                                }
                                std::string ignored;
                                if (fs::path(decoded).is_absolute())
                                {
                                    error = "The GLB uses an absolute asset path: " + uri;
                                    return false;
                                }
                                if (!add(source.parent_path() / decoded, ignored, error))
                                    return false;
                            }
                    }
                }
                return true;
            }
        };

        json blank_stage(const std::string &name)
        {
            json stage = json::object();
            stage["name"] = name;
            stage["dry_mass"] = 1000.0;
            stage["propellant"] = 1000.0;
            stage["z"] = 0.0;
            stage["length"] = 4.0;
            stage["radius"] = 1.0;
            stage["engines"] = json::array();
            stage["gear"] = json::array();
            stage["parts"] = json::array();
            return stage;
        }

        double engine_thrust(const json &engine) { return engine.value("thrust", 0.0); }

        bool is_air_breathing(const json &engine)
        {
            const std::string type = engine.value("type", std::string());
            return engine.value("air_breathing", false) || type == "jet" || type == "ramjet" || type == "scramjet";
        }

        /** Group engines the main throttle does not drive (hover, RCS and pod units) are left out of the stage's rocket numbers. */
        bool main_group(const json &engine)
        {
            const std::string group = engine.value("group", std::string("main"));
            return group == "main";
        }

        bool has_ship_equipment(const json &root)
        {
            for (const char *key : {"script", "systems", "vessels", "ports", "lights", "cockpit", "cameras", "surfaces", "wheels", "instruments", "weapons", "hardpoints", "payload_bays", "parachutes"})
                if (root.contains(key) && !root[key].empty())
                    return true;
            return false;
        }

        void copy_model_companions(const fs::path &from, const fs::path &to_dir)
        {
            std::error_code ec;
            if (from.extension() != ".obj")
                return;
            const fs::path mtl = fs::path(from).replace_extension(".mtl");
            if (!fs::exists(mtl))
                return;
            fs::copy_file(mtl, to_dir / mtl.filename(), fs::copy_options::overwrite_existing, ec);
            std::ifstream in(mtl);
            std::string line;
            while (std::getline(in, line))
            {
                const std::size_t k = line.find("map_");
                if (k == std::string::npos)
                    continue;
                const std::size_t sp = line.find_last_of(' ');
                if (sp == std::string::npos || sp + 1 >= line.size())
                    continue;
                std::string tex = line.substr(sp + 1);
                while (!tex.empty() && (tex.back() == '\r' || tex.back() == ' '))
                    tex.pop_back();
                const fs::path src = from.parent_path() / tex;
                if (fs::exists(src))
                {
                    fs::create_directories((to_dir / tex).parent_path(), ec);
                    fs::copy_file(src, to_dir / tex, fs::copy_options::overwrite_existing, ec);
                }
            }
        }
    } // namespace

    std::string slug(const std::string &text)
    {
        std::string out;
        for (char c : text)
        {
            const unsigned char u = static_cast<unsigned char>(c);
            if (std::isalnum(u))
                out += static_cast<char>(std::tolower(u));
            else if (!out.empty() && out.back() != '-')
                out += '-';
        }
        while (!out.empty() && out.back() == '-')
            out.pop_back();
        return out;
    }

    std::vector<AddonInfo> scan_addons(const fs::path &addons_dir)
    {
        std::vector<AddonInfo> result;
        std::error_code ec;
        if (!fs::is_directory(addons_dir, ec))
            return result;
        std::vector<fs::path> folders;
        for (const fs::directory_entry &entry : fs::directory_iterator(addons_dir, ec))
            if (entry.is_directory() && fs::exists(entry.path() / "addon.json"))
                folders.push_back(entry.path());
        std::sort(folders.begin(), folders.end());
        for (const fs::path &folder : folders)
        {
            json j;
            std::string error;
            if (!read_json(folder / "addon.json", j, error) || !j.is_object())
                continue;
            AddonInfo info;
            info.dir = folder;
            info.id = j.value("id", folder.filename().string());
            info.name = j.value("name", info.id);
            if (j.contains("ships") && j["ships"].is_array())
                for (const json &s : j["ships"])
                    if (s.is_string())
                        info.ships.push_back(s.get<std::string>());
            if (!info.ships.empty())
                result.push_back(std::move(info));
        }
        return result;
    }

    bool Document::load(const AddonInfo &addon, const std::string &ship_file, std::string &error)
    {
        json loaded;
        if (!read_json(addon.dir / ship_file, loaded, error))
            return false;
        if (!loaded.is_object())
        {
            error = ship_file + " is not a ship description";
            return false;
        }
        root_ = std::move(loaded);
        if (!root_.contains("stages") || !root_["stages"].is_array())
            root_["stages"] = json::array();
        source_dir_ = addon.dir;
        source_ship_file_ = ship_file;
        source_addon_id_ = addon.id;
        source_ship_name_ = root_.value("name", std::string());
        {
            std::error_code ec;
            loaded_time_ = fs::last_write_time(addon.dir / ship_file, ec);
        }
        pending_.clear();
        undo_.clear();
        redo_.clear();
        dirty_ = false;
        return true;
    }

    void Document::make_blank(const std::string &name)
    {
        root_ = json::object();
        root_["name"] = name;
        root_["class"] = "rocket";
        root_["torque_accel"] = 0.3;
        root_["max_rate"] = 0.6;
        root_["rcs_thrust"] = 500.0;
        root_["drag_area"] = 2.0;
        root_["camera_distance"] = 12.0;
        root_["stages"] = json::array({blank_stage("Stage 1")});
        source_dir_.clear();
        source_ship_file_ = "ship.json";
        source_addon_id_.clear();
        source_ship_name_.clear();
        pending_.clear();
        undo_.clear();
        redo_.clear();
        dirty_ = false;
    }

    json &Document::stages()
    {
        if (!root_.contains("stages") || !root_["stages"].is_array())
            root_["stages"] = json::array();
        return root_["stages"];
    }

    int Document::stage_count() const { return root_.contains("stages") && root_["stages"].is_array() ? static_cast<int>(root_["stages"].size()) : 0; }

    void Document::checkpoint()
    {
        undo_.push_back({root_, pending_});
        if (undo_.size() > undo_limit)
            undo_.erase(undo_.begin());
        redo_.clear();
        dirty_ = true;
    }

    bool Document::undo()
    {
        if (undo_.empty())
            return false;
        redo_.push_back({root_, pending_});
        root_ = std::move(undo_.back().root);
        pending_ = std::move(undo_.back().pending);
        undo_.pop_back();
        dirty_ = true;
        return true;
    }

    bool Document::redo()
    {
        if (redo_.empty())
            return false;
        undo_.push_back({root_, pending_});
        root_ = std::move(redo_.back().root);
        pending_ = std::move(redo_.back().pending);
        redo_.pop_back();
        dirty_ = true;
        return true;
    }

    int Document::add_stage()
    {
        checkpoint();
        json &list = stages();
        json stage = blank_stage("Stage " + std::to_string(list.size() + 1));
        if (!list.empty())
        {
            const json &last = list.back();
            const double length = stage["length"].get<double>();
            stage["z"] = last.value("z", 0.0) - last.value("length", 4.0) * 0.5 - length * 0.5;
            stage["radius"] = last.value("radius", 1.0);
        }
        list.push_back(stage);
        return static_cast<int>(list.size()) - 1;
    }

    void Document::remove_stage(int index)
    {
        json &list = stages();
        if (index < 0 || index >= static_cast<int>(list.size()))
            return;
        checkpoint();
        list.erase(static_cast<std::size_t>(index));
    }

    int Document::duplicate_stage(int index)
    {
        json &list = stages();
        if (index < 0 || index >= static_cast<int>(list.size()))
            return -1;
        checkpoint();
        json copy = list[static_cast<std::size_t>(index)];
        copy["name"] = copy.value("name", std::string("Stage")) + " copy";
        list.insert(list.begin() + index + 1, copy);
        return index + 1;
    }

    bool Document::move_stage(int index, int delta)
    {
        json &list = stages();
        const int to = index + delta;
        if (index < 0 || to < 0 || index >= static_cast<int>(list.size()) || to >= static_cast<int>(list.size()))
            return false;
        checkpoint();
        std::swap(list[static_cast<std::size_t>(index)], list[static_cast<std::size_t>(to)]);
        return true;
    }

    bool Document::arrange_stage(int from, int to, std::string &error)
    {
        const int count = stage_count();
        if (from < 0 || from >= count || to < 0 || to >= count)
        {
            error = "Invalid stage position.";
            return false;
        }
        if (from == to)
            return true;
        if (has_ship_equipment(root_))
        {
            error = "Moving stages would change ship-wide equipment or stage references. Use the Structure tab to edit this ship.";
            return false;
        }
        const double tail = stages()[0].value("z", 0.0) + stages()[0].value("length", 4.0) * 0.5;
        checkpoint();
        json &list = stages();
        json moving = list[static_cast<std::size_t>(from)];
        list.erase(static_cast<std::size_t>(from));
        list.insert(list.begin() + to, std::move(moving));
        double cursor = tail;
        for (int i = 0; i < count; ++i)
        {
            const double length = list[static_cast<std::size_t>(i)].value("length", 4.0);
            set_stage_z(i, cursor - length * 0.5, true);
            cursor -= length;
        }
        return true;
    }

    bool Document::expanded_root(json &out, std::string &error) const
    {
        try
        {
            nlohmann::json expanded = nlohmann::json::parse(root_.dump());
            if (!kosmos::expand_part_kits(expanded, source_dir_, error))
                return false;
            out = json::parse(expanded.dump());
            return true;
        }
        catch (const nlohmann::json::exception &e)
        {
            error = "Invalid part-kit data: " + std::string(e.what());
            return false;
        }
    }

    bool Document::import_stage(const Document &source, int index, int position, int &inserted, std::string &error, std::string &warning, bool overlap_previous)
    {
        inserted = -1;
        error.clear();
        warning.clear();
        if (!source.has_source())
        {
            error = "The source stage must come from an addon.";
            return false;
        }
        json expanded;
        if (!source.expanded_root(expanded, error))
            return false;
        if (!expanded.contains("stages") || !expanded["stages"].is_array() ||
            index < 0 || index >= static_cast<int>(expanded["stages"].size()))
        {
            error = "The selected source stage no longer exists.";
            return false;
        }
        json stage = expanded["stages"][static_cast<std::size_t>(index)];
        if (!stage.is_object())
        {
            error = "The selected source stage is not an object.";
            return false;
        }
        const int count = stage_count();
        if (position == -1)
            position = count;
        if (position < 0 || position > count)
        {
            error = "Invalid insertion position.";
            return false;
        }
        if (position < count && has_ship_equipment(root_))
        {
            error = "Inserting between existing stages would move ship-wide equipment or change stage references. Add at the nose end instead, or start a new rocket.";
            return false;
        }
        std::error_code ec;
        StageAssets assets;
        assets.root = fs::canonical(source.source_dir().parent_path().parent_path(), ec);
        if (ec)
        {
            error = "Cannot locate the source Kosmos tree: " + ec.message();
            return false;
        }
        for (int n = 1;; ++n)
        {
            assets.prefix = "stage-assets-" + std::to_string(n);
            const std::string prefix = assets.prefix + "/";
            const auto it = pending_.lower_bound(prefix);
            if ((!has_source() || !fs::exists(source_dir_ / assets.prefix)) &&
                (it == pending_.end() || it->first.compare(0, prefix.size(), prefix) != 0))
                break;
        }
        if (stage.contains("parts") && stage["parts"].is_array())
            for (json &part : stage["parts"])
            {
                if (!part.is_object())
                    continue;
                for (const char *key : {"model", "animations"})
                {
                    if (!part.contains(key) || !part[key].is_string() || part[key].get<std::string>().empty())
                        continue;
                    const fs::path file = source.source_dir() / part[key].get<std::string>();
                    std::string relative;
                    const bool ok = std::string(key) == "model" ? assets.model(file, relative, error) : assets.add(file, relative, error);
                    if (!ok)
                        return false;
                    part[key] = relative;
                }
            }
        const json &list = stages();
        double z = 0.0;
        if (!list.empty() && position == 0)
        {
            const json &first = list.front();
            z = first.value("z", 0.0) + first.value("length", 4.0) * 0.5 + stage.value("length", 4.0) * 0.5;
        }
        else if (position > 0)
        {
            const json &previous = list[static_cast<std::size_t>(position - 1)];
            z = previous.value("z", 0.0) + (overlap_previous ? 1.0 : -1.0) * previous.value("length", 4.0) * 0.5 - stage.value("length", 4.0) * 0.5;
        }
        const double dz = z - stage.value("z", 0.0);
        stage["z"] = z;
        shift_contents(stage, dz);
        checkpoint();
        if (position > 0 && position < count)
        {
            const json &next = stages()[static_cast<std::size_t>(position)];
            double nose = z - stage.value("length", 4.0) * 0.5;
            if (overlap_previous)
            {
                const json &previous = stages()[static_cast<std::size_t>(position - 1)];
                nose = std::min(nose, previous.value("z", 0.0) - previous.value("length", 4.0) * 0.5);
            }
            const double next_z = nose - next.value("length", 4.0) * 0.5;
            const double shift = next_z - next.value("z", 0.0);
            for (int i = position; i < count; ++i)
                set_stage_z(i, stages()[static_cast<std::size_t>(i)].value("z", 0.0) + shift, true);
        }
        stages().insert(stages().begin() + position, std::move(stage));
        pending_.insert(assets.files.begin(), assets.files.end());
        inserted = position;
        warning = assets.warning;
        return true;
    }

    void Document::set_stage_z(int index, double z, bool with_contents)
    {
        json &list = stages();
        if (index < 0 || index >= static_cast<int>(list.size()))
            return;
        json &stage = list[static_cast<std::size_t>(index)];
        const double dz = z - stage.value("z", 0.0);
        stage["z"] = z;
        if (with_contents && dz != 0.0)
            shift_contents(stage, dz);
    }

    void Document::auto_pack()
    {
        json &list = stages();
        if (list.size() < 2)
            return;
        checkpoint();
        for (std::size_t i = 1; i < list.size(); ++i)
        {
            const json &prev = list[i - 1];
            const double z = prev.value("z", 0.0) - prev.value("length", 4.0) * 0.5 - list[i].value("length", 4.0) * 0.5;
            set_stage_z(static_cast<int>(i), z, true);
        }
    }

    void Document::add_model_part(int stage, const fs::path &file)
    {
        json &list = stages();
        if (stage < 0 || stage >= static_cast<int>(list.size()))
            return;
        checkpoint();
        std::string relative;
        std::error_code ec;
        if (has_source())
        {
            const fs::path rel = fs::relative(file, source_dir_, ec);
            if (!ec && !rel.empty() && *rel.begin() != "..")
                relative = rel.generic_string();
        }
        if (relative.empty())
        {
            relative = "models/" + file.filename().string();
            pending_[relative] = file;
        }
        json part = json::object();
        part["model"] = relative;
        json &target = list[static_cast<std::size_t>(stage)];
        if (!target.contains("parts") || !target["parts"].is_array())
            target["parts"] = json::array();
        target["parts"].push_back(part);
    }

    void Document::add_prop_part(int stage, const std::string &prop)
    {
        json &list = stages();
        if (stage < 0 || stage >= static_cast<int>(list.size()))
            return;
        checkpoint();
        json &target = list[static_cast<std::size_t>(stage)];
        json part = json::object();
        part["prop"] = prop;
        part["radius"] = target.value("radius", 1.0);
        part["length"] = target.value("length", 4.0);
        part["z"] = target.value("z", 0.0);
        part["colour"] = json::array({226, 228, 232});
        part["accent"] = json::array({70, 74, 84});
        if (!target.contains("parts") || !target["parts"].is_array())
            target["parts"] = json::array();
        target["parts"].push_back(part);
    }

    void Document::add_engine(int stage)
    {
        json &list = stages();
        if (stage < 0 || stage >= static_cast<int>(list.size()))
            return;
        checkpoint();
        json &target = list[static_cast<std::size_t>(stage)];
        json engine = json::object();
        engine["position"] = json::array({0.0, 0.0, target.value("z", 0.0) + target.value("length", 4.0) * 0.5});
        engine["direction"] = json::array({0.0, 0.0, -1.0});
        engine["thrust"] = 500000.0;
        engine["isp"] = 300.0;
        engine["plume"] = 10.0;
        engine["type"] = "rocket";
        engine["fuel"] = "kerosene";
        if (!target.contains("engines") || !target["engines"].is_array())
            target["engines"] = json::array();
        target["engines"].push_back(engine);
    }

    void Document::add_gear(int stage)
    {
        json &list = stages();
        if (stage < 0 || stage >= static_cast<int>(list.size()))
            return;
        checkpoint();
        json &target = list[static_cast<std::size_t>(stage)];
        const double r = target.value("radius", 1.0);
        if (!target.contains("gear") || !target["gear"].is_array())
            target["gear"] = json::array();
        target["gear"].push_back(json::array({r * 1.2, 0.0, target.value("z", 0.0) + target.value("length", 4.0) * 0.5}));
    }

    fs::path Document::resolve(const std::string &relative) const
    {
        if (relative.empty())
            return {};
        if (const auto it = pending_.find(relative); it != pending_.end())
            return it->second;
        if (has_source() && fs::exists(source_dir_ / relative))
            return source_dir_ / relative;
        return {};
    }

    std::vector<StageStats> Document::stats() const
    {
        std::vector<StageStats> out;
        if (!root_.contains("stages") || !root_["stages"].is_array())
            return out;
        const json &list = root_["stages"];
        out.resize(list.size());
        double above = 0.0;
        for (std::size_t i = list.size(); i-- > 0;)
        {
            const json &st = list[i];
            StageStats &s = out[i];
            s.dry = st.value("dry_mass", 0.0);
            s.propellant = st.value("propellant", 0.0);
            double weighted = 0.0;
            for (const json &e : st.value("engines", json::array()))
            {
                if (!main_group(e))
                    continue;
                s.thrust += engine_thrust(e);
                weighted += engine_thrust(e) * e.value("isp", 0.0);
                s.air_breathing = s.air_breathing || is_air_breathing(e);
            }
            s.isp = s.thrust > 0.0 ? weighted / s.thrust : 0.0;
            s.mass_at_ignition = above + s.dry + s.propellant;
            s.twr = s.mass_at_ignition > 0.0 ? s.thrust / (s.mass_at_ignition * g0) : 0.0;
            const double burnout = s.mass_at_ignition - s.propellant;
            s.delta_v = !s.air_breathing && s.isp > 0.0 && burnout > 0.0 && s.propellant > 0.0 ? s.isp * g0 * std::log(s.mass_at_ignition / burnout) : 0.0;
            above = s.mass_at_ignition;
        }
        return out;
    }

    std::vector<std::string> Document::validate() const
    {
        std::vector<std::string> problems;
        if (name().empty())
            problems.push_back("The ship has no name.");
        if (has_source() && !name().empty() && name() == source_ship_name_)
            problems.push_back("The ship still has the original's name; rename it before Save as new addon or both will be listed under one name.");
        const json empty = json::array();
        const json &list = root_.contains("stages") ? root_["stages"] : empty;
        if (!list.is_array() || list.empty())
        {
            problems.push_back("The ship has no stages.");
            return problems;
        }
        for (std::size_t i = 0; i < list.size(); ++i)
        {
            const json &st = list[i];
            const std::string label = "Stage " + std::to_string(i + 1) + " (" + st.value("name", std::string("unnamed")) + ")";
            if (st.value("dry_mass", 0.0) <= 0.0)
                problems.push_back(label + " has no dry mass.");
            if (st.value("length", 0.0) <= 0.0 || st.value("radius", 0.0) <= 0.0)
                problems.push_back(label + " needs a length and a radius.");
            const json engines = st.value("engines", empty);
            if (st.value("propellant", 0.0) > 0.0 && std::none_of(engines.begin(), engines.end(), [](const json &e) { return engine_thrust(e) > 0.0; }))
                problems.push_back(label + " carries propellant but has no engine with thrust.");
            const double z = st.value("z", 0.0), half = st.value("length", 0.0) * 0.5;
            for (const json &e : engines)
                if (e.contains("position") && e["position"].size() >= 3 && e["position"][2].is_number())
                {
                    const double ez = e["position"][2].get<double>();
                    if (ez > z + half + 3.0 || ez < z - half - 3.0)
                        problems.push_back(label + " has an engine well outside its length (z " + std::to_string(ez).substr(0, 6) + ").");
                }
            for (const json &p : st.value("parts", empty))
                if (p.contains("model") && p["model"].is_string() && resolve(p["model"].get<std::string>()).empty())
                    problems.push_back(label + ": model " + p["model"].get<std::string>() + " is missing.");
        }
        return problems;
    }

    bool Document::save_as(const fs::path &dest_root, const std::string &id, const std::string &addon_name, bool overwrite, fs::path &written_dir, std::string &error)
    {
        std::error_code ec;
        if (id.empty() || id != slug(id))
        {
            error = "The addon id must be lower case letters, digits and dashes.";
            return false;
        }
        const fs::path dest = dest_root / id;
        if (has_source() && fs::equivalent(dest, source_dir_, ec) && !ec)
        {
            error = "ShipEdit never writes into the addon it was loaded from; choose a new id.";
            return false;
        }
        if (fs::exists(dest))
        {
            if (!overwrite)
            {
                error = dest.string() + " already exists.";
                return false;
            }
            fs::remove_all(dest, ec);
            if (ec)
            {
                error = "cannot clear " + dest.string() + ": " + ec.message();
                return false;
            }
        }
        fs::create_directories(dest, ec);
        if (ec)
        {
            error = "cannot create " + dest.string() + ": " + ec.message();
            return false;
        }

        json addon = json::object();
        if (has_source())
        {
            fs::copy(source_dir_, dest, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
            if (ec)
            {
                error = "copying the source addon failed: " + ec.message();
                return false;
            }
            fs::remove_all(dest / ".git", ec);
            std::string read_error;
            if (!read_json(source_dir_ / "addon.json", addon, read_error) || !addon.is_object())
                addon = json::object();
        }
        addon["id"] = id;
        addon["name"] = addon_name.empty() ? id : addon_name;
        if (!addon.contains("version"))
            addon["version"] = "0.1";
        const std::string ship_file = source_ship_file_.empty() ? "ship.json" : source_ship_file_;
        // Other ships in the source addon stay listed: equipment or scripts may refer to them (the ICBM's warhead, for one).
        if (!addon.contains("ships") || !addon["ships"].is_array() || std::find(addon["ships"].begin(), addon["ships"].end(), json(ship_file)) == addon["ships"].end())
            addon["ships"] = json::array({ship_file});
        // A copied native program would be a second copy of the original's; the new addon is a ship, not an app.
        addon.erase("native");

        for (const auto &[relative, source] : pending_)
        {
            const fs::path target = dest / relative;
            fs::create_directories(target.parent_path(), ec);
            fs::copy_file(source, target, fs::copy_options::overwrite_existing, ec);
            if (ec)
            {
                error = "copying " + source.string() + " failed: " + ec.message();
                return false;
            }
            if (relative.rfind("stage-assets-", 0) != 0)
                copy_model_companions(source, target.parent_path());
        }

        std::ofstream ship_out(dest / ship_file);
        std::ofstream addon_out(dest / "addon.json");
        if (!ship_out || !addon_out)
        {
            error = "cannot write into " + dest.string();
            return false;
        }
        ship_out << root_.dump(1) << "\n";
        addon_out << addon.dump(2) << "\n";
        written_dir = dest;
        return true;
    }

    bool Document::changed_on_disk() const
    {
        std::error_code ec;
        if (!has_source())
            return false;
        const auto now = fs::last_write_time(source_dir_ / (source_ship_file_.empty() ? "ship.json" : source_ship_file_), ec);
        return !ec && now != loaded_time_;
    }

    bool Document::save_in_place(const fs::path &backup_root, fs::path &backup_dir, std::string &error, bool force)
    {
        std::error_code ec;
        if (!has_source())
        {
            error = "This ship was not loaded from an addon; use Save as new addon.";
            return false;
        }
        if (!force && changed_on_disk())
        {
            error = "The ship file was changed outside ShipEdit after it was loaded. Saving now would overwrite that change; Load it again (or save as a new addon).";
            return false;
        }
        const fs::path source = fs::weakly_canonical(source_dir_, ec);
        const fs::path root = fs::weakly_canonical(backup_root, ec);
        const std::string src_s = source.string() + "/", root_s = root.string() + "/";
        if (root_s.compare(0, src_s.size(), src_s) == 0)
        {
            error = "The backup folder must be outside the addon.";
            return false;
        }
        char stamp[32];
        const std::time_t now = std::time(nullptr);
        std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", std::localtime(&now));
        backup_dir = backup_root / (source.filename().string() + "-" + stamp);
        fs::create_directories(backup_dir, ec);
        if (ec)
        {
            error = "cannot create " + backup_dir.string() + ": " + ec.message();
            return false;
        }
        fs::copy(source_dir_, backup_dir, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        if (ec)
        {
            error = "backing up the addon failed, nothing was changed: " + ec.message();
            return false;
        }
        fs::remove_all(backup_dir / ".git", ec);

        for (const auto &[relative, from] : pending_)
        {
            const fs::path target = source_dir_ / relative;
            fs::create_directories(target.parent_path(), ec);
            if (!fs::equivalent(from, target, ec))
            {
                fs::copy_file(from, target, fs::copy_options::overwrite_existing, ec);
                if (ec)
                {
                    error = "copying " + from.string() + " failed: " + ec.message();
                    return false;
                }
                if (relative.rfind("stage-assets-", 0) != 0)
                    copy_model_companions(from, target.parent_path());
            }
        }
        pending_.clear();

        const std::string ship_file = source_ship_file_.empty() ? "ship.json" : source_ship_file_;
        std::ofstream ship_out(source_dir_ / ship_file);
        if (!ship_out)
        {
            error = "cannot write " + (source_dir_ / ship_file).string() + " (the backup is in " + backup_dir.string() + ")";
            return false;
        }
        ship_out << root_.dump(1) << "\n";
        ship_out.close();
        loaded_time_ = fs::last_write_time(source_dir_ / ship_file, ec);
        return true;
    }
} // namespace shipedit
