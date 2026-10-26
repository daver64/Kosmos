#include "base.h"

#include "json.hpp"
#include "props3d.h"
#include "sl.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <sstream>

namespace kosmos
{
    namespace
    {
        constexpr double pi = 3.14159265358979323846;

        struct Frame
        {
            sl::dvec3 up, east, north;
        };

        Frame local_frame(const sl::dvec3 &direction, const sl::dvec3 &pole)
        {
            Frame f;
            f.up = glm::normalize(direction);
            f.east = glm::normalize(glm::cross(pole, f.up));
            f.north = glm::cross(f.up, f.east);
            return f;
        }

        // Base-frame axes: forward along the heading, right of it.
        void base_axes(const Frame &f, double heading_deg, sl::dvec3 &forward, sl::dvec3 &right)
        {
            const double h = heading_deg * pi / 180.0;
            forward = f.north * std::cos(h) + f.east * std::sin(h);
            right = f.east * std::cos(h) - f.north * std::sin(h);
        }

        bool read_colour(const nlohmann::json &j, const char *key, sl::Colour &out)
        {
            if (!j.contains(key) || !j[key].is_array() || j[key].size() < 3)
                return false;
            out = {static_cast<std::uint8_t>(j[key][0].get<int>()), static_cast<std::uint8_t>(j[key][1].get<int>()), static_cast<std::uint8_t>(j[key][2].get<int>()), 255};
            return true;
        }

        BaseObject parse_object(const nlohmann::json &j)
        {
            BaseObject o;
            o.type = j.at("type").get<std::string>();
            o.model = j.value("model", "");
            o.x = j.value("x", 0.0);
            o.z = j.value("z", 0.0);
            o.y = j.value("y", 0.0);
            o.heading = j.value("heading", 0.0);
            o.scale = j.value("scale", 1.0);
            o.count = std::max(1, j.value("count", 1));
            o.step_x = j.value("step_x", 0.0);
            o.step_z = j.value("step_z", 0.0);
            o.size = j.value("size", 0.0);
            o.width = j.value("width", 0.0);
            o.depth = j.value("depth", 0.0);
            o.height = j.value("height", 0.0);
            o.length = j.value("length", 0.0);
            o.radius = j.value("radius", 0.0);
            o.number = j.value("number", 0);
            o.seed = j.value("seed", 1u);
            o.has_colour = read_colour(j, "colour", o.colour);
            o.has_colour2 = read_colour(j, "colour2", o.colour2);
            return o;
        }

        float pick(double value, float fallback)
        {
            return value > 0.0 ? static_cast<float>(value) : fallback;
        }

