#pragma once

#include "app.h"

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
        sl::Mesh *bolt_mesh = nullptr; // the lightning bolt, rebuilt each frame it shows
        sl::Mesh *rain_mesh = nullptr; // falling rain: camera-facing streaks in the space around the eye
        sl::dvec3 rain_offset{0.0};
        sl::Mesh *local_cloud = nullptr; // the weather cloud deck below 100 km, rebuilt every frame
        sl::Mesh *sphere = nullptr, *glow = nullptr, *quad = nullptr, *cloud_shell = nullptr, *starfield = nullptr,  *tail = nullptr, *screen_quad = nullptr, *cockpit_box = nullptr;
        std::vector<sl::Mesh *> rings;
        std::vector<sl::Mesh *> body_cloud_meshes;
        std::vector<sl::Bitmap *> body_cloud_textures;
        sl::Bitmap *glow_texture = nullptr, *puff_texture = nullptr, *cloud_texture = nullptr, *lights_texture = nullptr;
        sl::Bitmap *star_surface = nullptr, *star_filaments = nullptr, *star_corona = nullptr, *star_corona_far = nullptr, *star_rays = nullptr, *star_limb = nullptr, *sky = nullptr, *nebula_atlas = nullptr;
        sl::Mesh *nebulae = nullptr; // nebulae of the current system, rebuilt when it changes
        int nebulae_system = -1;
        std::vector<sl::Bitmap *> body_textures;
        // Optional per-body shape replacing the sphere; entries may be null.
        std::vector<sl::Model *> body_meshes;
        // Optional per-body normal maps (fake relief for airless moons); entries may be null.
        std::vector<sl::Bitmap *> body_normals;
        std::unique_ptr<sl::PlanetTiles> earth_tiles;
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
            sl::Mesh *outside = nullptr, *inside = nullptr;
        };
        std::map<const kosmos::Interior *, InteriorMeshes> interiors;
        const InteriorMeshes &interior_meshes(const kosmos::Interior &interior)
        {
            auto found = interiors.find(&interior);
            if (found != interiors.end())
                return found->second;
            sl::MeshData outside, inside;
            interior.build_meshes(outside, inside);
            InteriorMeshes made;
            if (!outside.indices.empty())
                made.outside = sl::create_mesh(outside.vertices, outside.indices);
            if (!inside.indices.empty())
                made.inside = sl::create_mesh(inside.vertices, inside.indices);
            return interiors[&interior] = made;
        }
        struct LoadedModel
        {
            sl::Model *model = nullptr;
            std::filesystem::path path;
            std::filesystem::file_time_type stamp{};
        };
        std::map<const kosmos::PartSpec *, LoadedModel> models;
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
            if (loaded.model)
                for (sl::ModelPart &mp : loaded.model->parts)
                {
                    mp.material.cull = sl::CullMode::none; // Orbiter draws meshes double-sided and many are not closed
                    if (mp.name.rfind("vessel", 0) == 0 || mp.name.rfind("vent", 0) == 0 || mp.name.rfind("crew", 0) == 0)
                        mp.visible = false; // systems markers, not geometry
                }
            models[&part] = loaded;
            return loaded.model;
        }
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

    void draw_parts(Visuals &v, const kosmos::ShipSpec &spec, int first_stage, int last_stage, const glm::mat4 &model, bool lit = true, double gear = 1.0, double pod_left = 0.0, double pod_right = 0.0, const kosmos::Ship *ship = nullptr);
} // namespace kosmos::app
