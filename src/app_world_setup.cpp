#include "app_state.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace kosmos::app
{
    namespace
    {
        // A cloud map baked with planetbake, if there is one: clouds.dds/.png (or NAME_clouds) in the body's tile pack folder, or
        // <body>_clouds in data/textures-hires. The caller falls back to the procedural clouds when this returns null.
        sl::Bitmap *baked_clouds(const std::string &root, const Body &body, int max_size)
        {
            namespace fs = std::filesystem;
            std::string path;
            if (!body.tiles.empty())
            {
                const std::string folder = root + "/data/textures-hires/" + body.tiles + "/";
                path = find_texture_in_directory("", folder, "clouds", {".dds", ".png"});
                std::error_code ec;
                if (path.empty() && fs::is_directory(folder, ec))
                    for (const fs::directory_entry &entry : fs::directory_iterator(folder, ec))
                    {
                        const std::string name = entry.path().filename().string();
                        if (name.size() > 11 && (name.compare(name.size() - 11, 11, "_clouds.dds") == 0 || name.compare(name.size() - 11, 11, "_clouds.png") == 0))
                            path = entry.path().string();
                    }
            }
            if (path.empty())
            {
                std::string lower = body.name;
                std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                path = find_texture_in_directory(root, "/data/textures-hires/", lower + "_clouds", {".dds", ".png"});
            }
            if (path.empty())
                return nullptr;
            sl::Bitmap *bitmap = load_surface_texture(path, max_size);
            if (bitmap)
                std::fprintf(stderr, "kosmos: clouds for %s from %s\n", body.name.c_str(), path.c_str());
            return bitmap;
        }
    }

    // Everything drawn or collided with that belongs to one star system; a quantum jump drops it and builds the next.
    void App::leave_system()
    {
        body_terrains.clear();
        terrain_set.items.clear();
        terrain = nullptr;
        if (cloud_shell)
            sl::destroy_mesh(cloud_shell);
        cloud_shell = nullptr;
        if (home_cloud_texture)
            sl::destroy_bitmap(home_cloud_texture);
        home_cloud_texture = nullptr;
        home_cloud_height = cloud_height;
        for (sl::Mesh *m : vis.body_cloud_meshes)
            if (m)
                sl::destroy_mesh(m);
        for (sl::Bitmap *b : vis.body_cloud_textures)
            if (b)
                sl::destroy_bitmap(b);
        vis.body_cloud_meshes.clear();
        vis.body_cloud_textures.clear();
        std::sort(vis.body_textures.begin(), vis.body_textures.end());
        vis.body_textures.erase(std::unique(vis.body_textures.begin(), vis.body_textures.end()), vis.body_textures.end());
        for (sl::Bitmap *b : vis.body_textures)
            sl::destroy_bitmap(b);
        for (sl::Bitmap *b : vis.body_normals)
            if (b)
                sl::destroy_bitmap(b);
        for (sl::Mesh *m : vis.rings)
            if (m)
                sl::destroy_mesh(m);
        vis.body_textures.clear();
        vis.body_normals.clear();
        for (sl::Model *m : vis.body_meshes)
            if (m)
                sl::destroy_model(m);
        vis.body_meshes.clear();
        vis.earth_tiles.reset();
        vis.rings.clear();
        vis.surface_textured.clear();
    }

    // Dates and bodies that depend on where the planets stand: re-run when the start date changes without a rebuild of the system.
    void App::place_worlds()
    {
        earth_body = &sim.universe.bodies[static_cast<std::size_t>(earth_id)];
        sim.universe.bodies[static_cast<std::size_t>(earth_id)].rotation_offset += options.earth_turn;
        // A tidally locked body with terrain does not spin, so its far side is fixed: face Earth at time zero.
        for (std::size_t i = 0; i < sim.universe.bodies.size(); ++i)
        {
            kosmos::Body &b = sim.universe.bodies[i];
            if (!b.terrain.empty() && static_cast<int>(i) != earth_id && b.parent >= 0 && (!b.has_meridian || sim.universe.epoch_override <= 0.0))
            {
                const sl::dvec3 to_parent = sim.universe.position(b.parent) - sim.universe.position(static_cast<int>(i));
                // Longitude 0 faces the parent: the Moon toward Earth, an undated Mars toward the Sun.
                const sl::dvec3 pole = sim.universe.pole(static_cast<int>(i));
                const sl::dvec3 lon0 = b.axis_tilt * sl::dvec3(1.0, 0.0, 0.0);
                b.rotation_offset = std::atan2(glm::dot(pole, glm::cross(lon0, to_parent)), glm::dot(lon0, to_parent));
            }
        }
        loading.note("Placing bases and beacons");
        sim.nav.truncate_beacons(base_beacon_mark);
        for (kosmos::Base &base : sim.bases)
        {
            const int body = sim.universe.find(base.body);
            if (body >= 0)
            {
                base.direction = kosmos::base_direction(sim.universe, body, base);
                base.pole = sim.universe.pole(body);
                base.height = base.elevation;
                kosmos::register_base_beacons(sim.nav, sim.universe, body, base);
            }
        }
    }

    // A scenario on another date in the current system keeps the terrain, textures and cloud layers; only the planets' positions
    // and the turn of each terrain body (baked into the terrain when it was built) are brought to the new date.
    bool App::change_date(double epoch_jd, std::string &message)
    {
        std::vector<double> built(body_terrains.size(), 0.0);
        for (std::size_t k = 0; k < body_terrains.size(); ++k)
        {
            const sl::FlightBody *fb = sim.universe.world.body(body_terrains[k]->body);
            built[k] = sim.universe.rotation_angle(body_terrains[k]->body) - (fb->spin_phase + fb->spin_rate * sim.universe.world.time());
        }
        // The terrain was levelled under each base at the height sampled when it was built; place_worlds would reset it to the configured elevation.
        std::vector<double> base_heights;
        for (const kosmos::Base &base : sim.bases)
            base_heights.push_back(base.height);
        sim.start_epoch_jd = epoch_jd;
        if (!sim.change_system(sim.system_index(), message))
            return false;
        place_worlds();
        for (std::size_t k = 0; k < sim.bases.size() && k < base_heights.size(); ++k)
            sim.bases[k].height = base_heights[k];
        for (std::size_t k = 0; k < body_terrains.size(); ++k)
        {
            sl::FlightBody *fb = sim.universe.world.body(body_terrains[k]->body);
            fb->spin_phase = sim.universe.bodies[static_cast<std::size_t>(body_terrains[k]->body)].rotation_offset - built[k];
            fb->terrain = body_terrains[k]->terrain.get();
        }
        return true;
    }

    // Remakes the far-view image of every invented world at the width now chosen in the settings, without leaving the system.
    void App::reload_world_textures()
    {
        for (std::size_t i = 0; i < sim.universe.bodies.size() && i < vis.body_textures.size(); ++i)
        {
            const kosmos::Body &b = sim.universe.bodies[i];
            if (b.minor || !b.style.valid() || !b.texture.empty())
                continue;
            const std::string image = generated_surface_image(b);
            if (image.empty())
                continue;
            if (sl::Bitmap *image_bitmap = load_surface_texture(image, options.max_texture))
            {
                sl::destroy_bitmap(vis.body_textures[i]);
                vis.body_textures[i] = image_bitmap;
            }
        }
    }

    void App::load_planet_images()
    {
        loading.note("Loading planet images");
        sl::Bitmap *shared_rock = nullptr;
        for (std::size_t i = 0; i < sim.universe.bodies.size(); ++i)
        {
            const kosmos::Body &b = sim.universe.bodies[i];
            const sl::Colour average{static_cast<std::uint8_t>(b.colour[0] * 255.0f), static_cast<std::uint8_t>(b.colour[1] * 255.0f),
                                     static_cast<std::uint8_t>(b.colour[2] * 255.0f), 255};
            // The anonymous belt rocks share one texture instead of generating ninety.
            if (b.minor && shared_rock)
                vis.body_textures.push_back(nullptr);
            else
            {
                vis.body_textures.push_back(b.fallback_texture == "moon" ? fx::make_moon_texture() : fx::make_detail_texture(average, 31 + static_cast<int>(i) * 7, 0.45f, 0.15f, 256));
                if (b.minor)
                    shared_rock = vis.body_textures.back();
            }
            sl::Model *shape = nullptr;
            if (!b.mesh.empty())
            {
                shape = sl::load_model(sim.root() + "/data/" + b.mesh);
                if (!shape || shape->parts.empty() || !shape->parts[0].mesh)
                {
                    std::fprintf(stderr, "[kosmos] cannot load body mesh %s: %s\n", b.mesh.c_str(), sl::last_error().c_str());
                    if (shape)
                        sl::destroy_model(shape);
                    shape = nullptr;
                }
            }
            vis.body_meshes.push_back(shape);
            if (!b.minor)
            {
                loading.note("Loading the surface of " + b.name);
                const std::string image = find_surface_image(sim.root(), b);
                if (!image.empty())
                    if (sl::Bitmap *image_bitmap = load_surface_texture(image, options.max_texture))
                    {
                        sl::destroy_bitmap(vis.body_textures.back());
                        vis.body_textures.back() = image_bitmap;
                        vis.surface_textured.push_back(static_cast<int>(i));
                    }
            }
            {
                sl::Bitmap *normal = nullptr;
                std::string lower = b.name;
                for (char &c : lower)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                std::string celestia_normal = find_texture_in_directory(sim.root(), "/data/textures-hires/", lower + "_normal",
                                                                         {".dds", ".png", ".jpg", ".jpeg"});
                if (celestia_normal.empty())
                    celestia_normal = find_image_in_folder(sim.root() + "/import/celestia/hires/" + lower + "_normal");
                if (celestia_normal.empty())
                    celestia_normal = find_texture_in_directory(sim.root(), "/data/textures/", lower + "_normal",
                                                                 {".dds", ".png", ".jpg", ".jpeg"});
                if (!b.minor && !options.no_relief && !celestia_normal.empty())
                    normal = load_surface_texture(celestia_normal, options.max_texture);
                if (normal)
                {
                    normal->smooth = true;
                    normal->mipmaps = true;
                }
                vis.body_normals.push_back(normal);
            }
            vis.rings.push_back(b.ring_outer > 0.0 ? make_ring_mesh(b.ring_inner / b.ring_outer, b.ring_colour) : nullptr);
        }
        for (std::size_t i = 0; i < sim.universe.bodies.size(); ++i)
            if (!vis.body_textures[i])
                vis.body_textures[i] = shared_rock;

    }

    void App::load_earth_tiles()
    {
        // Tiled Earth imagery (optional, not in the repository: see the Developers Manual) replaces the single image on the sphere.
        if (!options.no_tiles)
        {
            const int home_body = sim.universe.home_id();
            const std::string pack = home_body >= 0 ? sim.universe.bodies[static_cast<std::size_t>(home_body)].tiles : std::string();
            if (home_body >= 0 && (sim.universe.bodies[static_cast<std::size_t>(home_body)].imagery == "celestia" || !pack.empty()))
            {
                loading.note("Opening the Earth imagery tiles");
                const std::string hires = sim.root() + "/import/celestia/hires/";
                sl::PlanetTilesOptions tiles;
                // Skirts show as bright lines along tile edges from orbit.
                tiles.skirt = 0.0f;
                // Full-strength, tight glint blows out into a white disc from orbit.
                tiles.specular_strength = 0.45f;
                tiles.specular_min_roughness = 0.3f;
                if (!pack.empty())
                {
                    const std::string base = sim.root() + "/data/textures-hires/" + pack + "/";
                    tiles.colour = find_tile_source(base + "colour");
                    tiles.specular = find_tile_source(base + "specular");
                    if (!options.no_relief)
                        tiles.normal = find_tile_source(base + "normal");
                }
                else
                {
                    tiles.colour = find_tile_source(hires + "earth");
                    tiles.specular = find_tile_source(hires + "earth_specular");
                    if (!options.no_relief)
                        tiles.normal = find_tile_source(hires + "earth_normal/textures/medres/earth-normals-hs-32k");
                }
                if (!tiles.colour.empty())
                {
                    vis.earth_tiles = sl::PlanetTiles::create(tiles);
                    if (!vis.earth_tiles)
                        std::fprintf(stderr, "kosmos: tiled Earth unavailable: %s\n", sl::last_error().c_str());
                }
                if (vis.earth_tiles)
                {
                    const std::size_t index = static_cast<std::size_t>(home_body);
                    if (index < vis.body_textures.size())
                    {
                        sl::destroy_bitmap(vis.body_textures[index]);
                        vis.body_textures[index] = nullptr;
                    }
                    if (index < vis.body_normals.size())
                    {
                        sl::destroy_bitmap(vis.body_normals[index]);
                        vis.body_normals[index] = nullptr;
                    }
                }
            }
        }
    }

    bool App::build_terrains()
    {
        try
        {
            body_terrains.push_back(make_body_terrain(sim, earth_id, true));
            for (std::size_t i = 0; i < sim.universe.bodies.size(); ++i)
                if (static_cast<int>(i) != earth_id && (!sim.universe.bodies[i].terrain.empty() || sim.universe.bodies[i].style.has_ground()))
                    if (std::unique_ptr<BodyTerrain> extra = make_body_terrain(sim, static_cast<int>(i), false))
                        body_terrains.push_back(std::move(extra));
        }
        catch (const std::exception &e)
        {
            std::fprintf(stderr, "Terrain unavailable: %s\n", e.what());
            return false;
        }
        return true;
    }

    void App::build_cloud_layers()
    {
        // Weather for the other worlds that have it: a generated cloud layer on its own shell.
        vis.body_cloud_meshes.assign(sim.universe.bodies.size(), nullptr);
        vis.body_cloud_textures.assign(sim.universe.bodies.size(), nullptr);
        for (std::size_t i = 0; i < sim.universe.bodies.size(); ++i)
        {
            const kosmos::Body &b = sim.universe.bodies[i];
            if (b.cloud_cover <= 0.0 || static_cast<int>(i) == earth_id)
                continue;
            vis.body_cloud_textures[i] = baked_clouds(sim.root(), b, options.max_texture);
            if (!vis.body_cloud_textures[i])
                vis.body_cloud_textures[i] = fx::make_cloud_texture(2048, 1024, b.cloud_cover, 101 + static_cast<std::uint32_t>(i), 7.0);
            vis.body_cloud_meshes[i] = sl::create_sphere_mesh(static_cast<float>(b.radius + b.cloud_height), 128, 64, {255, 255, 255, 255});
        }
        loading.note("Building the cloud layers");
        if (!earth_body->real_world && earth_body->cloud_cover > 0.0)
        {
            home_cloud_texture = baked_clouds(sim.root(), *earth_body, options.max_texture);
            if (!home_cloud_texture)
                home_cloud_texture = fx::make_cloud_texture(2048, 1024, earth_body->cloud_cover, 101 + static_cast<std::uint32_t>(earth_id), 7.0);
            home_cloud_height = earth_body->cloud_height;
        }
    }

    bool App::enter_system()
    {
        load_planet_images();
        load_earth_tiles();

        // The home planet is a streaming procedural terrain whose heights the launch gear also collides with.
        earth_id = sim.universe.home_id();
        if (earth_id < 0)
        {
            std::fprintf(stderr, "kosmos: system has no home planet with terrain\n");
            return false;
        }
        place_worlds();
        body_terrains.clear();
        terrain_set.items.clear();
        if (!build_terrains())
            return false;
        terrain = body_terrains[0]->terrain.get();
        // Each terrain is built in the body's frame as it stands now; from here it turns with the planet.
        for (const std::unique_ptr<BodyTerrain> &bt : body_terrains)
            if (sl::FlightBody *fb = sim.universe.world.body(bt->body))
                fb->spin_phase = -fb->spin_rate * sim.universe.world.time();
        for (const std::unique_ptr<BodyTerrain> &bt : body_terrains)
        {
            sim.universe.world.body(bt->body)->terrain = bt->terrain.get();
            terrain_set.items.emplace_back(bt->body, bt->terrain.get());
        }
        build_cloud_layers();
        cloud_shell = sl::create_sphere_mesh(static_cast<float>(earth_body->radius + home_cloud_height), 256, 128, {255, 255, 255, 255});
        return true;
    }

    // The star system: terrain, bodies, bases, and the functions that enter and leave a system.
    int App::init_systems()
    {
        settings_defaults(sim.props);
        settings_load(sim.props);
        if (options.world_texture >= 0)
            sim.props.set_int("/sim/settings/world-texture", options.world_texture);
        generated_texture_width = sim.props.get_int("/sim/settings/world-texture");
        if (!enter_system())
        {
            sl::shutdown();
            return 1;
        }
    return keep_going;
    }

    // The ship to fly and the HUD.
    int App::init_ship()
    {
        loading.note("Assembling the ship");
        ship = start_scenario(sim, options, terrain_set);
        if (!ship)
        {
            if (!options.scenario.empty() && sim.find_scenario(options.scenario))
                std::fprintf(stderr, "kosmos: unable to start scenario \"%s\"\n", options.scenario.c_str());
            else
                std::fprintf(stderr, "No Kestrel addon found under %s\n", options.root.c_str());
            sl::shutdown();
            return 1;
        }

        if (!options.hud.empty())
        {
            sl::HudMode start_mode = sl::HudMode::off;
            if (sl::hud_parse_mode(options.hud, start_mode))
                hud.set_mode(start_mode);
            else
                std::fprintf(stderr, "unknown --hud mode '%s' (off, orbit, surface, docking)\n", options.hud.c_str());
        }
        sound_on = true;
        first_frame = true;
        show_orbit = true;
        show_info = options.show_info && !options.hide_info;
        running = true;
        shot_saved = false;
        map_mode = options.map;
        galaxy_mode = options.galaxy_view;
        jump_to = -1;
        jump_to_star_orbit = false;
              // a body of the target system to arrive at; empty picks the best world
              // the planet or moon chosen in the quantum window for quantum_selected
        quantum_bodies_for = -1;
         // name, description
        quantum_window = options.quantum_window;
        quantum_selected = -1;
        gview.refresh(sim.galaxy, sim.system_index());
        cockpit_view = options.cockpit;
        mouse_captured = false;
        pointer_free = false; // ` toggles between mouselook and a free pointer for the menus when on foot
        if (!options.target.empty())
            sim.props.set_string("/sim/target", options.target);
        apply_sets(sim.props, options.sets);
        mfd_display = options.mfd_display;
    return keep_going;
    }
} // namespace kosmos::app