        // Returns an empty mesh for an unknown type.
        sl::MeshData build(const BaseObject &o)
        {
            const std::string &t = o.type;
            const float size = static_cast<float>(o.size), w = static_cast<float>(o.width), d = static_cast<float>(o.depth), h = static_cast<float>(o.height),
                        len = static_cast<float>(o.length), r = static_cast<float>(o.radius);
            const auto col = [&](sl::Colour fallback) { return o.has_colour ? o.colour : fallback; };
            const auto col2 = [&](sl::Colour fallback) { return o.has_colour2 ? o.colour2 : fallback; };
            if (t == "house")
                return sl::make_house_data(pick(w, 6.0f), pick(d, 5.0f), pick(h, 3.0f), col({214, 196, 160, 255}), col2({150, 70, 56, 255}), o.seed);
            if (t == "office")
                return sl::make_office_data(pick(w, 14.0f), pick(d, 10.0f), o.number > 0 ? o.number : 4, col({170, 172, 176, 255}));
            if (t == "skyscraper")
                return sl::make_skyscraper_data(pick(w, 14.0f), o.number > 0 ? o.number : 30, col({66, 96, 128, 255}), o.seed);
            if (t == "hangar")
                return sl::make_hangar_data(pick(w, 24.0f), pick(d, 30.0f), pick(h, 9.0f), col({170, 175, 180, 255}));
            if (t == "wall")
                return sl::make_wall_data(pick(len, 6.0f), pick(h, 2.5f), 0.4f, col({150, 144, 134, 255}));
            if (t == "fence")
                return sl::make_fence_data(pick(len, 6.0f), pick(h, 1.2f), col({176, 140, 96, 255}));
            if (t == "water_tower")
                return sl::make_water_tower_data(pick(h, 16.0f), pick(r, 3.0f), col({120, 140, 160, 255}));
            if (t == "radar")
                return sl::make_radar_data(pick(h, 8.0f), pick(r, 3.0f), col({205, 208, 212, 255}));
            if (t == "domed_radar")
                return sl::make_domed_radar_data(pick(r, 4.0f), col({236, 238, 240, 255}));
            if (t == "pressure_dome")
                return sl::make_pressure_dome_data(pick(r, 8.0f), col({226, 229, 234, 255}));
            if (t == "habitat_tube")
                return sl::make_habitat_tube_data(pick(len, 12.0f), pick(r, 2.2f), col({222, 224, 226, 255}));
            if (t == "lunar_rover")
                return sl::make_lunar_rover_data(pick(len, 3.5f), col({214, 214, 208, 255}));
            if (t == "landing_pad")
                return sl::make_landing_pad_data(pick(r, 15.0f));
            if (t == "control_tower")
                return sl::make_control_tower_data(pick(h, 24.0f), o.seed);
            if (t == "street_lamp")
                return sl::make_street_lamp_data(pick(h, 6.0f), col({70, 74, 80, 255}));
            if (t == "road")
                return sl::make_road_data(pick(len, 10.0f), pick(w, 6.0f));
            if (t == "barrel")
                return sl::make_barrel_data(pick(r, 0.4f), pick(h, 1.0f), col({60, 90, 150, 255}));
            if (t == "crate")
                return sl::make_crate_data(pick(size, 1.0f), col({168, 128, 80, 255}));
            if (t == "container")
                return sl::make_container_data(pick(len, 6.1f), pick(w, 2.44f), pick(h, 2.6f), col({160, 52, 42, 255}));
            if (t == "tank")
                return sl::make_tank_data(pick(r, 3.0f), pick(h, 6.0f), col({196, 198, 200, 255}));
            if (t == "conifer")
                return sl::make_conifer_data(pick(h, 8.0f), pick(r, 2.0f));
            if (t == "rock")
                return sl::make_rock_data(pick(r, 1.0f), col({120, 118, 112, 255}), o.seed);
            if (t == "tree")
                return sl::make_tree_data(pick(h, 6.0f), pick(r, 2.0f), col2({104, 74, 48, 255}), col({64, 134, 58, 255}), o.seed);
            if (t == "bush")
                return sl::make_bush_data(pick(r, 0.8f), col({58, 118, 52, 255}), o.seed);
            if (t == "rock_cluster")
                return sl::make_rock_cluster_data(pick(r, 1.5f), col({120, 118, 112, 255}), o.seed);
            if (t == "car")
                return sl::make_car_data(pick(len, 4.4f), col({190, 44, 40, 255}), o.seed);
            if (t == "spaceship")
                return sl::make_spaceship_data(pick(len, 8.0f), col({170, 176, 190, 255}));
            if (t == "aeroplane")
                return sl::make_aeroplane_data(pick(w, 12.0f), col({232, 234, 238, 255}), col2({40, 90, 170, 255}));
            if (t == "launch_tower")
                return sl::make_launch_tower_data(pick(h, 120.0f), pick(w, 8.0f), pick(len, 14.0f), col({196, 70, 52, 255}));
            if (t == "launch_pad")
                return sl::make_launch_pad_data(pick(size, 60.0f));
            if (t == "saturn_v")
                return sl::make_saturn_v_data(pick(size, 1.0f));
            if (t == "rocket_stage")
                return sl::make_rocket_stage_data(pick(r, 5.05f), pick(len, 20.0f), col({236, 236, 230, 255}));
            if (t == "station_module")
                return sl::make_station_module_data(pick(r, 3.3f), pick(len, 14.6f), col({236, 236, 230, 255}), o.seed);
            if (t == "solar_array")
                return sl::make_solar_array_data(pick(len, 10.0f), pick(w, 3.0f), o.number > 0 ? o.number : 4, col({30, 48, 118, 255}));
            if (t == "solar_windmill")
                return sl::make_solar_windmill_data(pick(len, 7.0f), pick(w, 3.0f));
            if (t == "antenna_dish")
                return sl::make_antenna_dish_data(pick(r, 1.0f));
            if (t == "satellite")
                return sl::make_satellite_data(pick(size, 1.5f), o.seed);
            if (t == "skylab")
                return sl::make_skylab_data(pick(size, 1.0f));
            return {};
        }

        std::string key_of(const BaseObject &o)
        {
            std::ostringstream s;
            s << o.type << '|' << o.size << '|' << o.width << '|' << o.depth << '|' << o.height << '|' << o.length << '|' << o.radius << '|' << o.number << '|' << o.seed << '|'
              << o.has_colour << int(o.colour.red) << ',' << int(o.colour.green) << ',' << int(o.colour.blue) << '|' << o.has_colour2 << int(o.colour2.red) << ',' << int(o.colour2.green) << ','
              << int(o.colour2.blue);
            return s.str();
        }

        // A runway lying on y = 0 along Z: asphalt, white edge lines, threshold bars and centre-line dashes.
        sl::MeshData make_runway(float length, float width)
        {
            sl::MeshData m;
            const auto quad = [&](float x0, float z0, float x1, float z1, float y, sl::Colour c)
            {
                const std::uint32_t base = static_cast<std::uint32_t>(m.vertices.size());
                const float xs[4] = {x0, x0, x1, x1}, zs[4] = {z0, z1, z1, z0};
                for (int i = 0; i < 4; ++i)
                {
                    sl::Vertex3D v;
                    v.position[0] = xs[i];
                    v.position[1] = y;
                    v.position[2] = zs[i];
                    v.normal[0] = 0.0f;
                    v.normal[1] = 1.0f;
                    v.normal[2] = 0.0f;
                    v.colour[0] = c.red / 255.0f;
                    v.colour[1] = c.green / 255.0f;
                    v.colour[2] = c.blue / 255.0f;
                    m.vertices.push_back(v);
                }
                for (const std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
                    m.indices.push_back(base + i);
            };
            const float hl = 0.5f * length, hw = 0.5f * width;
            quad(-hw, -hl, hw, hl, 0.0f, {58, 59, 64, 255});
            for (const float side : {-1.0f, 1.0f})
                quad(side * (hw - 1.2f) - 0.15f, -hl + 2.0f, side * (hw - 1.2f) + 0.15f, hl - 2.0f, 0.02f, {225, 225, 222, 255});
            const sl::Colour white{235, 235, 232, 255};
            for (const float end : {-1.0f, 1.0f})
            {
                const float z0 = end * (hl - 6.0f), z1 = end * (hl - 36.0f);
                for (int i = 0; i < 8; ++i)
                {
                    const float x = (static_cast<float>(i) - 3.5f) * (width * 0.9f / 8.0f);
                    quad(x - 0.7f, std::min(z0, z1), x + 0.7f, std::max(z0, z1), 0.02f, white);
                }
            }
            const int dashes = std::max(1, static_cast<int>((length - 160.0f) / 50.0f));
            for (int i = 0; i < dashes; ++i)
            {
                const float z = -hl + 80.0f + (static_cast<float>(i) + 0.5f) * (length - 160.0f) / static_cast<float>(dashes);
                quad(-0.5f, z - 15.0f, 0.5f, z + 15.0f, 0.02f, white);
            }
            return m;
        }

        double smooth(double t)
        {
            t = std::clamp(t, 0.0, 1.0);
            return t * t * (3.0 - 2.0 * t);
        }
    }

