#ifdef __linux__
#include <sys/resource.h>
#endif
#include "app_state.h"

#include <filesystem>

namespace kosmos::app
{
    // The planetbake (SDK) command line that makes the same world, with every value in full so the maps match the game.
    void print_planetbake(const kosmos::Body &b)
    {
        std::printf("for bigger maps, PNG/DDS and more layers use planetbake: planetbake --type %s --seed %d --ocean %.9g --ice %.9g --relief %.9g --tint %.9g,%.9g,%.9g --bands %d --contrast %.9g --radius-km %.9g --out DIR\n",
                    kosmos::archetype_name(b.style.type), b.style.seed, b.style.ocean, b.style.ice, b.style.relief, b.style.tint[0], b.style.tint[1], b.style.tint[2],
                    b.style.bands, b.style.contrast, b.radius / 1000.0);
    }

    int App::list_worlds()
    {
            // --list-worlds mercury|venus|earth|mars|gas [--system N]: worlds of that kind within quantum range, nearest first.
            std::string want = options.list_worlds;
            std::transform(want.begin(), want.end(), want.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (want == "jupiter" || want == "giant" || want == "neptune")
                want = "gas";
            if (want != "mercury" && want != "venus" && want != "earth" && want != "mars" && want != "gas")
            {
                std::fprintf(stderr, "kosmos: world types are mercury venus earth mars gas (jupiter)\n");
                return 1;
            }
            const int here = sim.system_index();
            std::vector<int> ids = sim.galaxy.nearest(here, sim.galaxy.size());
            int found = 0;
            for (const int id : ids)
            {
                const double d = sim.galaxy.distance_ly(here, id);
                if (d > 50.0)
                    break;
                const nlohmann::json system = nlohmann::json::parse(sim.galaxy.system_json(id), nullptr, false);
                if (!system.is_object() || !system.contains("bodies"))
                    continue;
                for (const nlohmann::json &b : system["bodies"])
                {
                    const std::string kind = b.value("kind", "");
                    if ((kind != "planet" && kind != "moon") || !b.contains("style") || b["style"].value("type", "") != want)
                        continue;
                    std::printf("%5.1f ly  %-18s %-26s %s  %.2f R_earth\n", d, sim.galaxy.star(id).name.c_str(), b.value("name", "").c_str(),
                                kind == "moon" ? "moon  " : "planet", b.value("radius", 0.0) / 6.371e6);
                    ++found;
                }
            }
            std::printf("%d %s worlds within 50 ly\n", found, want.c_str());
            return 0;
    }

    int App::generate_world()
    {
            // Makes the far-view image of an invented world into the cache, within 4 threads and 4 GB:
            // --system N --gen-world "Name" [--gen-width 16384] [--gen-threads 4] [--gen-memory MB]
            const int id = sim.universe.find(options.gen_world);
            if (id < 0 || !sim.universe.bodies[static_cast<std::size_t>(id)].style.valid())
            {
                std::fprintf(stderr, "kosmos: no generated world '%s' in this system. Candidates:\n", options.gen_world.c_str());
                for (const kosmos::Body &b : sim.universe.bodies)
                    if (b.style.valid() && !b.minor)
                        std::fprintf(stderr, "  %s (%s)\n", b.name.c_str(), kosmos::archetype_name(b.style.type));
                return 1;
            }
            const kosmos::Body &b = sim.universe.bodies[static_cast<std::size_t>(id)];
            const int width = std::clamp(options.gen_width, 256, 16384) & ~1;
            const std::uint64_t limit = static_cast<std::uint64_t>(options.gen_memory_mb) << 20;
#ifdef __linux__
            // A hard ceiling on address space, so a mistake fails the run rather than the machine.
            struct rlimit cap = {limit + (512ull << 20), limit + (512ull << 20)};
            setrlimit(RLIMIT_AS, &cap);
#endif
            const std::filesystem::path path = kosmos::far_image_path(b.style, width);
            if (std::filesystem::exists(path))
            {
                std::printf("already there: %s\n", path.string().c_str());
                return 0;
            }
            std::printf("%s (%s): %d x %d, %d threads, up to %d MB\ninto %s\n", b.name.c_str(), kosmos::archetype_name(b.style.type), width, width / 2,
                        options.gen_threads, options.gen_memory_mb, path.string().c_str());
            print_planetbake(b);
            const kosmos::PlanetSurface planet_surface(b.style, b.radius);
            std::string problem;
            int last = -1;
            const bool ok = kosmos::generate_far_image(planet_surface, width, static_cast<unsigned>(options.gen_threads), limit, [&](double fraction)
            {
                const int percent = static_cast<int>(fraction * 100.0);
                if (percent != last)
                {
                    last = percent;
                    std::printf("\r%3d%%", percent);
                    std::fflush(stdout);
                }
            }, problem);
            std::printf("\n%s\n", ok ? "done" : problem.c_str());
            return ok ? 0 : 1;
    }

    int App::bake_world()
    {
            // Pre-generates the high-resolution tile set of an invented world: --system N --bake-planet "Name" [--bake-width 16384] [--bake-estimate | --bake-lazy]
            const int id = sim.universe.find(options.bake_body);
            if (id < 0 || !sim.universe.bodies[static_cast<std::size_t>(id)].style.has_ground())
            {
                std::fprintf(stderr, "kosmos: no generated rocky body '%s' in this system. Candidates:\n", options.bake_body.c_str());
                for (const kosmos::Body &b : sim.universe.bodies)
                    if (b.style.has_ground())
                        std::fprintf(stderr, "  %s (%s)\n", b.name.c_str(), kosmos::archetype_name(b.style.type));
                return 1;
            }
            const kosmos::Body &b = sim.universe.bodies[static_cast<std::size_t>(id)];
            const kosmos::BakeEstimate estimate = kosmos::estimate_bake(options.bake_width);
            const std::filesystem::path dir = kosmos::baked_tiles_dir(b.style, b.radius);
            std::printf("%s (%s): %d x %d pixels, %lld tiles, about %s on disk\ninto %s\n", b.name.c_str(), kosmos::archetype_name(b.style.type), estimate.width,
                        estimate.height, static_cast<long long>(estimate.tiles), kosmos::format_bytes(estimate.disk_bytes).c_str(), dir.string().c_str());
            print_planetbake(b);
            if (options.bake_estimate)
                return 0;
            if (options.bake_lazy)
            {
                if (!kosmos::write_lazy_marker(dir, options.bake_width))
                {
                    std::fprintf(stderr, "kosmos: cannot write into %s\n", dir.string().c_str());
                    return 1;
                }
                std::printf("lazy: tiles up to that size are now made as you fly over them and kept there\n");
                return 0;
            }
            const kosmos::PlanetSurface planet_surface(b.style, b.radius);
            std::string bake_error;
            int last = -1;
            const bool ok = kosmos::bake_tiles(planet_surface, dir, options.bake_width, [&](double fraction)
            {
                const int percent = static_cast<int>(fraction * 100.0);
                if (percent != last)
                {
                    last = percent;
                    std::printf("\r%3d%%", percent);
                    std::fflush(stdout);
                }
                return true;
            }, bake_error);
            std::printf("\n%s\n", ok ? "done" : bake_error.c_str());
            return ok ? 0 : 1;
    }

    int App::export_base()
    {
            // Every built-in prop of a base, one scene object each, as a GLB (also a check of the Blender bake).
            for (const kosmos::Base &base : sim.bases)
                if (base.id == options.export_base)
                {
                    std::vector<kosmos::GlbNode> nodes;
                    for (std::size_t i = 0; i < base.objects.size(); ++i)
                    {
                        kosmos::GlbNode node;
                        node.name = std::to_string(i) + "_" + base.objects[i].type;
                        if (kosmos::bake_base_object(base.objects[i], node.mesh))
                            nodes.push_back(std::move(node));
                    }
                    std::string problem;
                    const bool ok = kosmos::write_glb(nodes, options.export_file, problem);
                    std::printf("%zu objects -> %s %s\n", nodes.size(), options.export_file.c_str(), ok ? "" : problem.c_str());
                    return ok ? 0 : 1;
                }
            std::printf("no base %s\n", options.export_base.c_str());
            return 1;
    }

    int App::check_data()
    {
            // Every scenario and base must survive the editors' save and load unchanged.
            int bad = 0;
            for (const kosmos::Scenario &sc : sim.scenarios())
            {
                kosmos::Scenario again;
                std::string problem;
                const std::string text = kosmos::scenario_to_json(sc);
                if (!kosmos::parse_scenario(text, sc.path, again, problem) || kosmos::scenario_to_json(again) != text)
                {
                    std::printf("scenario %s does not round-trip %s\n", sc.id.c_str(), problem.c_str());
                    ++bad;
                }
            }
            {
                std::vector<kosmos::Base> again;
                std::string problem;
                const std::string text = kosmos::bases_to_json(sim.bases);
                if (!kosmos::parse_bases(text, "check", again, problem) || kosmos::bases_to_json(again) != text || again.size() != sim.bases.size())
                {
                    std::printf("bases do not round-trip %s\n", problem.c_str());
                    ++bad;
                }
            }
            std::printf("%zu scenarios, %zu bases, %d problems\n", sim.scenarios().size(), sim.bases.size(), bad);
            return bad == 0 ? 0 : 1;
    }

    // Command line, simulation and scenario choice.
    int App::init_sim(int argc, char **argv)
    {
        options.root = default_root(argc > 0 ? argv[0] : nullptr);
        backend_args = std::vector<char *>{argv[0]};
        if (const int code = parse_command_line(argc, argv, options, backend_args); code != keep_going)
            return code;
        if (!options.capture_camera.empty() && !load_cinematic_camera(options.capture_camera, capture_camera_keys, error))
        {
            std::fprintf(stderr, "kosmos: %s\n", error.c_str());
            return 1;
        }
        if (!options.system_name.empty())
        {
            char *end = nullptr;
            const long index = std::strtol(options.system_name.c_str(), &end, 10);
            options.system = *end == '\0' ? static_cast<int>(index) : sim.galaxy.find(options.system_name);
            if (options.system < 0 || options.system >= sim.galaxy.size())
            {
                std::fprintf(stderr, "kosmos: unknown star system '%s' (try --stars)\n", options.system_name.c_str());
                return 1;
            }
        }
        if (!sim.init(options.root, error, options.system))
        {
            std::fprintf(stderr, "kosmos: %s\n", error.c_str());
            return 1;
        }
        if (!options.scenario_file.empty())
        {
            kosmos::Scenario file_scenario;
            std::string scenario_error;
            if (!kosmos::load_scenario(options.scenario_file, file_scenario, scenario_error))
            {
                std::fprintf(stderr, "kosmos: %s\n", scenario_error.c_str());
                return 1;
            }
            options.scenario = file_scenario.id;
            sim.add_scenario(std::move(file_scenario));
        }
        sim.life_support = kosmos::parse_life_support(options.life_support, sim.life_support);
        // A scenario set in another star system starts there, unless --system says otherwise.
        if (options.system_name.empty() && !options.scenario.empty())
            if (const kosmos::Scenario *sc = sim.find_scenario(options.scenario))
                if (!sc->system.empty())
                {
                    const int star = sim.galaxy.find(sc->system);
                    std::string system_error;
                    if (star < 0)
                        std::fprintf(stderr, "kosmos: scenario %s is set in unknown star system '%s'\n", sc->id.c_str(), sc->system.c_str());
                    else if (star != sim.system_index() && sim.change_system(star, system_error))
                        options.system = star;
                    else if (star != sim.system_index())
                        std::fprintf(stderr, "kosmos: cannot go to %s: %s\n", sc->system.c_str(), system_error.c_str());
                }
        if (!options.list_worlds.empty())
            return list_worlds();
        if (options.list_stars)
        {
            std::printf("%d stars; system %d %s (class %c)\n", sim.galaxy.size(), sim.system_index(), sim.galaxy.star(sim.system_index()).name.c_str(),
                        sim.galaxy.star(sim.system_index()).spectral_class);
            for (const int id : sim.galaxy.nearest(sim.system_index(), 20))
                std::printf("%6d  %-22s class %c  %6.1f ly  %d planets\n", id, sim.galaxy.star(id).name.c_str(), sim.galaxy.star(id).spectral_class,
                            sim.galaxy.distance_ly(sim.system_index(), id), sim.galaxy.planet_count(id));
            return 0;
        }
        if (!options.gen_world.empty())
            return generate_world();
        if (!options.bake_body.empty())
            return bake_world();
        if (options.list_scenarios)
        {
            for (const kosmos::Scenario &sc : sim.scenarios())
                std::printf("%-22s %-16s %s\n", sc.id.c_str(), sc.group.c_str(), sc.name.c_str());
            return 0;
        }
        if (!options.export_base.empty())
            return export_base();
        if (options.check_data)
            return check_data();
        if (!options.scenario.empty() && !sim.find_scenario(options.scenario))
        {
            std::fprintf(stderr, "kosmos: scenario \"%s\" not found\n", options.scenario.c_str());
            return 1;
        }
    return keep_going;
    }

    // The start date and the headless self-tests.
    int App::init_start_date()
    {
        // With no scenario or start flags, load the "default" scenario (scenarios/default.json, overridable from ~/.config/kosmos/scenarios).
        if (options.scenario.empty() && options.selftest <= 0.0 && !options.orbit_start && options.orbit_body.empty() && !options.ship_name_explicit && options.ship_name == "Kestrel" && sim.find_scenario("default"))
            options.scenario = "default";
        // The start date shapes the planets' positions and the home planet's orientation, which the terrain bakes in, so it is set before the terrain is built.
        {
            double start_jd = options.start_jd;
            if (start_jd <= 0.0 && !options.scenario.empty())
                if (const kosmos::Scenario *sc = sim.find_scenario(options.scenario))
                    start_jd = sc->epoch_jd;
            // Unless the command line or the scenario gives a date, start on the real date and time, so the Earth is turned the way it really is.
            if (start_jd <= 0.0 && options.selftest <= 0.0)
                start_jd = 2440587.5 + std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count() / 86400.0;
            if (start_jd > 0.0)
            {
                sim.start_epoch_jd = start_jd;
                std::string epoch_error;
                if (!sim.change_system(options.system, epoch_error))
                {
                    std::fprintf(stderr, "kosmos: cannot start on that date: %s\n", epoch_error.c_str());
                    return 1;
                }
            }
        }
        if (options.selftest > 0.0)
        {
            // The selftest has no terrain, so bases stand at sea level.
            for (kosmos::Base &base : sim.bases)
            {
                const int body = sim.universe.find(base.body);
                if (body >= 0)
                {
                    base.direction = kosmos::base_direction(sim.universe, body, base);
                    base.pole = sim.universe.pole(body);
                    base.height = 0.0;
                    kosmos::register_base_beacons(sim.nav, sim.universe, body, base);
                }
            }
            Ship *flown = start_scenario(sim, options, TerrainSet());
            if (!flown)
            {
                if (!options.scenario.empty() && sim.find_scenario(options.scenario))
                    std::fprintf(stderr, "kosmos: unable to start scenario \"%s\"\n", options.scenario.c_str());
                else
                    std::fprintf(stderr, "No Kestrel addon found under %s\n", sim.root().c_str());
                return 1;
            }
            apply_sets(sim.props, options.sets);
            const std::string replay_test = sim.props.get_string("/selftest/flight-replay-path");
            if (!replay_test.empty())
            {
                ship = flown;
                earth_id = sim.universe.find("Earth");
                return test_flight_playback(replay_test);
            }
            const std::string reopen_test = sim.props.get_string("/selftest/open-flight-replay-path");
            if (!reopen_test.empty())
            {
                ship = flown;
                earth_id = sim.universe.find("Earth");
                return test_open_flight_playback(reopen_test);
            }
            kosmos::SelftestOptions test;
            test.seconds = options.selftest;
            test.warp = options.selftest_warp;
            test.gear_up = options.selftest_gear_up;
            test.drop = options.selftest_drop;
            test.drop_atom = options.selftest_drop_atom;
            test.arsenal = options.selftest_arsenal;
            test.timed_burn = options.selftest_timed_burn;
            test.orbital_sas = options.selftest_orbital_sas;
            test.ui_windows = options.selftest_ui_windows;
            test.vessel_ascent = options.selftest_vessel_ascent;
            test.save = options.selftest_save;
            test.orbitcomp = options.selftest_orbitcomp;
            test.weapons = options.selftest_weapons;
            test.throttle = options.selftest_throttle;
            test.select_instruments = [this] {
                props_ref = &sim.props;
                ship = sim.active_ship();
                select_default_instruments();
            };
            return kosmos::run_selftest(sim, flown, test);
        }
    return keep_going;
    }

    // The window and the 3D surface.
    int App::init_window()
    {
        sl::set_text_panel_style({true});
        if (!options.capture_dir.empty())
        {
            std::error_code ec;
            std::filesystem::create_directories(options.capture_dir, ec);
            const bool directory = !ec && std::filesystem::is_directory(options.capture_dir, ec);
            if (ec || !directory)
            {
                std::fprintf(stderr, "kosmos: cannot create frame directory %s: %s\n",
                             options.capture_dir.c_str(), ec ? ec.message().c_str() : "not a directory");
                return 1;
            }
            screen_w = options.capture_width;
            screen_h = options.capture_height;
        }
        if (!sl::configure_graphics_backend_from_args(static_cast<int>(backend_args.size()), backend_args.data()))
            return -1;
        if (!sl::set_gfx_mode(sl::GFX_AUTODETECT_WINDOWED, screen_w, screen_h))
        {
            std::fprintf(stderr, "Failed to initialize display: %s\n", sl::last_error().c_str());
            return -1;
        }
        sl::set_window_title("Kosmos");
        {
            bool want_fullscreen = options.fullscreen;
            if (options.capture_dir.empty() && !options.scenario.empty())
                if (const kosmos::Scenario *sc = sim.find_scenario(options.scenario))
                    want_fullscreen = want_fullscreen || sc->fullscreen;
            if (want_fullscreen)
                sl::set_fullscreen(true);
            screen_w = sl::screen_width();
            screen_h = sl::screen_height();
            if (!options.capture_dir.empty() && (screen_w != options.capture_width || screen_h != options.capture_height))
            {
                std::fprintf(stderr, "kosmos: requested capture size %dx%d, got %dx%d\n",
                             options.capture_width, options.capture_height, screen_w, screen_h);
                return -1;
            }
        }
    return keep_going;
    }

    // Textures, meshes and the sky.
    int App::init_render()
    {
        loading.begin();
        screen_w = sl::screen_width();
        screen_h = sl::screen_height();
        surface_msaa = std::clamp(sim.props.get_int("/sim/settings/gfx-msaa", 4), 1, 8);
        surface = sl::create_surface_3d(screen_w, screen_h, surface_msaa);
        if (!surface && surface_msaa > 1)
        {
            surface_msaa = 1;
            surface = sl::create_surface_3d(screen_w, screen_h, 1);
        }
        surface_w = screen_w;
        surface_h = screen_h;
        if (!surface)
        {
            std::fprintf(stderr, "3D surface unavailable on this backend: %s\n", sl::last_error().c_str());
            sl::shutdown();
            return -1;
        }

        loading.note("Generating the sky and star textures");
        vis.sphere = sl::create_sphere_mesh(1.0f, 192, 96, {255, 255, 255, 255});
        vis.glow = sl::create_sphere_mesh(1.0f, 14, 8, {255, 255, 255, 255});
        vis.quad = sl::create_plane_mesh(2.0f, 2.0f, {255, 255, 255, 255});
        vis.glow_texture = make_glow_texture();
        vis.plasma_texture = make_plasma_texture();
        vis.star_surface = make_star_surface(0, 11);
        vis.star_filaments = make_star_surface(1, 29);
        vis.star_corona = make_star_corona(5, 5.0);
        vis.star_corona_far = make_star_corona(41, 14.0);
        vis.star_rays = make_star_rays();
        vis.star_limb = make_star_limb();
        vis.sky = make_sky_texture();
        vis.puff_texture = make_puff_texture();
        vis.cloud_texture = fx::make_cloud_texture();
        vis.starfield = make_starfield();
        vis.tail = make_tail_mesh();
        if (!vis.sphere || !vis.glow || !vis.quad || !vis.glow_texture || !vis.plasma_texture || !vis.puff_texture || !vis.cloud_texture || !vis.starfield)
        {
            std::fprintf(stderr, "Failed to create resources: %s\n", sl::last_error().c_str());
            sl::shutdown();
            return -1;
        }
        settings_defaults(sim.props);
        settings_load(sim.props);
        if (options.world_texture >= 0)
            sim.props.set_int("/sim/settings/world-texture", options.world_texture);
        if (options.max_texture < 0)
        {
            const int saved = sim.props.get_int("/sim/settings/max-texture");
            options.max_texture = saved > 0 ? saved : 16384;
        }
        loading.note("Loading Earth's clouds and night lights");
        // Orbiter's Earth cloud and night-light layers, when imported with tools/orbiter_import.py.
        std::string clouds_path = find_texture_in_directory(sim.root(), "/data/textures-hires/", "earth_clouds", {".dds"});
        if (clouds_path.empty())
            clouds_path = find_texture_in_directory(sim.root(), "/data/orbiter-defaults/earth/", "earth_clouds", {".dds"});
        if (clouds_path.empty())
            clouds_path = find_texture_in_directory(sim.root(), "/data/textures/", "earth_clouds", {".dds"});
        if (sl::Bitmap *clouds = load_optional_texture(clouds_path, options.max_texture))
        {
            sl::destroy_bitmap(vis.cloud_texture);
            vis.cloud_texture = clouds;
        }
        std::string lights_path = find_texture_in_directory(sim.root(), "/data/textures-hires/", "earth_lights", {".dds", ".jpg", ".png"});
        if (lights_path.empty())
            lights_path = find_texture_in_directory(sim.root(), "/data/orbiter-defaults/earth/", "earth_lights", {".dds"});
        if (lights_path.empty())
            lights_path = find_texture_in_directory(sim.root(), "/data/textures/", "earth_lights", {".dds", ".jpg", ".png"});
        vis.lights_texture = load_night_lights(lights_path, options.max_texture);
        if (vis.lights_texture)
            std::fprintf(stderr, "[kosmos] Earth night lights: %s\n", lights_path.c_str());
        earth_id = -1;
        earth_body = nullptr;
        terrain = nullptr;
        cloud_shell = nullptr;
        home_cloud_texture = nullptr; // generated weather for an invented home world; Earth uses the imported image
        home_cloud_height = cloud_height;
        base_renderer.root = sim.root();
        base_beacon_mark = sim.nav.beacon_count();
    return keep_going;
    }
} // namespace kosmos::app
