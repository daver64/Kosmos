#pragma once

#include "app.h"
#include "procedural.h"

#include <array>
#include <fstream>
#include <sstream>

namespace kosmos::app
{
    // A props-library part moved into the ship frame: the prop's +Y becomes the nose direction (-Z) and its +Z umbilical faces +Y.
    bool make_prop_data(const kosmos::PartSpec &part, sl::MeshData &data);

    // A props-library part moved into the ship frame: the prop's +Y becomes the nose direction (-Z) and its +Z umbilical faces +Y.
    sl::Mesh *make_prop_mesh(const kosmos::PartSpec &part);

    // A profile of revolution about Z as mesh data, in the part's colour; the same shape the lathe draws.
    sl::MeshData make_lathe_data(const kosmos::PartSpec &part, int segments = 28);

    struct Visuals
    {
        std::vector<sl::Mesh *> limb_pool;
        std::vector<kosmos::HazeMesh> haze_pool;
        sl::Mesh *bolt_meshes[kosmos::Lightning::max_bolts] = {}; // one lightning bolt each, rebuilt each frame it shows
        sl::Bitmap *bolt_bitmaps[kosmos::Lightning::bolt_textures] = {};
        bool bolt_bitmaps_tried = false;
        sl::Mesh *rain_mesh = nullptr; // falling rain: camera-facing streaks in the space around the eye
        sl::dvec3 rain_offset{0.0};
        sl::Mesh *local_cloud = nullptr; // the weather cloud deck below 100 km, rebuilt every frame
        sl::Mesh *sphere = nullptr, *glow = nullptr, *quad = nullptr, *cloud_shell = nullptr, *starfield = nullptr,  *tail = nullptr, *screen_quad = nullptr, *cockpit_box = nullptr;
        std::vector<sl::Mesh *> rings;
        std::vector<sl::Mesh *> body_cloud_meshes;
        std::vector<sl::Bitmap *> body_cloud_textures;
        sl::Bitmap *glow_texture = nullptr, *plasma_texture = nullptr, *puff_texture = nullptr, *cloud_texture = nullptr, *lights_texture = nullptr;
        sl::Bitmap *star_surface = nullptr, *star_filaments = nullptr, *star_corona = nullptr, *star_corona_far = nullptr, *star_rays = nullptr, *star_limb = nullptr, *sky = nullptr, *nebula_atlas = nullptr;
        // The nebula_atlas bitmap holds the current system's nebulae, rebuilt when the system changes.
        int nebulae_system = -1;
        std::vector<sl::Bitmap *> body_textures;
        // Optional per-body shape replacing the sphere; entries may be null.
        std::vector<sl::Model *> body_meshes;
        // Optional per-body normal maps (fake relief for airless moons); entries may be null.
        std::vector<sl::Bitmap *> body_normals;
        std::unique_ptr<sl::PlanetTiles> earth_tiles;
        std::unique_ptr<sl::PlanetTiles> earth_cloud_tiles; // the streamed cloud layer (Orbiter's cloud tree), in place of the single image shell
        std::vector<int> surface_textured;
        std::map<const kosmos::PartSpec *, sl::Mesh *> parts;