    bool load_bases(const std::string &file, std::vector<Base> &out, std::string &error)
    {
        std::ifstream in(file);
        if (!in)
        {
            error = "cannot open " + file;
            return false;
        }
        std::stringstream text;
        text << in.rdbuf();
        const std::size_t before = out.size();
        if (!parse_bases(text.str(), file, out, error))
            return false;
        const std::string folder = std::filesystem::path(file).parent_path().string();
        for (std::size_t i = before; i < out.size(); ++i)
            out[i].source_dir = folder;
        return true;
    }

    bool parse_bases(const std::string &text, const std::string &file, std::vector<Base> &out, std::string &error)
    {
        try
        {
            const nlohmann::json json = nlohmann::json::parse(text);
            for (const nlohmann::json &j : json.at("bases"))
            {
                Base b;
                b.id = j.at("id").get<std::string>();
                b.name = j.value("name", b.id);
                b.body = j.value("body", "Earth");
                b.latitude = j.at("latitude").get<double>();
                b.longitude = j.at("longitude").get<double>();
                b.heading = j.value("heading", 0.0);
                b.flat_radius = j.value("flat_radius", 300.0);
                b.blend = j.value("blend", 200.0);
                b.elevation = j.value("elevation", 0.0);
                for (const nlohmann::json &n : j.value("navaids", nlohmann::json::array()))
                {
                    BaseNavaid nv;
                    nv.type = n.at("type").get<std::string>();
                    nv.id = n.value("id", b.id + "-" + nv.type);
                    nv.name = n.value("name", b.name + " " + nv.type);
                    nv.x = n.value("x", 0.0);
                    nv.z = n.value("z", 0.0);
                    nv.frequency = n.at("frequency").get<double>();
                    nv.heading = n.value("heading", 0.0);
                    nv.glide = n.value("glide", 3.0);
                    nv.range = n.value("range", 0.0);
                    b.navaids.push_back(std::move(nv));
                }
                for (const nlohmann::json &c : j.value("cameras", nlohmann::json::array()))
                {
                    BaseCamera cam;
                    cam.name = c.value("name", "Camera");
                    cam.x = c.value("x", cam.x);
                    cam.y = c.value("y", cam.y);
                    cam.z = c.value("z", cam.z);
                    cam.fov = c.value("fov", cam.fov);
                    cam.track = c.value("track", cam.track);
                    cam.yaw = c.value("yaw", cam.yaw);
                    cam.pitch = c.value("pitch", cam.pitch);
                    b.cameras.push_back(std::move(cam));
                }
                for (const nlohmann::json &r : j.value("runways", nlohmann::json::array()))
                {
                    Runway rw;
                    rw.name = r.value("name", "Runway");
                    rw.x = r.value("x", 0.0);
                    rw.z = r.value("z", 0.0);
                    rw.heading = r.value("heading", 0.0);
                    rw.length = r.value("length", 1500.0);
                    rw.width = r.value("width", 45.0);
                    for (const nlohmann::json &i : r.value("ils", nlohmann::json::array()))
                        rw.ils.push_back({i.at("frequency").get<double>(), i.value("reverse", false)});
                    b.runways.push_back(std::move(rw));
                }
                if (j.contains("pad"))
                {
                    b.pad_x = j["pad"].value("x", 0.0);
                    b.pad_z = j["pad"].value("z", 0.0);
                    b.pad_heading = j["pad"].value("heading", 0.0);
                }
                for (const nlohmann::json &o : j.value("objects", nlohmann::json::array()))
                    b.objects.push_back(parse_object(o));
                for (const nlohmann::json &l : j.value("lights", nlohmann::json::array()))
                    b.lights.push_back(parse_light(l));
                out.push_back(std::move(b));
            }
        }
        catch (const std::exception &e)
        {
            error = file + ": " + e.what();
            return false;
        }
        return true;
    }

    std::vector<Base> load_bases_in(const std::string &folder, std::vector<std::string> &errors)
    {
        namespace fs = std::filesystem;
        std::vector<Base> result;
        if (!fs::is_directory(folder))
            return result;
        std::vector<fs::path> files;
        for (const fs::directory_entry &entry : fs::directory_iterator(folder))
            if (entry.is_regular_file() && entry.path().extension() == ".json")
                files.push_back(entry.path());
        std::sort(files.begin(), files.end());
        for (const fs::path &file : files)
        {
            std::string error;
            if (!load_bases(file.string(), result, error))
                errors.push_back(error);
        }
        return result;
    }

