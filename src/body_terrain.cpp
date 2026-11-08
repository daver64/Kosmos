#include "body_terrain.h"
#include "loading_screen.h"
#include "surface_textures.h"
#include "terrain_mesh.h"
#include "terrain_visibility.h"

#include <algorithm>
#include <cctype>

namespace kosmos::app
{
    namespace
    {
        // Detail maps are shared by every body that names the same file, so each is loaded once.
        std::map<std::string, sl::Bitmap *> detail_maps;
    }

    std::vector<sl::TerrainLayer> planet_layers()
    {
        std::vector<sl::TerrainLayer> layers(4);
        layers[0].colour = {18, 62, 135, 255};
        layers[0].max_height = 1.0;
        layers[0].height_blend = 2.0;
        layers[0].specular = 0.8;
        layers[1].colour = {58, 112, 52, 255};
        layers[1].min_height = 1.0;
        layers[1].max_height = 2600.0;
        layers[1].height_blend = 600.0;
        layers[2].colour = {125, 112, 98, 255};
        layers[2].min_height = 2200.0;
        layers[2].max_height = 4600.0;
        layers[2].height_blend = 500.0;
        layers[3].colour = {245, 246, 250, 255};
        layers[3].min_height = 4300.0;
        layers[3].height_blend = 400.0;
        return layers;
    }

    bool detail_disabled = false;
    ClutterPerf clutter_perf;

    // Meshes placed on the ground become its surface; the bases are then levelled into it.
    static std::shared_ptr<kosmos::MeshSource> place_terrain_meshes(Sim &sim, const kosmos::Body &b, int id, sl::TerrainConfig &config, std::shared_ptr<sl::TerrainSource> &source)
    {
        std::shared_ptr<kosmos::MeshSource> meshes;
        const auto find = [&](const kosmos::Base &base, const std::string &name) {
            std::string found = kosmos::resolve_base_model(base, name);
            std::error_code ec;
            for (const char *folder : {"/", "/data/"})
                if (found.empty() && std::filesystem::exists(sim.root() + folder + name, ec))
                    found = sim.root() + folder + name;
            return found;
        };
        std::vector<kosmos::MeshSource::Placed> placed;
        for (const kosmos::Base &base : sim.bases)
        {
            if (sim.universe.find(base.body) != id)
                continue;
            for (const kosmos::TerrainMeshSpec &spec : base.terrain_meshes)
            {
                const std::string path = find(base, spec.file), image = spec.image.empty() ? std::string() : find(base, spec.image);
                auto grid = std::make_shared<kosmos::MeshGrid>();
                std::string error;
                if (path.empty())
                    error = "file not found";
                else if (!spec.image.empty() && image.empty())
                    error = "image not found";
                else if (kosmos::load_mesh_grid(spec, path, image, *grid, error))
                {
                    std::fprintf(stderr, "[kosmos] %s terrain mesh %s: %d x %d at %.2f m%s\n", base.id.c_str(), spec.file.c_str(), grid->width, grid->height, grid->cell, grid->colours.empty() ? "" : ", coloured");
                    placed.push_back(kosmos::MeshSource::place(base, b.radius, spec, grid, base.height + spec.y));
                    continue;
                }
                std::fprintf(stderr, "[kosmos] %s terrain mesh %s: %s\n", base.id.c_str(), spec.file.c_str(), error.c_str());
            }
        }
        if (!placed.empty())
        {
            meshes = std::make_shared<kosmos::MeshSource>(source, b.radius, std::move(placed));
            source = meshes;
            const auto inner = config.colour_function;
            config.colour_function = [inner, meshes](const sl::TerrainSample &sample, float rgb[3])
            {
                float c[3] = {0.5f, 0.5f, 0.5f};
                const bool base_colour = inner && inner(sample, c);
                if (meshes->colour(sample.position, c))
                {
                    std::copy(c, c + 3, rgb);
                    return true;
                }
                if (base_colour)
                    std::copy(c, c + 3, rgb);
                return base_colour;
            };
        }
        return meshes;
    }

    static void level_ground_under_bases(Sim &sim, const kosmos::Body &b, int id, std::shared_ptr<sl::TerrainSource> &source)
    {
        // Level the ground under each base at the height of its centre (on a mesh, the mesh's height there).
        std::vector<kosmos::FlattenSource::Site> sites;
        for (kosmos::Base &base : sim.bases)
        {
            if (sim.universe.find(base.body) != id)
                continue;
            base.height = source->height_on_sphere(base.direction, b.radius);
            for (const kosmos::FlattenSite &site : kosmos::flatten_sites(base, b.radius))
                sites.push_back(site);
        }
        if (!sites.empty())
            source = std::make_shared<kosmos::FlattenSource>(source, b.radius, std::move(sites));
    }

