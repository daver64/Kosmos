#include "app_state.h"
#include "orbiter_tile_terrain.h"

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
#ifdef KOSMOS_EXPERIMENTAL_ORBITAL_RELIEF
        vis.earth_relief_tiles.reset();
#endif
        vis.earth_cloud_tiles.reset();
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
            if (b.gravity_harmonics && !b.gravity_frame_defined)
                sim.universe.world.body(static_cast<int>(i))->gravity_orientation =
                    b.axis_tilt * glm::angleAxis(b.rotation_offset, sl::dvec3(0.0, 1.0, 0.0));
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

    // Swaps in the best cached far-view image of every invented world at the width now chosen in the settings; the larger widths
    // that are missing are made in the background and swapped in as they finish.
    void App::reload_world_textures()
    {
        for (std::size_t i = 0; i < sim.universe.bodies.size() && i < vis.body_textures.size(); ++i)
        {
            const kosmos::Body &b = sim.universe.bodies[i];
            if (b.minor || !b.style.valid() || !b.texture.empty())
                continue;
            if (std::find(vis.surface_textured.begin(), vis.surface_textured.end(), static_cast<int>(i)) == vis.surface_textured.end())
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
        world_jobs.cancel();
        world_jobs_pending = true;
    }

    // Queues the missing larger images, nearest world first, and swaps in the ones the background jobs have finished.
    void App::update_world_textures()
    {
        if (world_jobs_pending && ship)
        {
            world_jobs_pending = false;
            const sl::dvec3 eye = ship->origin();
            std::vector<std::pair<double, std::size_t>> order;
            for (std::size_t i = 0; i < sim.universe.bodies.size(); ++i)
            {
                const kosmos::Body &b = sim.universe.bodies[i];
                if (b.minor || !b.style.valid() || !b.texture.empty() || generated_missing_widths(b).empty())
                    continue;
                order.push_back({glm::length(sim.universe.position(static_cast<int>(i)) - eye), i});
            }
            std::sort(order.begin(), order.end());
            // Every world's 2k first, nearest first, then the full width.
            for (const int pass : {0, 1})
                for (const auto &entry : order)
                {
                    const kosmos::Body &b = sim.universe.bodies[entry.second];
                    const std::vector<int> missing = generated_missing_widths(b);
                    if (pass < static_cast<int>(missing.size()))
                        world_jobs.add({sim.system_index(), b.name, missing[static_cast<std::size_t>(pass)],
                                        kosmos::far_image_path(b.style, missing[static_cast<std::size_t>(pass)]).string()});
                }
        }
        for (const kosmos::WorldTextureJobs::Job &job : world_jobs.poll())
        {
            if (job.system != sim.system_index())
                continue;
            const int id = sim.universe.find(job.body);
            std::error_code ec;
            if (id < 0 || !std::filesystem::exists(job.path, ec))
                continue;
            const std::size_t i = static_cast<std::size_t>(id);
            const kosmos::Body &b = sim.universe.bodies[i];
            // Only ever swap upwards, and never past the width chosen in the settings.
            const std::string best = generated_surface_image(b);
            if (best != job.path || i >= vis.body_textures.size())
                continue;
            if (sl::Bitmap *image_bitmap = load_surface_texture(job.path, options.max_texture))
            {
                sl::destroy_bitmap(vis.body_textures[i]);
                vis.body_textures[i] = image_bitmap;
            }
        }
    }

    void App::load_planet_images()
    {
        world_jobs.cancel();
        world_jobs_pending = true;
        loading.note("Loading planet images");
        sl::Bitmap *shared_rock = nullptr;
        for (std::size_t i = 0; i < sim.universe.bodies.size(); ++i)
        {
            if (loading.started)
                loading.pump();
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
            // A mesh under data/textures-hires/ is optional; without it the body stays a sphere.
            if (!b.mesh.empty() && std::filesystem::exists(sim.root() + "/data/" + b.mesh))
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
            if (home_body >= 0)
            {
                if (sim.universe.bodies[static_cast<std::size_t>(home_body)].imagery == "celestia" || !pack.empty())
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
                        if (tiles.colour.empty() && sim.universe.bodies[static_cast<std::size_t>(home_body)].name == "Earth")
                        {
                            const std::string defaults = sim.root() + "/data/orbiter-defaults/earth/";
                            tiles.colour = find_tile_source(defaults + "colour");
                            tiles.specular = find_tile_source(defaults + "specular");
                            tiles.normal.clear();
                        }
                    }
                    if (!tiles.colour.empty())
                    {
                        vis.earth_tiles = sl::PlanetTiles::create(tiles);
                        if (!vis.earth_tiles)
                            std::fprintf(stderr, "kosmos: tiled Earth unavailable: %s\n", sl::last_error().c_str());
                        else
                            std::fprintf(stderr, "kosmos: Earth tiles: colour=%s, specular=%s\n",
                                         tiles.colour.c_str(), tiles.specular.empty() ? "(none)" : tiles.specular.c_str());
                    }
                }
#ifdef KOSMOS_EXPERIMENTAL_ORBITAL_RELIEF
                const kosmos::Body &body = sim.universe.bodies[static_cast<std::size_t>(home_body)];
                if (!body_terrains.empty() && body_terrains[0] &&
                    body_terrains[0]->body == home_body && body_terrains[0]->orbiter_imagery &&
                    body_terrains[0]->has_orbiter_elevation)
                {
                    const auto image = body_terrains[0]->orbiter_imagery;
                    const auto heights = body_terrains[0]->terrain->source();
                    const glm::dquat terrain_frame = body_terrains[0]->frame.orientation;
                    if (heights)
                    {
                        sl::PlanetTilesOptions relief;
                        relief.has_colour = [image](int level, int column, int row)
                        {
                            return image->has_tile(level + 4, row, column);
                        };
                        relief.has_colour_children = [image](int level, int column, int row)
                        {
                            return image->has_children(level + 4, row, column);
                        };
                        relief.load_colour = [image](int level, int column, int row) -> sl::Bitmap *
                        {
                            constexpr std::size_t decoded_limit = 512u * 512u * 4u;
                            kosmos::OrbiterTree::Tile ancestor;
                            std::vector<std::uint8_t> bytes;
                            if (!image->read_tile(level + 4, row, column, ancestor, bytes, 2u << 20, decoded_limit) ||
                                ancestor.level != level + 4)
                            {
                                sl::detail::set_error("Orbiter imagery tile is unreadable");
                                return nullptr;
                            }
                            sl::DdsOptions decoding;
                            decoding.allow_compressed_upload = false;
                            decoding.max_decoded_bytes = decoded_limit;
                            return sl::load_dds_from_memory(bytes.data(), bytes.size(), decoding);
                        };
                        const double radius = body.radius;
                        relief.elevation = [heights, terrain_frame, radius](const glm::dvec3 &direction)
                        {
                            return kosmos::orbiter_tile_height(*heights, terrain_frame, direction, radius, 1000.0);
                        };
                        relief.elevation_lod = [heights, terrain_frame, radius](const glm::dvec3 &direction, double cell_size)
                        {
                            return kosmos::orbiter_tile_height(*heights, terrain_frame, direction, radius, cell_size * radius);
                        };
                        relief.max_elevation = std::max({std::fabs(heights->min_height()), std::fabs(heights->max_height()), 1.0}) / radius;
                        relief.normal_sample_spacing = 1000.0 / radius;
                        relief.match_elevation_edges = true;
                        relief.adaptive_elevation_diagonals = true;
                        relief.adaptive_elevation_normals = true;
                        relief.colour_max_level = 18;
                        relief.tile_size = 512;
                        relief.segments = 24;
                        relief.geometry_pixels = 32.0f;
                        relief.max_relief_patches = options.orbital_relief_patches;
                        relief.max_relief_meshes = options.orbital_relief_meshes;
                        relief.max_relief_mesh_jobs = 4;
                        relief.max_cached_tiles = options.orbital_relief_textures == 0 ? 512 :
                                                  std::max(8, options.orbital_relief_textures / 4 * 3);
                        relief.max_resident_tiles = options.orbital_relief_textures;
                        relief.max_failed_tiles = 256;
                        relief.max_loads_in_flight = 4;
                        relief.feather = 0.0f;
                        relief.fade_seconds = 0.0f;
                        relief.skirt = 0.02f;
                        vis.earth_relief_tiles = sl::PlanetTiles::create(relief);
                        if (!vis.earth_relief_tiles)
                            std::fprintf(stderr, "[kosmos] Orbiter relief tiles unavailable: %s\n", sl::last_error().c_str());
                        else
                            std::fprintf(stderr, "[kosmos] Orbiter relief budgets: patches %d, meshes %d, textures %d (0 = unlimited)\n",
                                         relief.max_relief_patches, relief.max_relief_meshes, relief.max_resident_tiles);
                    }
                    else
                        std::fprintf(stderr, "[kosmos] Orbiter relief tiles unavailable: terrain source unavailable\n");
                }
#endif
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

    // The cloud layer as tiles, when the pack has them: Orbiter's cloud tree reaches about 600 m a pixel, far past one image.
    void App::load_earth_cloud_tiles()
    {
        vis.earth_cloud_tiles.reset();
        if (options.no_tiles || options.no_clouds)
            return;
        const std::string source = find_tile_source(sim.root() + "/data/textures-hires/earth_clouds");
        if (source.empty())
            return;
        sl::PlanetTilesOptions clouds;
        clouds.colour = source;
        clouds.skirt = 0.0f;
        clouds.segments = 16;
        clouds.max_cached_tiles = 384;
        clouds.fade_seconds = 0.6f;
        // A thin shell seen edge-on at the limb would smear into streaks above the horizon, so it fades as the view grazes it.
        clouds.limb_fade = 0.3f;
        vis.earth_cloud_tiles = sl::PlanetTiles::create(clouds);
        if (!vis.earth_cloud_tiles)
            std::fprintf(stderr, "kosmos: tiled clouds unavailable: %s\n", sl::last_error().c_str());
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
#ifndef KOSMOS_EXPERIMENTAL_ORBITAL_RELIEF
        load_earth_tiles();
#endif
        load_earth_cloud_tiles();

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
#ifdef KOSMOS_EXPERIMENTAL_ORBITAL_RELIEF
        load_earth_tiles();
#endif
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
        const float shell_radius = static_cast<float>(earth_body->radius + home_cloud_height);
        constexpr int shell_segments = 256, shell_rings = 128;
        cloud_shell = sl::create_sphere_mesh(shell_radius, shell_segments, shell_rings, {255, 255, 255, 255});
        // The same vertices again on the CPU, so the shell can be faded toward its limb each frame.
        cloud_shell_vertices.clear();
        for (int ring = 0; ring <= shell_rings; ++ring)
            for (int segment = 0; segment <= shell_segments; ++segment)
            {
                const float phi = 3.14159265f * static_cast<float>(ring) / shell_rings, theta = 6.2831853f * static_cast<float>(segment) / shell_segments;
                const glm::vec3 normal{std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta)};
                sl::Vertex3D v;
                for (int k = 0; k < 3; ++k)
                {
                    v.position[k] = normal[k] * shell_radius;
                    v.normal[k] = normal[k];
                }
                v.uv[0] = static_cast<float>(segment) / shell_segments;
                v.uv[1] = static_cast<float>(ring) / shell_rings;
                cloud_shell_vertices.push_back(v);
            }
        return true;
    }

    // The star system: terrain, bodies, bases, and the functions that enter and leave a system.
    int App::init_systems()
    {
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
        if (!options.scenario.empty())
            if (const kosmos::Scenario *sc = sim.find_scenario(options.scenario))
                active_scenario_path = sc->path;

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
        quantum.jump_to = -1;
        quantum.quantum_window = options.quantum_window;
        quantum.quantum_selected = -1;
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