    sl::dvec3 base_direction(const Universe &universe, int body, const Base &base)
    {
        return surface_direction(universe, body, base.latitude, base.longitude);
    }

    sl::dvec3 base_point(const Base &base, double radius, double x, double z)
    {
        const Frame f = local_frame(base.direction, base.pole);
        sl::dvec3 forward, right;
        base_axes(f, base.heading, forward, right);
        return glm::normalize(base.direction + (right * x + forward * z) / radius);
    }

    void base_local_xz(const Universe &universe, const Base &base, const sl::dvec3 &world_point, double &x, double &z)
    {
        const int body = universe.find(base.body);
        const double radius = body >= 0 ? universe.bodies[static_cast<std::size_t>(body)].radius : 1.0;
        const sl::dvec3 centre = body >= 0 ? universe.position(body) : sl::dvec3(0.0);
        const sl::dvec3 dir = glm::normalize(world_point - centre);
        const Frame f = local_frame(base.direction, base.pole);
        sl::dvec3 forward, right;
        base_axes(f, base.heading, forward, right);
        const sl::dvec3 offset = (dir - base.direction) * radius;
        x = glm::dot(offset, right);
        z = glm::dot(offset, forward);
    }

    namespace
    {
        std::string &model_folder()
        {
            static std::string folder;
            return folder;
        }

        std::string &stock_folder()
        {
            static std::string folder;
            return folder;
        }
    }

    void set_base_model_folder(const std::string &folder)
    {
        model_folder() = folder;
    }

    void set_stock_model_folder(const std::string &folder)
    {
        stock_folder() = folder;
    }

    std::vector<std::string> stock_base_models()
    {
        namespace fs = std::filesystem;
        std::vector<std::string> found;
        std::error_code ec;
        const fs::path root = fs::path(stock_folder()) / "parts";
        if (stock_folder().empty() || !fs::is_directory(root, ec))
            return found;
        for (const fs::directory_entry &folder : fs::directory_iterator(root, ec))
            if (folder.is_directory(ec))
                for (const fs::directory_entry &file : fs::directory_iterator(folder.path(), ec))
                    if (file.path().extension() == ".glb")
                        found.push_back(fs::relative(file.path(), stock_folder(), ec).generic_string());
        std::sort(found.begin(), found.end());
        return found;
    }

    bool is_stock_model(const std::string &path)
    {
        if (stock_folder().empty() || path.empty())
            return false;
        std::error_code ec;
        const std::filesystem::path root = std::filesystem::weakly_canonical(stock_folder(), ec);
        const std::string rel = std::filesystem::relative(std::filesystem::weakly_canonical(path, ec), root, ec).generic_string();
        return !rel.empty() && rel.compare(0, 2, "..") != 0;
    }

    std::string resolve_base_model(const Base &base, const std::string &model)
    {
        namespace fs = std::filesystem;
        if (model.empty())
            return {};
        std::error_code ec;
        const fs::path given(model);
        if (given.is_absolute())
            return fs::exists(given, ec) ? given.string() : std::string();
        for (const std::string &folder : {base.source_dir, model_folder(), stock_folder()})
            if (!folder.empty() && fs::exists(fs::path(folder) / given, ec))
                return (fs::path(folder) / given).string();
        return {};
    }

    bool base_object_mesh(const BaseObject &object, sl::MeshData &mesh)
    {
        if (object.type == "model")
            return false;
        mesh = build(object);
        return !mesh.indices.empty();
    }

    bool bake_base_object(const BaseObject &object, sl::MeshData &out)
    {
        sl::MeshData base;
        if (!base_object_mesh(object, base))
            return false;
        const double h = object.heading * pi / 180.0, c = std::cos(h), s = std::sin(h), k = object.scale;
        for (int i = 0; i < std::max(1, object.count); ++i)
        {
            const double ox = object.x + object.step_x * i, oz = object.z + object.step_z * i;
            const std::uint32_t first = static_cast<std::uint32_t>(out.vertices.size());
            for (const sl::Vertex3D &v : base.vertices)
            {
                sl::Vertex3D t = v;
                // Object frame to the base frame (x right, z forward), then to the model frame (x left).
                t.position[0] = static_cast<float>(-ox + k * (v.position[0] * c - v.position[2] * s));
                t.position[1] = static_cast<float>(object.y + k * v.position[1]);
                t.position[2] = static_cast<float>(oz + k * (v.position[0] * s + v.position[2] * c));
                t.normal[0] = static_cast<float>(v.normal[0] * c - v.normal[2] * s);
                t.normal[2] = static_cast<float>(v.normal[0] * s + v.normal[2] * c);
                out.vertices.push_back(t);
            }
            for (std::uint32_t index : base.indices)
                out.indices.push_back(first + index);
        }
        return true;
    }

    const std::vector<std::string> &base_object_types()
    {
        static const std::vector<std::string> types = {"house", "office", "skyscraper", "hangar", "wall", "fence", "water_tower", "radar", "domed_radar", "pressure_dome", "habitat_tube", "lunar_rover", "landing_pad", "control_tower", "street_lamp",
                                                       "road", "barrel", "crate", "container", "tank", "conifer", "rock", "tree", "bush", "rock_cluster", "car", "spaceship", "aeroplane",
                                                       "model", "launch_tower", "launch_pad", "saturn_v", "rocket_stage", "station_module", "solar_array", "solar_windmill", "antenna_dish",
                                                       "satellite", "skylab"};
        return types;
    }