    static std::string lower_case(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    // Land cover is optional per body: it lives in data/landclass/<system>/<body> (Sol's Earth ships one).
    static void open_landclass(Sim &sim, const kosmos::Body &b, BodyTerrain &result)
    {
        const std::string where = lower_case(sim.universe.name) + "/" + lower_case(b.name);
        const std::string land_dir = sim.root() + "/data/landclass/" + where;
        std::error_code land_ec;
        if (std::filesystem::exists(land_dir, land_ec))
        {
            auto land = std::make_shared<kosmos::Landclass>();
            std::string land_error;
            // The raw rasters are developer data under import/; the shipped cache stays in data/.
            if (land->open(sim.root() + "/import/landclass/" + where, land_dir + "/landclass.kavlc", land_error, [](double, const std::string &message) { loading.note(message); }))
            {
                std::fprintf(stderr, "[kosmos] %s land cover: %d x %d cells\n", b.name.c_str(), land->width(), land->height());
                result.landclass = std::move(land);
            }
            else
                std::fprintf(stderr, "[kosmos] %s land cover unavailable: %s\n", b.name.c_str(), land_error.c_str());
        }
    }

    static void apply_detail_texture(Sim &sim, const kosmos::Body &b, const sl::TerrainConfig &config, BodyTerrain &result)
    {
        // A seamless map modulates the imagery's brightness near the ground so flat ground is not a smooth smear.
        const std::string file = b.detail.empty() ? "detail_dirt.jpg" : b.detail;
        auto found = detail_maps.find(file);
        if (found == detail_maps.end())
            found = detail_maps.emplace(file, sl::load_bitmap((sim.root() + "/data/textures/" + file).c_str())).first;
        sl::Bitmap *detail = found->second;
        if (detail)
        {
            sl::TerrainTextureOptions opts;
            opts.colour_detail = true;
            opts.macro_texture = detail;
            opts.macro_tile_metres = b.detail_tile;
            opts.macro_strength = b.detail_strength;
            opts.fade_start = 400.0;
            opts.fade_end = 4000.0;
            opts.max_size = 2048;
            std::fprintf(stderr, "[kosmos] %s detail texture: %s\n", b.name.c_str(), result.terrain->set_layer_textures(config.layers, opts) ? "on" : "unavailable");
        }
    }

    static void imagery_colours(sl::TerrainConfig &config, const std::shared_ptr<kosmos::OrbiterImagery> &orbiter_imagery, const std::vector<std::shared_ptr<kosmos::CelestiaImagery>> &surface_imagery)
    {
        const auto fallback = config.colour_function;
        config.colour_function = [orbiter_imagery, surface_imagery, fallback](const sl::TerrainSample &sample, float rgb[3])
        {
            float orbiter_rgb[3], celestia_rgb[3];
            double orbiter_resolution = std::numeric_limits<double>::infinity();
            const bool has_orbiter = orbiter_imagery &&
                                     orbiter_imagery->colour(sample.position, sample.cell_size, orbiter_rgb, orbiter_resolution);
            bool has_celestia = false;
            double celestia_resolution = std::numeric_limits<double>::infinity();
            for (const auto &imagery : surface_imagery)
            {
                float candidate_rgb[3];
                double candidate_resolution = std::numeric_limits<double>::infinity();
                if (imagery->colour(sample.position, sample.cell_size, candidate_rgb, candidate_resolution) &&
                    candidate_resolution < celestia_resolution)
                {
                    std::copy(std::begin(candidate_rgb), std::end(candidate_rgb), celestia_rgb);
                    celestia_resolution = candidate_resolution;
                    has_celestia = true;
                }
            }
            if (has_celestia && (!has_orbiter || celestia_resolution < orbiter_resolution))
                std::copy(std::begin(celestia_rgb), std::end(celestia_rgb), rgb);
            else if (has_orbiter)
                std::copy(std::begin(orbiter_rgb), std::end(orbiter_rgb), rgb);
            else
                return fallback && fallback(sample, rgb);
            return true;
        };
    }

    static void make_generated_ground(sl::TerrainConfig &config, const kosmos::Body &b, const kosmos::SurfaceFrame &frame, bool home, std::shared_ptr<sl::TerrainSource> &source)
    {
        // An invented world: the same generator that painted its far-view image shapes and colours the ground.
        const auto planet_surface = std::make_shared<kosmos::PlanetSurface>(b.style, b.radius);
        const auto generated_direction = [frame, home](const sl::dvec3 &direction) {
            return home ? glm::inverse(frame.orientation) * direction : direction;
        };
        source = home ? planet_surface->make_source(frame.orientation) : planet_surface->make_source();
        // A baked tile set (see --bake-planet) is used wherever it is at least as fine as the terrain cell; finer than that the generator takes over.
        std::vector<std::shared_ptr<kosmos::CelestiaImagery>> baked;
        const std::filesystem::path tiles = kosmos::baked_tiles_dir(b.style, b.radius);
        std::error_code tiles_ec;
        if (std::filesystem::exists(tiles / "tiles.ctx", tiles_ec))
        {
            auto imagery = std::make_shared<kosmos::CelestiaImagery>(tiles.string(), frame, b.radius);
            if (imagery->ok())
            {
                baked.push_back(std::move(imagery));
                std::fprintf(stderr, "[kosmos] %s baked tiles: %s\n", b.name.c_str(), tiles.string().c_str());
            }
        }
        std::shared_ptr<kosmos::LazyTiles> lazy;
        if (const int lazy_width = kosmos::read_lazy_width(tiles))
        {
            lazy = std::make_shared<kosmos::LazyTiles>(planet_surface, tiles, b.radius, lazy_width);
            std::fprintf(stderr, "[kosmos] %s lazy tiles up to %d wide: %s\n", b.name.c_str(), kosmos::estimate_bake(lazy_width).width, tiles.string().c_str());
        }
        config.colour_function = [planet_surface, baked, lazy, generated_direction](const sl::TerrainSample &sample, float rgb[3])
        {
            const sl::dvec3 direction = generated_direction(sample.position);
            if (lazy && lazy->colour(direction, sample.cell_size, rgb))
                return true;
            double resolution = 0.0;
            if (!baked.empty() && baked.front()->colour(sample.position, sample.cell_size, rgb, resolution) && resolution <= std::max(sample.cell_size, 1.0))
                return true;
            planet_surface->colour(direction, sample.height, sample.slope_degrees, rgb);
            return true;
        };
        std::fprintf(stderr, "[kosmos] %s generated %s terrain\n", b.name.c_str(), kosmos::archetype_name(b.style.type));
    }

    std::unique_ptr<BodyTerrain> make_body_terrain(Sim &sim, int id, bool home, const std::string &orbiter_textures)
    {
        const kosmos::Body &b = sim.universe.bodies[static_cast<std::size_t>(id)];
        loading.note("Building the terrain of " + b.name);
        const kosmos::SurfaceFrame frame{sim.universe.orientation(id)};
        const auto orbiter_layer = [&](const char *layer)
        {
            std::string path = kosmos::OrbiterTree::find_layer(sim.root() + "/import/orbiter", b.name, layer);
            if (path.empty() && !orbiter_textures.empty())
                path = kosmos::OrbiterTree::find_layer(orbiter_textures, b.name, layer);
            if (path.empty())
                path = kosmos::OrbiterTree::find_layer(sim.root() + "/data/orbiter-defaults", b.name, layer);
            return path;
        };
        sl::TerrainConfig config;
        config.shape = sl::TerrainShape::sphere;
        config.radius = b.radius;
        config.finest_cell_size = 4.0;
        config.max_screen_error = std::array<double, 4>{0.5, 1.0, 2.0, 4.0}[static_cast<std::size_t>(std::clamp(sim.props.get_int("/sim/settings/gfx-terrain-detail"), 0, 3))];
        config.error_scale = 0.1;
        if (home)
            config.layers = planet_layers();
        else
        {
            config.layers.resize(1);
            config.layers[0].colour = {static_cast<std::uint8_t>(b.colour[0] * 255.0f), static_cast<std::uint8_t>(b.colour[1] * 255.0f), static_cast<std::uint8_t>(b.colour[2] * 255.0f), 255};
        }

        std::shared_ptr<sl::TerrainSource> source;
        bool has_elevation = false;
        const std::string elevation = orbiter_layer("Elev");
        if (!elevation.empty())
        {
            // Earth's elevation file includes the sea floor; the sea itself stays flat at zero.
            const double max_height = orbiter_max_height(home, sim.universe.name == "Sol" && b.name == "Mars");
            auto data = std::make_shared<kosmos::OrbiterElevation>(elevation, frame, b.radius, home ? 0.0 : -12000.0, max_height,
                                                                   home ? 0.0 : std::numeric_limits<double>::quiet_NaN());
            if (data->ok())
            {
                source = data;
                has_elevation = true;
                std::fprintf(stderr, "[kosmos] %s elevation: %s\n", b.name.c_str(), elevation.c_str());
            }
            else
                std::fprintf(stderr, "[kosmos] %s elevation: %s\n", b.name.c_str(), data->error().c_str());
        }
        std::shared_ptr<kosmos::OrbiterImagery> orbiter_imagery;
        const std::string surface = orbiter_layer("Surf");
        if (!surface.empty())
        {
            orbiter_imagery = std::make_shared<kosmos::OrbiterImagery>(surface, frame, b.radius);
            if (!orbiter_imagery->ok())
            {
                std::fprintf(stderr, "[kosmos] %s imagery: %s\n", b.name.c_str(), orbiter_imagery->error().c_str());
                orbiter_imagery.reset();
            }
            else
                std::fprintf(stderr, "[kosmos] %s terrain imagery: %s\n", b.name.c_str(), surface.c_str());
        }
        std::string lower_name = b.name;
        for (char &c : lower_name)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        std::vector<std::shared_ptr<kosmos::CelestiaImagery>> surface_imagery;
        const std::string celestia_tiles = find_tile_source(sim.root() + "/import/celestia/hires/" + lower_name);
        const std::string surface_image = find_surface_image(sim.root(), b);
        std::vector<std::string> surface_paths;
        const bool generated_ground = b.style.use_generated_ground(b.real_world);
        if (!celestia_tiles.empty())
            surface_paths.push_back(celestia_tiles);
        else if (!surface_image.empty() && !generated_ground)
            surface_paths.push_back(surface_image);
        for (const std::string &path : surface_paths)
        {
            auto imagery = std::make_shared<kosmos::CelestiaImagery>(path, frame, b.radius);
            if (!imagery->ok())
            {
                if (!imagery->error().empty())
                    std::fprintf(stderr, "[kosmos] %s surface imagery: %s\n", b.name.c_str(), imagery->error().c_str());
                continue;
            }
            std::fprintf(stderr, "[kosmos] %s terrain imagery candidate: %s\n", b.name.c_str(), path.c_str());
            surface_imagery.push_back(std::move(imagery));
        }
        if (source)
        {
            std::fprintf(stderr, "[kosmos] %s terrain from Orbiter data\n", b.name.c_str());
        }
        else if (generated_ground)
        {
            make_generated_ground(config, b, frame, home, source);
        }
        else if (home)
        {
            sl::NoiseParameters noise;
            noise.seed = 11;
            noise.frequency = 1.0 / 3.0e6;
            noise.amplitude = 6000.0;
            noise.base_height = -1200.0;
            noise.floor_height = 0.0;
            source = std::make_shared<sl::NoiseSource>(noise);
        }
        else if (orbiter_imagery)
            source = std::make_shared<sl::FunctionSource>([](double, double) { return 0.0; }, 0.0, 0.0);
        else
            return nullptr;
        if (orbiter_imagery || !surface_imagery.empty())
            imagery_colours(config, orbiter_imagery, surface_imagery);
        for (kosmos::Base &base : sim.bases)
            if (sim.universe.find(base.body) == id)
                base.height = source->height_on_sphere(base.direction, b.radius);
        const std::shared_ptr<kosmos::MeshSource> meshes = place_terrain_meshes(sim, b, id, config, source);
        level_ground_under_bases(sim, b, id, source);
        auto result = std::make_unique<BodyTerrain>();
        result->body = id;
        result->orbital_relief = has_elevation && sim.universe.name == "Sol" && b.name == "Mars";
        if (result->orbital_relief)
            std::fprintf(stderr, "[kosmos] Mars orbital terrain relief: on\n");
        result->terrain = std::make_unique<sl::Terrain>(config, source);
        result->frame = frame;
        result->clutter_zones = kosmos::clutter_zones(sim.bases, sim.universe, id, b.radius);
        if (meshes)
            for (const auto &footprint : meshes->footprints())
                result->clutter_zones.push_back({footprint.first, footprint.first, footprint.second + 60.0});
        open_landclass(sim, b, *result);
        if (!detail_disabled)
            apply_detail_texture(sim, b, config, *result);
        return result;
    }

    void settle(sl::Terrain &terrain, const sl::TerrainView &view)
    {
        for (int attempt = 0; attempt < 400; ++attempt)
        {
            terrain.update(view);
            if (loading.started)
                loading.pump();
            if (terrain.stats().pending_chunks == 0)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    bool project(const glm::mat4 &view_proj, const glm::vec3 &p, float &x, float &y)
    {
        const glm::vec4 clip = view_proj * glm::vec4(p, 1.0f);
        if (clip.w <= 0.0f)
            return false;
        x = (clip.x / clip.w * 0.5f + 0.5f) * static_cast<float>(screen_w);
        y = (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) * static_cast<float>(screen_h);
        return true;
    }
} // namespace kosmos::app
