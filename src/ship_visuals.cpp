#include "ship_visuals.h"

namespace kosmos::app
{
    // A props-library part moved into the ship frame: the prop's +Y becomes the nose direction (-Z) and its +Z umbilical faces +Y.
    bool make_prop_data(const kosmos::PartSpec &part, sl::MeshData &data)
    {
        auto colour = [](const float c[3]) { return sl::Colour{static_cast<std::uint8_t>(c[0] * 255.0f), static_cast<std::uint8_t>(c[1] * 255.0f), static_cast<std::uint8_t>(c[2] * 255.0f), 255}; };
        const sl::Colour body = colour(part.colour), accent = colour(part.accent);
        if (part.prop == "stage")
            data = sl::make_rocket_stage_data(part.radius, part.length, body, accent);
        else if (part.prop == "adapter")
            data = sl::make_rocket_adapter_data(part.radius, part.top_radius, part.length, body, accent);
        else if (part.prop == "engines")
            data = sl::make_rocket_engines_data(part.radius, part.count, part.length);
        else if (part.prop == "fins")
            data = sl::make_rocket_fins_data(part.radius, part.span, part.length, part.count, body);
        else if (part.prop == "fairing")
            data = sl::make_payload_fairing_data(part.radius, part.length, body, accent);
        else if (part.prop == "fatman")
            data = sl::make_fatman_data(part.radius, part.length);
        else if (part.prop == "rv")
            data = make_rv_data(part.radius, part.length);
        else if (part.prop == "csm")
            data = sl::make_apollo_csm_data(part.radius, body);
        else if (part.prop == "aeroplane")
        {
            // The prop's nose is +Z with wheels at y = 0; turn it about the vertical to face the ship's -Z.
            data = sl::make_aeroplane_data(part.span, body, accent);
            glm::mat4 turn = glm::rotate(glm::mat4(1.0f), 3.14159265f, glm::vec3(0.0f, 1.0f, 0.0f));
            turn[3] = glm::vec4(0.0f, 0.0f, part.z, 1.0f);
            sl::transform_mesh_data(data, turn);
            return true;
        }
        else if (part.prop == "lunar_rover")
        {
            // The prop faces +Z with its wheels on y = 0; turn it to face the ship's -Z and lower the origin to the centre of mass,
            // about 0.8 m up on a 3.5 m rover.
            data = sl::make_lunar_rover_data(part.length, body);
            glm::mat4 turn = glm::rotate(glm::mat4(1.0f), 3.14159265f, glm::vec3(0.0f, 1.0f, 0.0f));
            turn[3] = glm::vec4(0.0f, -0.8f * part.length / 3.5f, part.z, 1.0f);
            sl::transform_mesh_data(data, turn);
            return true;
        }
        else
        {
            std::fprintf(stderr, "[kosmos] unknown prop part '%s'\n", part.prop.c_str());
            return false;
        }
        glm::mat4 to_ship(1.0f);
        to_ship[0] = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        to_ship[1] = glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);
        to_ship[2] = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
        to_ship[3] = glm::vec4(0.0f, 0.0f, part.z, 1.0f);
        sl::transform_mesh_data(data, to_ship);
        return true;
    }

    // A props-library part moved into the ship frame: the prop's +Y becomes the nose direction (-Z) and its +Z umbilical faces +Y.
    sl::Mesh *make_prop_mesh(const kosmos::PartSpec &part)
    {
        sl::MeshData data;
        return make_prop_data(part, data) ? sl::create_mesh(data.vertices, data.indices) : nullptr;
    }

    // A profile of revolution about Z as mesh data, in the part's colour; the same shape the lathe draws.
    sl::MeshData make_lathe_data(const kosmos::PartSpec &part, int segments)
    {
        sl::MeshData data;
        const auto &profile = part.profile;
        const int rows = static_cast<int>(profile.size());
        for (int i = 0; i < rows; ++i)
        {
            const int a = std::max(i - 1, 0), b = std::min(i + 1, rows - 1);
            const float dz = profile[static_cast<std::size_t>(b)].first - profile[static_cast<std::size_t>(a)].first;
            const float dr = profile[static_cast<std::size_t>(b)].second - profile[static_cast<std::size_t>(a)].second;
            const float len = std::max(std::sqrt(dz * dz + dr * dr), 1e-6f);
            for (int s = 0; s <= segments; ++s)
            {
                const float angle = 6.2831853f * static_cast<float>(s) / static_cast<float>(segments);
                const float cs = std::cos(angle), sn = std::sin(angle);
                sl::Vertex3D v;
                v.position[0] = cs * profile[static_cast<std::size_t>(i)].second;
                v.position[1] = sn * profile[static_cast<std::size_t>(i)].second;
                v.position[2] = profile[static_cast<std::size_t>(i)].first;
                v.normal[0] = cs * dz / len;
                v.normal[1] = sn * dz / len;
                v.normal[2] = -dr / len;
                for (int c = 0; c < 3; ++c)
                    v.colour[c] = part.colour[c];
                v.uv[0] = static_cast<float>(s) / static_cast<float>(segments);
                v.uv[1] = static_cast<float>(i) / static_cast<float>(rows - 1);
                data.vertices.push_back(v);
            }
        }
        for (int i = 0; i + 1 < rows; ++i)
            for (int s = 0; s < segments; ++s)
            {
                const std::uint32_t p = static_cast<std::uint32_t>(i * (segments + 1) + s), q = p + segments + 1;
                data.indices.insert(data.indices.end(), {p, p + 1, q, p + 1, q + 1, q});
            }
        return data;
    }

    // Drives a model's animations: simlib JSON animations read these named properties, and a glTF clip called "gear" is posed from
    // gear-up (start) to gear-down (end).
    void pose_model(sl::Model *model, double gear, double pod_left, double pod_right, const kosmos::Ship *ship)
    {
        if (ship)
            for (std::size_t i = 0; i < ship->spec().animation_controls.size(); ++i)
                sl::set_model_property(model, ship->spec().animation_controls[i].property, static_cast<float>(ship->animation_position(i)));
        sl::set_model_property(model, "gear-position", static_cast<float>(gear));
        sl::set_model_property(model, "pod-left", static_cast<float>(pod_left));
        sl::set_model_property(model, "pod-right", static_cast<float>(pod_right));
        const int clip = sl::find_model_clip(model, "gear");
        if (clip >= 0)
            sl::sample_model_clip(model, clip, static_cast<float>(gear) * sl::model_clip_duration(model, clip), false);
    }

    bool hud_allowed(sl::HudMode mode, const std::vector<std::string> &modes)
    {
        for (const std::string &name : modes)
        {
            sl::HudMode m = sl::HudMode::off;
            if (sl::hud_parse_mode(name, m) && m == mode)
                return true;
        }
        return false;
    }

    /** The mode after `current` in the ship's list, going off after the last. */
    sl::HudMode next_hud_mode(sl::HudMode current, const std::vector<std::string> &modes)
    {
        bool take_next = current == sl::HudMode::off;
        for (const std::string &name : modes)
        {
            sl::HudMode m = sl::HudMode::off;
            if (!sl::hud_parse_mode(name, m) || m == sl::HudMode::off)
                continue;
            if (take_next)
                return m;
            take_next = m == current;
        }
        return sl::HudMode::off;
    }

    // Parachute canopies that are out: narrow while reefed, filling to full width as they open.
    static void draw_canopies(Visuals &v, const kosmos::ShipSpec &spec, const glm::mat4 &model, const kosmos::Ship &ship)
    {
        const auto &runtime = ship.chutes();
        for (std::size_t i = 0; i < spec.parachutes.size() && i < runtime.size(); ++i)
        {
            const kosmos::ParachuteSpec &chute = spec.parachutes[i];
            const kosmos::ChuteRuntime &state = runtime[i];
            if (chute.model.empty() || state.state == kosmos::ChuteState::stowed || state.state == kosmos::ChuteState::gone)
                continue;
            double width = 1.0, length = 1.0;
            if (state.state == kosmos::ChuteState::reefed)
            {
                width = 0.3;
                length = 0.6 + 0.4 * std::min(1.0, state.time / 1.5);
            }
            else if (state.state == kosmos::ChuteState::opening)
            {
                const double fill = std::clamp(state.fill, 0.0, 1.0);
                width = 0.3 + 0.7 * fill;
                length = 1.0;
            }
            if (sl::Model *canopy = v.canopy_model(chute, spec.directory))
            {
                const float k = static_cast<float>(std::max(chute.canopy_radius, 0.1) / chute.model_radius);
                glm::mat4 place = glm::translate(glm::mat4(1.0f), glm::vec3(chute.position));
                place = glm::scale(place, glm::vec3(k * static_cast<float>(width), k * static_cast<float>(width), k * static_cast<float>(length)));
                sl::draw_model(canopy, model * place);
            }
        }
    }

    namespace
    {
        std::string lower_case(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        }

        // The base name (lower case, no folder or extension) of each material's diffuse texture, read from the OBJ's .mtl.
        std::map<std::string, std::string> material_texture_names(const std::filesystem::path &obj)
        {
            std::map<std::string, std::string> names;
            std::filesystem::path mtl = obj;
            mtl.replace_extension(".mtl");
            std::ifstream in(mtl);
            std::string line, material;
            while (std::getline(in, line))
            {
                std::istringstream words(line);
                std::string key, value;
                words >> key;
                std::getline(words >> std::ws, value);
                while (!value.empty() && (value.back() == '\r' || value.back() == ' '))
                    value.pop_back();
                if (key == "newmtl")
                    material = value;
                else if (key == "map_Kd" && !material.empty())
                    names[material] = lower_case(std::filesystem::path(value).stem().string());
            }
            return names;
        }
    } // namespace

    sl::Model *Visuals::skinned_part_model(const kosmos::PartSpec &part, const kosmos::ShipSpec &spec, const std::string &skin)
    {
        const auto key = std::make_pair(&part, skin);
        const auto found = skinned_models.find(key);
        if (found != skinned_models.end())
            return found->second;
        const std::filesystem::path obj = std::filesystem::path(spec.directory) / part.model;
        const std::filesystem::path folder = std::filesystem::path(kosmos::ship_skins_directory(spec)) / skin;
        sl::Model *model = sl::load_model(obj.string());
        if (model)
        {
            for (sl::ModelPart &mp : model->parts)
            {
                mp.material.cull = sl::CullMode::none;
                if (mp.name.rfind("vessel", 0) == 0 || mp.name.rfind("vent", 0) == 0 || mp.name.rfind("crew", 0) == 0)
                    mp.visible = false;
            }
            if (!part.animations.empty())
                sl::load_model_animations_json(model, (std::filesystem::path(spec.directory) / part.animations).string());
            const std::map<std::string, std::string> names = material_texture_names(obj);
            std::map<std::string, sl::Bitmap *> loaded;
            std::error_code ec;
            for (const auto &file : std::filesystem::directory_iterator(folder, ec))
                if (lower_case(file.path().extension().string()) == ".dds" && lower_case(file.path().stem().string()).find("_norm") == std::string::npos)
                {
                    const std::string stem = lower_case(file.path().stem().string());
                    if (sl::Bitmap *bitmap = sl::load_dds(file.path().string()))
                    {
                        model->textures.push_back(bitmap);
                        loaded[stem] = bitmap;
                    }
                }
            for (sl::ModelPart &mp : model->parts)
            {
                const auto name = names.find(mp.material_name);
                const auto bitmap = name == names.end() ? loaded.end() : loaded.find(name->second);
                if (bitmap != loaded.end())
                    mp.material.texture = bitmap->second;
            }
        }
        else
            std::fprintf(stderr, "[kosmos] cannot load part model %s: %s\n", part.model.c_str(), sl::last_error().c_str());
        return skinned_models[key] = model;
    }

    void draw_parts(Visuals &v, const kosmos::ShipSpec &spec, int first_stage, int last_stage, const glm::mat4 &model, bool lit, double gear, double pod_left, double pod_right, const kosmos::Ship *ship)
    {
        if (ship && !spec.parachutes.empty())
            draw_canopies(v, spec, model, *ship);
        for (int s = first_stage; s <= last_stage; ++s)
            for (const kosmos::PartSpec &part : spec.stages[static_cast<std::size_t>(s)].parts)
            {
                if (!part.model.empty())
                {
                    sl::Model *part_model = nullptr;
                    if (ship && !ship->skin.empty())
                        part_model = v.skinned_part_model(part, spec, ship->skin);
                    if (!part_model)
                        part_model = v.part_model(part, spec.directory);
                    if (part_model)
                    {
                        glm::mat4 place = glm::translate(glm::mat4(1.0f), glm::vec3(part.offset));
                        if (part.rotate != sl::dvec3(0.0))
                        {
                            place = glm::rotate(place, glm::radians(static_cast<float>(part.rotate.z)), glm::vec3(0.0f, 0.0f, 1.0f));
                            place = glm::rotate(place, glm::radians(static_cast<float>(part.rotate.y)), glm::vec3(0.0f, 1.0f, 0.0f));
                            place = glm::rotate(place, glm::radians(static_cast<float>(part.rotate.x)), glm::vec3(1.0f, 0.0f, 0.0f));
                        }
                        if (part.scale != sl::dvec3(1.0))
                            place = glm::scale(place, glm::vec3(part.scale));
                        pose_model(part_model, gear, pod_left, pod_right, ship);
                        sl::draw_model(part_model, model * place);
                    }
                    continue;
                }
                sl::Mesh *mesh = v.part_mesh(part);
                if (!mesh)
                    continue;
                sl::Material m;
                m.tint = part.prop.empty() ? sl::Colour{static_cast<std::uint8_t>(part.colour[0] * 255.0f), static_cast<std::uint8_t>(part.colour[1] * 255.0f),
                                                        static_cast<std::uint8_t>(part.colour[2] * 255.0f), 255}
                                           : sl::Colour{255, 255, 255, 255};
                m.lit = lit;
                m.fog = false;
                m.pbr = true;
                m.metallic = 0.0f;
                m.roughness = 0.7f;
                sl::draw_mesh(mesh, model * glm::translate(glm::mat4(1.0f), glm::vec3(part.offset)), m);
            }
    }
} // namespace kosmos::app