    std::string bases_to_json(const std::vector<Base> &bases)
    {
        using nlohmann::json;
        const auto colour = [](sl::Colour c) { return json::array({static_cast<int>(c.red), static_cast<int>(c.green), static_cast<int>(c.blue)}); };
        json root;
        root["bases"] = json::array();
        for (const Base &b : bases)
        {
            json j;
            j["id"] = b.id;
            j["name"] = b.name;
            j["body"] = b.body;
            j["latitude"] = b.latitude;
            j["longitude"] = b.longitude;
            j["heading"] = b.heading;
            j["flat_radius"] = b.flat_radius;
            j["blend"] = b.blend;
            if (b.elevation != 0.0)
                j["elevation"] = b.elevation;
            j["pad"] = {{"x", b.pad_x}, {"z", b.pad_z}, {"heading", b.pad_heading}};
            if (!b.runways.empty())
                j["runways"] = json::array();
            for (const Runway &r : b.runways)
            {
                json jr = {{"name", r.name}, {"x", r.x}, {"z", r.z}, {"heading", r.heading}, {"length", r.length}, {"width", r.width}};
                if (!r.ils.empty())
                    jr["ils"] = json::array();
                for (const RunwayIls &i : r.ils)
                    jr["ils"].push_back({{"frequency", i.frequency}, {"reverse", i.reverse}});
                j["runways"].push_back(jr);
            }
            if (!b.navaids.empty())
                j["navaids"] = json::array();
            for (const BaseNavaid &n : b.navaids)
            {
                json jn = {{"type", n.type}, {"id", n.id}, {"name", n.name}, {"x", n.x}, {"z", n.z}, {"frequency", n.frequency}, {"heading", n.heading}, {"glide", n.glide}};
                if (n.range > 0.0)
                    jn["range"] = n.range;
                j["navaids"].push_back(jn);
            }
            if (!b.cameras.empty())
                j["cameras"] = json::array();
            for (const BaseCamera &c : b.cameras)
                j["cameras"].push_back({{"name", c.name}, {"x", c.x}, {"y", c.y}, {"z", c.z}, {"fov", c.fov}, {"track", c.track}, {"yaw", c.yaw}, {"pitch", c.pitch}});
            if (!b.lights.empty())
                j["lights"] = json::array();
            for (const LightSpec &l : b.lights)
                j["lights"].push_back(light_to_json(l));
            if (!b.objects.empty())
                j["objects"] = json::array();
            for (const BaseObject &o : b.objects)
            {
                json jo = {{"type", o.type}, {"x", o.x}, {"z", o.z}};
                if (!o.model.empty())
                    jo["model"] = o.model;
                const auto put = [&](const char *key, double v, double def) { if (v != def) jo[key] = v; };
                put("y", o.y, 0.0);
                put("heading", o.heading, 0.0);
                put("scale", o.scale, 1.0);
                if (o.count != 1)
                    jo["count"] = o.count;
                put("step_x", o.step_x, 0.0);
                put("step_z", o.step_z, 0.0);
                put("size", o.size, 0.0);
                put("width", o.width, 0.0);
                put("depth", o.depth, 0.0);
                put("height", o.height, 0.0);
                put("length", o.length, 0.0);
                put("radius", o.radius, 0.0);
                if (o.number != 0)
                    jo["number"] = o.number;
                if (o.seed != 1u)
                    jo["seed"] = o.seed;
                if (o.has_colour)
                    jo["colour"] = colour(o.colour);
                if (o.has_colour2)
                    jo["colour2"] = colour(o.colour2);
                j["objects"].push_back(jo);
            }
            root["bases"].push_back(j);
        }
        return root.dump(2) + "\n";
    }

    bool save_bases(const std::string &file, const std::vector<Base> &bases, std::string &error)
    {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(file).parent_path(), ec);
        std::ofstream out(file);
        if (!out)
        {
            error = "cannot write " + file;
            return false;
        }
        out << bases_to_json(bases);
        return true;
    }

    void base_pad(const Base &base, double radius, sl::dvec3 &direction, double &heading)
    {
        direction = base_point(base, radius, base.pad_x, base.pad_z);
        heading = base.heading + base.pad_heading;
    }

    namespace
    {
        void lat_lon_of(const Universe &universe, int body, const Base &base, double x, double z, double &latitude, double &longitude)
        {
            const double radius = universe.bodies[static_cast<std::size_t>(body)].radius;
            surface_lat_lon(universe, body, base_point(base, radius, x, z), latitude, longitude);
        }

        // Runway ends in the base frame: the threshold a ship lands over first, and the far end.
        void runway_ends(const Runway &rw, bool reverse, double &x0, double &z0, double &x1, double &z1)
        {
            const double h = rw.heading * pi / 180.0, half = 0.5 * rw.length;
            const double dx = std::sin(h) * half, dz = std::cos(h) * half;
            const double sign = reverse ? -1.0 : 1.0;
            x0 = rw.x - sign * dx;
            z0 = rw.z - sign * dz;
            x1 = rw.x + sign * dx;
            z1 = rw.z + sign * dz;
        }
    }