        sl::Mesh *part_mesh(const kosmos::PartSpec &part)
        {
            auto found = parts.find(&part);
            if (found != parts.end())
                return found->second;
            sl::Mesh *mesh = part.prop.empty() ? fx::make_lathe_mesh(part.profile, 28) : make_prop_mesh(part);
            parts[&part] = mesh;
            return mesh;
        }
        struct InteriorMeshes
        {
            sl::Mesh *outside = nullptr, *walls = nullptr, *floor = nullptr, *ceiling = nullptr, *glass = nullptr;
            sl::Model *model = nullptr; // a modder's mesh, which replaces the generated walls
        };
        // Wall, floor and ceiling surfaces of generated interiors (data/textures/interior, see tools/bake_interior_textures.py).
        struct InteriorSurface
        {
            sl::Bitmap *colour = nullptr, *normal = nullptr, *metal_rough = nullptr;
        };
        std::array<InteriorSurface, 3> interior_surfaces{}; // wall, floor, ceiling
        bool interior_surfaces_loaded = false;
        const std::array<InteriorSurface, 3> &interior_textures(const std::string &root)
        {
            if (!interior_surfaces_loaded)
            {
                interior_surfaces_loaded = true;
                const char *names[3] = {"wall", "floor", "ceiling"};
                for (int i = 0; i < 3; ++i)
                {
                    const std::string base = root + "/data/textures/interior/" + names[i];
                    interior_surfaces[static_cast<std::size_t>(i)].colour = sl::load_bitmap((base + ".png").c_str());
                    interior_surfaces[static_cast<std::size_t>(i)].normal = sl::load_bitmap((base + "_normal.png").c_str());
                    if (i != 1)
                        interior_surfaces[static_cast<std::size_t>(i)].metal_rough = sl::load_bitmap((base + "_mr.png").c_str());
                }
            }
            return interior_surfaces;
        }
        std::map<const kosmos::Interior *, InteriorMeshes> interiors;
        const InteriorMeshes &interior_meshes(const kosmos::Interior &interior)
        {
            auto found = interiors.find(&interior);
            if (found != interiors.end())
                return found->second;
            sl::MeshData outside, walls, floor, ceiling, glass;
            interior.build_meshes(outside, walls, floor, ceiling, glass);
            InteriorMeshes made;
            if (!glass.indices.empty())
                made.glass = sl::create_mesh(glass.vertices, glass.indices);
            if (!interior.mesh_path.empty())
            {
                made.model = sl::load_model(interior.mesh_path);
                if (!made.model)
                    std::fprintf(stderr, "[kosmos] cannot load interior mesh %s: %s\n", interior.mesh_path.c_str(), sl::last_error().c_str());
                else
                    for (sl::ModelPart &mp : made.model->parts)
                        mp.material.cull = sl::CullMode::none;
            }
            if (!outside.indices.empty())
                made.outside = sl::create_mesh(outside.vertices, outside.indices);
            if (!walls.indices.empty())
                made.walls = sl::create_mesh(walls.vertices, walls.indices);
            if (!floor.indices.empty())
                made.floor = sl::create_mesh(floor.vertices, floor.indices);
            if (!ceiling.indices.empty())
                made.ceiling = sl::create_mesh(ceiling.vertices, ceiling.indices);
            return interiors[&interior] = made;
        }
        std::map<const kosmos::Cockpit *, sl::Model *> vc_models;
        // The MFD screen triangles of a virtual cockpit (ship frame, with the OBJ texture coordinates), for picking buttons.
        struct VcTri
        {
            glm::vec3 p[3];
            glm::vec2 uv[3];
        };
        std::map<const kosmos::Cockpit *, std::array<std::vector<VcTri>, 2>> vc_screens;
        const std::array<std::vector<VcTri>, 2> &vc_screen_tris(const kosmos::Cockpit &cockpit, const std::string &directory)
        {
            vc_model(cockpit, directory);
            return vc_screens[&cockpit];
        }
        static std::array<std::vector<VcTri>, 2> parse_vc_screens(const std::string &path)
        {
            std::array<std::vector<VcTri>, 2> out;
            std::ifstream in(path);
            std::vector<glm::vec3> pos;
            std::vector<glm::vec2> tex;
            int side = -1;
            std::string object, material;
            for (std::string line; std::getline(in, line);)
            {
                std::istringstream ls(line);
                std::string tag;
                ls >> tag;
                if (tag == "v")
                {
                    glm::vec3 v;
                    ls >> v.x >> v.y >> v.z;
                    pos.push_back(v);
                }
                else if (tag == "vt")
                {
                    glm::vec2 v;
                    ls >> v.x >> v.y;
                    tex.push_back(v);
                }
                else if (tag == "usemtl" || tag == "o")
                {
                    std::string name;
                    ls >> name;
                    (tag == "o" ? object : material) = name;
                    // A part is named by its o object, or by its material when the file has no o lines, as in simlib's loader.
                    const std::string &part = object.empty() ? material : object;
                    side = part.find("LMFD_DISPLAY") != std::string::npos ? 0 : part.find("RMFD_DISPLAY") != std::string::npos ? 1 : -1;
                }
                else if (tag == "f" && side >= 0)
                {
                    std::vector<std::pair<int, int>> corner;
                    for (std::string c; ls >> c;)
                    {
                        const std::size_t a = c.find('/'), b = c.find('/', a + 1);
                        const int vi = std::atoi(c.c_str()) - 1;
                        const int ti = a == std::string::npos ? -1 : std::atoi(c.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1).c_str()) - 1;
                        corner.push_back({vi, ti});
                    }
                    for (std::size_t k = 2; k < corner.size(); ++k)
                    {
                        const std::pair<int, int> *tri[3] = {&corner[0], &corner[k - 1], &corner[k]};
                        VcTri t;
                        bool ok = true;
                        for (int i = 0; i < 3; ++i)
                        {
                            if (tri[i]->first < 0 || tri[i]->first >= static_cast<int>(pos.size()) || tri[i]->second < 0 || tri[i]->second >= static_cast<int>(tex.size()))
                            {
                                ok = false;
                                break;
                            }
                            t.p[i] = pos[tri[i]->first];
                            t.uv[i] = tex[tri[i]->second];
                        }
                        if (ok)
                            out[side].push_back(t);
                    }
                }
            }
            return out;
        }
        sl::Model *vc_model(const kosmos::Cockpit &cockpit, const std::string &directory)
        {
            auto found = vc_models.find(&cockpit);
            if (found != vc_models.end())
                return found->second;
            const std::string path = (std::filesystem::path(directory) / cockpit.vc_model).string();
            sl::Model *model = sl::load_model(path);
            if (!model)
                std::fprintf(stderr, "[kosmos] cannot load cockpit model %s: %s\n", path.c_str(), sl::last_error().c_str());
            else
            {
                for (sl::ModelPart &mp : model->parts)
                    mp.material.cull = sl::CullMode::none;
                vc_screens[&cockpit] = parse_vc_screens(path);
            }
            return vc_models[&cockpit] = model;
        }
        struct LoadedModel
        {
            sl::Model *model = nullptr;
            std::filesystem::path path;
            std::filesystem::file_time_type stamp{};
        };
        std::map<const kosmos::PartSpec *, LoadedModel> models;
        // A parachute's canopy model is loaded through the same cache, as a stand-in part.
        std::map<const kosmos::ParachuteSpec *, kosmos::PartSpec> canopy_parts;
        sl::Model *canopy_model(const kosmos::ParachuteSpec &chute, const std::string &directory)
        {
            auto found = canopy_parts.find(&chute);
            if (found == canopy_parts.end())
            {
                kosmos::PartSpec stand_in;
                stand_in.model = chute.model;
                found = canopy_parts.emplace(&chute, stand_in).first;
            }
            return part_model(found->second, directory);
        }
        void configure_part_model(sl::Model *model)
        {
            if (!model)
                return;
            for (sl::ModelPart &mp : model->parts)
            {
                const bool nose_shell = mp.name.rfind("NCone", 0) == 0 && mp.name != "NConeDock";
                // The NCone texture layers are reverse-wound copies; cull front to keep the exterior layer visible.
                mp.material.cull = nose_shell ? sl::CullMode::front : sl::CullMode::none;
                if (mp.name.rfind("vessel", 0) == 0 || mp.name.rfind("vent", 0) == 0 || mp.name.rfind("crew", 0) == 0)
                    mp.visible = false;
            }
        }
        sl::Model *part_model(const kosmos::PartSpec &part, const std::string &directory)
        {
            auto found = models.find(&part);
            if (found != models.end())
                return found->second.model;
            LoadedModel loaded;
            loaded.path = std::filesystem::path(directory) / part.model;
            std::error_code ec;
            loaded.stamp = std::filesystem::last_write_time(loaded.path, ec);
            loaded.model = sl::load_model(loaded.path.string());
            if (!loaded.model)
                std::fprintf(stderr, "[kosmos] cannot load part model %s: %s\n", part.model.c_str(), sl::last_error().c_str());
            if (loaded.model && !part.animations.empty() && !sl::load_model_animations_json(loaded.model, (std::filesystem::path(directory) / part.animations).string()))
                std::fprintf(stderr, "[kosmos] cannot load animations %s: %s\n", part.animations.c_str(), sl::last_error().c_str());
            configure_part_model(loaded.model);
            models[&part] = loaded;
            return loaded.model;
        }
        // A part's model with a skin's textures laid over it, loaded once per part and skin.
        std::map<std::pair<const kosmos::PartSpec *, std::string>, sl::Model *> skinned_models;
        sl::Model *skinned_part_model(const kosmos::PartSpec &part, const kosmos::ShipSpec &spec, const std::string &skin);
        // Drops models whose file changed on disk so the next draw loads them again; this is how an edit made in
        // Blender appears. A file is left alone until it has stopped changing, as exporters write it in pieces.
        int reload_models()
        {
            int dropped = 0;
            const auto now = std::filesystem::file_time_type::clock::now();
            for (auto it = models.begin(); it != models.end();)
            {
                std::error_code ec;
                const auto stamp = std::filesystem::last_write_time(it->second.path, ec);
                if (!ec && stamp != it->second.stamp && now - stamp > std::chrono::milliseconds(600))
                {
                    if (it->second.model)
                        sl::destroy_model(it->second.model);
                    it = models.erase(it);
                    ++dropped;
                }
                else
                    ++it;
            }
            return dropped;
        }
        void clear_parts()
        {
            for (auto &entry : models)
                if (entry.second.model)
                    sl::destroy_model(entry.second.model);
            models.clear();
            for (auto &entry : skinned_models)
                if (entry.second)
                    sl::destroy_model(entry.second);
            skinned_models.clear();
            for (auto &entry : parts)
                if (entry.second)
                    sl::destroy_mesh(entry.second);
            parts.clear();
        }
    };

    // Drives a model's animations: simlib JSON animations read these named properties, and a glTF clip called "gear" is posed from
    // gear-up (start) to gear-down (end).
    void pose_model(sl::Model *model, double gear, double pod_left = 0.0, double pod_right = 0.0, const kosmos::Ship *ship = nullptr);

    bool hud_allowed(sl::HudMode mode, const std::vector<std::string> &modes);

    /** The mode after `current` in the ship's list, going off after the last. */
    sl::HudMode next_hud_mode(sl::HudMode current, const std::vector<std::string> &modes);

    void draw_parts(Visuals &v, const kosmos::ShipSpec &spec, int first_stage, int last_stage, const glm::mat4 &model, bool lit = true, double gear = 1.0, double pod_left = 0.0, double pod_right = 0.0, const kosmos::Ship *ship = nullptr, int part_index = -1);
} // namespace kosmos::app