    void register_base_beacons(NavSystem &nav, const Universe &universe, int body, const Base &base)
    {
        for (const BaseNavaid &n : base.navaids)
        {
            Transmitter t;
            t.type = n.type;
            t.id = n.id;
            t.name = n.name;
            t.body = base.body;
            t.frequency = n.frequency;
            t.heading = base.heading + n.heading;
            t.glide = n.glide;
            t.range = n.range > 0.0 ? n.range : (n.type == "vor" ? 2.0e6 : n.type == "ndb" ? 4.0e5 : 3.0e5);
            double x = n.x, z = n.z;
            if (n.type == "vtol" && x == 0.0 && z == 0.0)
                x = base.pad_x, z = base.pad_z;
            lat_lon_of(universe, body, base, x, z, t.latitude, t.longitude);
            nav.add_beacon(t);
        }
        for (std::size_t r = 0; r < base.runways.size(); ++r)
        {
            const Runway &rw = base.runways[r];
            for (const RunwayIls &ils : rw.ils)
            {
                double x0, z0, x1, z1;
                runway_ends(rw, ils.reverse, x0, z0, x1, z1);
                // The localizer course and glideslope both radiate from the touchdown zone, 300 m past the threshold.
                const double f = std::min(300.0, 0.3 * rw.length) / rw.length;
                Transmitter t;
                t.type = "ils";
                t.id = base.id + "-ils-" + std::to_string(r) + (ils.reverse ? "r" : "f");
                t.name = base.name + " " + rw.name + (ils.reverse ? " (rev)" : "") + " ILS";
                t.body = base.body;
                t.frequency = ils.frequency;
                t.range = 3.0e5;
                t.glide = 3.0;
                const double h = std::atan2(x1 - x0, z1 - z0) * 180.0 / pi;
                t.heading = base.heading + h;
                lat_lon_of(universe, body, base, x0 + (x1 - x0) * f, z0 + (z1 - z0) * f, t.latitude, t.longitude);
                nav.add_beacon(t);
            }
        }
    }

    void base_runway_start(const Base &base, const Runway &runway, bool reverse, double radius, sl::dvec3 &direction, double &heading, double along)
    {
        double x0, z0, x1, z1;
        runway_ends(runway, reverse, x0, z0, x1, z1);
        const double f = along / runway.length;
        direction = base_point(base, radius, x0 + (x1 - x0) * f, z0 + (z1 - z0) * f);
        heading = base.heading + std::atan2(x1 - x0, z1 - z0) * 180.0 / pi;
    }

    void base_camera_pose(const Universe &universe, const Base &base, const BaseCamera &camera, const sl::dvec3 *target, sl::dvec3 &eye, sl::dvec3 &forward, sl::dvec3 &up)
    {
        const int body = universe.find(base.body);
        const double radius = body >= 0 ? universe.bodies[static_cast<std::size_t>(body)].radius : 0.0;
        const sl::dvec3 centre = body >= 0 ? universe.position(body) : sl::dvec3(0.0);
        const Frame f = local_frame(base.direction, base.pole);
        sl::dvec3 base_forward, base_right;
        base_axes(f, base.heading, base_forward, base_right);
        up = f.up;
        eye = centre + base.direction * (radius + base.height) + base_right * camera.x + up * camera.y + base_forward * camera.z;
        if (camera.track)
        {
            // Follow the ship while it can be seen: close by, or above the local horizon within a few thousand km.
            // Otherwise (the ship is at another site or body) a tracking camera would stare into the ground, so look at the pad.
            if (target)
            {
                const sl::dvec3 to_target = *target - eye;
                const double distance = glm::length(to_target);
                if (distance > 1e-3 && (distance < 20.0e3 || (distance < 5.0e6 && glm::dot(to_target / distance, up) > 0.02)))
                {
                    forward = to_target / distance;
                    return;
                }
            }
            const sl::dvec3 pad = centre + base_point(base, radius, base.pad_x, base.pad_z) * (radius + base.height + 15.0);
            forward = glm::normalize(pad - eye);
            return;
        }
        const double yaw = camera.yaw * pi / 180.0, pitch = camera.pitch * pi / 180.0;
        forward = glm::normalize((base_forward * std::cos(yaw) + base_right * std::sin(yaw)) * std::cos(pitch) + up * std::sin(pitch));
    }

    std::vector<FlattenSite> flatten_sites(const Base &base, double radius)
    {
        std::vector<FlattenSite> sites;
        sites.push_back({base.direction, base.direction, base.height, base.flat_radius, base.blend});
        for (const Runway &rw : base.runways)
        {
            double x0, z0, x1, z1;
            runway_ends(rw, false, x0, z0, x1, z1);
            const double margin = 0.5 * rw.width + 30.0;
            sites.push_back({base_point(base, radius, x0, z0), base_point(base, radius, x1, z1), base.height, margin, 150.0});
        }
        return sites;
    }

    FlattenSource::FlattenSource(std::shared_ptr<sl::TerrainSource> source, double radius, std::vector<Site> sites)
        : source_(std::move(source)), radius_(radius), sites_(std::move(sites))
    {
        min_ = source_->min_height();
        max_ = source_->max_height();
        for (const Site &s : sites_)
        {
            min_ = std::min(min_, s.height);
            max_ = std::max(max_, s.height);
        }
    }

    double FlattenSource::apply(const sl::dvec3 &direction, double natural) const
    {
        double result = natural;
        for (const Site &s : sites_)
        {
            const sl::dvec3 p = direction * radius_, a = s.a * radius_, ab = s.b * radius_ - a;
            const double len2 = glm::dot(ab, ab);
            const double u = len2 > 0.0 ? std::clamp(glm::dot(p - a, ab) / len2, 0.0, 1.0) : 0.0;
            const double distance = glm::length(p - (a + ab * u));
            if (distance >= s.flat + s.blend)
                continue;
            const double t = distance <= s.flat ? 0.0 : smooth((distance - s.flat) / std::max(s.blend, 1.0));
            result = s.height + (result - s.height) * t;
        }
        return result;
    }

    double FlattenSource::height_on_sphere(const sl::dvec3 &direction, double radius) const
    {
        return apply(direction, source_->height_on_sphere(direction, radius));
    }

    double FlattenSource::height_on_sphere_lod(const sl::dvec3 &direction, double radius, double cell_size) const
    {
        return apply(direction, source_->height_on_sphere_lod(direction, radius, cell_size));
    }

    BaseRenderer::~BaseRenderer()
    {
        release();
    }

    void BaseRenderer::release()
    {
        for (auto &entry : meshes_)
            if (entry.second)
                sl::destroy_mesh(entry.second);
        meshes_.clear();
        for (auto &entry : models_)
            if (entry.second.model)
                sl::destroy_model(entry.second.model);
        models_.clear();
    }

    sl::Model *BaseRenderer::model_for(const Base &base, const BaseObject &object)
    {
        const std::string key = base.source_dir + "|" + object.model;
        const auto found = models_.find(key);
        if (found != models_.end())
            return found->second.model;
        LoadedModel loaded;
        loaded.path = resolve_base_model(base, object.model);
        if (!loaded.path.empty())
        {
            std::error_code ec;
            loaded.stamp = std::filesystem::last_write_time(loaded.path, ec);
            loaded.model = sl::load_model(loaded.path);
            if (!loaded.model)
                std::fprintf(stderr, "[kosmos] cannot load base model %s: %s\n", loaded.path.c_str(), sl::last_error().c_str());
            else
                for (sl::ModelPart &part : loaded.model->parts)
                    part.material.cull = sl::CullMode::none;
        }
        else
            std::fprintf(stderr, "[kosmos] base %s: model '%s' not found\n", base.id.c_str(), object.model.c_str());
        models_[key] = loaded;
        return loaded.model;
    }

    // A model file that changed on disk (saved from Blender, say) is dropped and read again at its next use.
    void BaseRenderer::reload_changed_models()
    {
        const auto now = std::filesystem::file_time_type::clock::now();
        for (auto it = models_.begin(); it != models_.end();)
        {
            std::error_code ec;
            const auto stamp = it->second.path.empty() ? it->second.stamp : std::filesystem::last_write_time(it->second.path, ec);
            if (!ec && !it->second.path.empty() && stamp != it->second.stamp && now - stamp > std::chrono::milliseconds(600))
            {
                if (it->second.model)
                    sl::destroy_model(it->second.model);
                it = models_.erase(it);
            }
            else if (it->second.path.empty() && !it->second.model && std::filesystem::exists(std::filesystem::path(it->first.substr(0, it->first.find('|'))) / it->first.substr(it->first.find('|') + 1), ec))
                it = models_.erase(it); // a model that was missing may have appeared
            else
                ++it;
        }
    }

    double base_night(const Universe &universe, const Base &base)
    {
        if (universe.bodies.empty())
            return 0.0;
        const int body = universe.find(base.body);
        if (body < 0)
            return 0.0;
        const sl::dvec3 here = universe.position(body) + base.direction * universe.bodies[static_cast<std::size_t>(body)].radius;
        const sl::dvec3 to_sun = universe.position(0) - here;
        const double height = glm::dot(base.direction, glm::normalize(to_sun));
        const double t = std::clamp((0.10 - height) / 0.16, 0.0, 1.0);
        return t * t * (3.0 - 2.0 * t);
    }

    void collect_base_lights(const Universe &universe, const std::vector<Base> &bases, const sl::dvec3 &eye, double range, double time,
                             const std::vector<sl::dvec3> &ships, std::vector<PlacedLight> &out)
    {
        for (const Base &base : bases)
        {
            if (base.lights.empty())
                continue;
            const int body = universe.find(base.body);
            if (body < 0)
                continue;
            const sl::dvec3 centre = universe.position(body);
            const double radius = universe.bodies[static_cast<std::size_t>(body)].radius;
            if (glm::length(centre + base.direction * (radius + base.height) - eye) > range + 2.0 * base.flat_radius + base.blend)
                continue;
            const double night = base_night(universe, base);
            const Frame f = local_frame(base.direction, base.pole);
            sl::dvec3 forward, right;
            base_axes(f, base.heading, forward, right);
            const auto world_point = [&](double x, double y, double z)
            {
                const sl::dvec3 dir = glm::normalize(base.direction + (right * x + forward * z) / radius);
                return centre + dir * (radius + base.height) + local_frame(dir, base.pole).up * y;
            };
            for (const LightSpec &spec : base.lights)
                for (int i = 0; i < spec.count; ++i)
                {
                    const double x = spec.x + spec.step_x * i, y = spec.y + spec.step_y * i, z = spec.z + spec.step_z * i;
                    const sl::dvec3 at = world_point(x, y, z);
                    const double distance = glm::length(at - eye);
                    if (distance > range)
                        continue;
                    const float on = light_pattern(spec, time + i * 0.37) * (spec.always ? 1.0f : static_cast<float>(night));
                    if (on <= 0.001f)
                        continue;
                    PlacedLight p;
                    p.light.position = glm::vec3(at - eye);
                    p.light.colour = glm::vec3(spec.colour.red, spec.colour.green, spec.colour.blue) / 255.0f;
                    p.light.intensity = spec.intensity * on;
                    p.light.range = spec.range;
                    p.light.inner_angle = spec.inner;
                    p.light.outer_angle = spec.outer;
                    p.light.cast_shadow = spec.shadow;
                    if (spec.outer > 0.0f)
                    {
                        sl::dvec3 target = at + (right * spec.dx + f.up * spec.dy + forward * spec.dz);
                        if (spec.has_aim)
                            target = world_point(spec.ax, spec.ay, spec.az);
                        if (spec.aim_ship)
                        {
                            // A ship on the pad takes the beam; with none near, it rests on the fixed aim point.
                            double best = 400.0;
                            for (const sl::dvec3 &s : ships)
                                if (glm::length(s - at) < best)
                                {
                                    best = glm::length(s - at);
                                    target = s;
                                }
                        }
                        p.light.direction = glm::vec3(glm::normalize(target - at));
                    }
                    p.glow_colour = p.light.colour;
                    p.glow_size = spec.glow;
                    p.glow_strength = on;
                    p.distance = static_cast<float>(distance);
                    out.push_back(p);
                }
        }
    }

    sl::Mesh *BaseRenderer::mesh_for(const BaseObject &object)
    {
        const std::string key = key_of(object);
        const auto found = meshes_.find(key);
        if (found != meshes_.end())
            return found->second;
        sl::Mesh *mesh = nullptr;
        const sl::MeshData data = build(object);
        if (!data.indices.empty())
            mesh = sl::create_mesh(data.vertices, data.indices);
        meshes_[key] = mesh;
        return mesh;
    }

    void BaseRenderer::draw(const Universe &universe, const std::vector<Base> &bases, const sl::dvec3 &eye, double range, const sl::Material &material)
    {
        const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (wall - last_poll_ > 0.5)
        {
            last_poll_ = wall;
            reload_changed_models();
        }
        for (const Base &base : bases)
        {
            const int body = universe.find(base.body);
            if (body < 0)
                continue;
            const sl::dvec3 centre = universe.position(body);
            const double radius = universe.bodies[static_cast<std::size_t>(body)].radius;
            const sl::dvec3 base_world = centre + base.direction * (radius + base.height);
            if (glm::length(base_world - eye) > range + 2.0 * base.flat_radius + base.blend)
                continue;
            const Frame f = local_frame(base.direction, base.pole);
            sl::dvec3 forward, right;
            base_axes(f, base.heading, forward, right);
            for (const Runway &rw : base.runways)
            {
                const std::string key = "runway|" + std::to_string(rw.length) + "|" + std::to_string(rw.width);
                sl::Mesh *&mesh = meshes_[key];
                if (!mesh)
                {
                    const sl::MeshData data = make_runway(static_cast<float>(rw.length), static_cast<float>(rw.width));
                    mesh = sl::create_mesh(data.vertices, data.indices);
                }
                const sl::dvec3 dir = base_point(base, radius, rw.x, rw.z);
                const sl::dvec3 position = centre + dir * (radius + base.height);
                if (glm::length(position - eye) > range + 0.5 * rw.length)
                    continue;
                const Frame pf = local_frame(dir, base.pole);
                sl::dvec3 pforward, pright;
                base_axes(pf, base.heading + rw.heading, pforward, pright);
                glm::mat4 model(1.0f);
                model[0] = glm::vec4(glm::vec3(-pright), 0.0f);
                model[1] = glm::vec4(glm::vec3(pf.up), 0.0f);
                model[2] = glm::vec4(glm::vec3(pforward), 0.0f);
                model[3] = glm::vec4(glm::vec3(position + pf.up * 0.25 - eye), 1.0f);
                sl::draw_mesh(mesh, model, material);
            }
            for (const BaseObject &o : base.objects)
            {
                const bool is_model = o.type == "model";
                sl::Mesh *mesh = is_model ? nullptr : mesh_for(o);
                sl::Model *model = is_model ? model_for(base, o) : nullptr;
                if (!mesh && !model)
                    continue;
                for (int i = 0; i < o.count; ++i)
                {
                    const double x = o.x + o.step_x * i, z = o.z + o.step_z * i;
                    const sl::dvec3 dir = glm::normalize(base.direction + (right * x + forward * z) / radius);
                    const sl::dvec3 position = centre + dir * (radius + base.height);
                    if (glm::length(position - eye) > range)
                        continue;
                    const Frame pf = local_frame(dir, base.pole);
                    sl::dvec3 pforward, pright;
                    base_axes(pf, base.heading + o.heading, pforward, pright);
                    // Right-handed basis (a mirrored one would flip the winding): +Z faces forward, so +X is to the left.
                    const glm::vec3 ax(-pright), ay(pf.up), az(pforward);
                    glm::mat4 placed(1.0f);
                    placed[0] = glm::vec4(ax * static_cast<float>(o.scale), 0.0f);
                    placed[1] = glm::vec4(ay * static_cast<float>(o.scale), 0.0f);
                    placed[2] = glm::vec4(az * static_cast<float>(o.scale), 0.0f);
                    placed[3] = glm::vec4(glm::vec3(position + pf.up * o.y - eye), 1.0f);
                    if (model)
                        sl::draw_model(model, placed);
                    else
                        sl::draw_mesh(mesh, placed, material);
                }
            }
        }
    }
}
