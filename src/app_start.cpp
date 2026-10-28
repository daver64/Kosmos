#include "app_state.h"

namespace kosmos::app
{
    // Command line, simulation and scenario choice.
    int App::init_sim(int argc, char **argv)
    {
        options.root = default_root(argc > 0 ? argv[0] : nullptr);
        backend_args = std::vector<char *>{argv[0]};
        if (const int code = parse_command_line(argc, argv, options, backend_args); code != keep_going)
            return code;

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
        if (options.list_stars)
        {
            std::printf("%d stars; system %d %s (class %c)\n", sim.galaxy.size(), sim.system_index(), sim.galaxy.star(sim.system_index()).name.c_str(),
                        sim.galaxy.star(sim.system_index()).spectral_class);
            for (const int id : sim.galaxy.nearest(sim.system_index(), 20))
                std::printf("%6d  %-22s class %c  %6.1f ly  %d planets\n", id, sim.galaxy.star(id).name.c_str(), sim.galaxy.star(id).spectral_class,
                            sim.galaxy.distance_ly(sim.system_index(), id), sim.galaxy.planet_count(id));
            return 0;
        }
        if (!options.bake_body.empty())
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
            const kosmos::PlanetSurface surface(b.style, b.radius);
            std::string bake_error;
            int last = -1;
            const bool ok = kosmos::bake_tiles(surface, dir, options.bake_width, [&](double fraction)
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
        if (options.list_scenarios)
        {
            for (const kosmos::Scenario &sc : sim.scenarios())
                std::printf("%-22s %-16s %s\n", sc.id.c_str(), sc.group.c_str(), sc.name.c_str());
            return 0;
        }
        if (!options.export_base.empty())
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
                    std::string error;
                    const bool ok = kosmos::write_glb(nodes, options.export_file, error);
                    std::printf("%zu objects -> %s %s\n", nodes.size(), options.export_file.c_str(), ok ? "" : error.c_str());
                    return ok ? 0 : 1;
                }
            std::printf("no base %s\n", options.export_base.c_str());
            return 1;
        }
        if (options.check_data)
        {
            // Every scenario and base must survive the editors' save and load unchanged.
            int bad = 0;
            for (const kosmos::Scenario &sc : sim.scenarios())
            {
                kosmos::Scenario again;
                std::string error;
                const std::string text = kosmos::scenario_to_json(sc);
                if (!kosmos::parse_scenario(text, sc.path, again, error) || kosmos::scenario_to_json(again) != text)
                {
                    std::printf("scenario %s does not round-trip %s\n", sc.id.c_str(), error.c_str());
                    ++bad;
                }
            }
            {
                std::vector<kosmos::Base> again;
                std::string error;
                const std::string text = kosmos::bases_to_json(sim.bases);
                if (!kosmos::parse_bases(text, "check", again, error) || kosmos::bases_to_json(again) != text || again.size() != sim.bases.size())
                {
                    std::printf("bases do not round-trip %s\n", error.c_str());
                    ++bad;
                }
            }
            std::printf("%zu scenarios, %zu bases, %d problems\n", sim.scenarios().size(), sim.bases.size(), bad);
            return bad == 0 ? 0 : 1;
        }
    return keep_going;
    }

    // The start date and the headless self-tests.
    int App::init_start_date()
    {
        // With no scenario or start flags, load the "default" scenario (data/scenarios/default.json, overridable from ~/.config/kosmos/scenarios).
        if (options.scenario.empty() && options.selftest <= 0.0 && !options.orbit_start && options.orbit_body.empty() && options.ship_name == "Kestrel" && sim.find_scenario("default"))
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
            if (start_jd > 0.0 && options.system == 0)
            {
                sim.start_epoch_jd = start_jd;
                std::string epoch_error;
                if (!sim.change_system(0, epoch_error))
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
                std::fprintf(stderr, "No Kestrel addon found under %s\n", sim.root().c_str());
                return 1;
            }
            apply_sets(sim.props, options.sets);
            kosmos::SelftestOptions test;
            test.seconds = options.selftest;
            test.warp = options.selftest_warp;
            test.gear_up = options.selftest_gear_up;
            test.drop = options.selftest_drop;
            test.drop_atom = options.selftest_drop_atom;
            test.arsenal = options.selftest_arsenal;
            test.throttle = options.selftest_throttle;
            return kosmos::run_selftest(sim, flown, test);
        }
    return keep_going;
    }

    // The window and the 3D surface.
    int App::init_window()
    {
        sl::set_text_panel_style({true});
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
            if (!options.scenario.empty())
                if (const kosmos::Scenario *sc = sim.find_scenario(options.scenario))
                    want_fullscreen = want_fullscreen || sc->fullscreen;
            if (want_fullscreen)
                sl::set_fullscreen(true);
            screen_w = sl::screen_width();
            screen_h = sl::screen_height();
        }
    return keep_going;
    }

    // Textures, meshes and the sky.
    int App::init_render()
    {
        loading.begin();
        surface = sl::create_surface_3d(screen_w, screen_h, 1);
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
        vis.star_surface = make_star_surface(0, 11);
        vis.star_filaments = make_star_surface(1, 29);
        vis.star_corona = make_star_corona(5, 5.0);
        vis.star_corona_far = make_star_corona(41, 14.0);
        vis.star_rays = make_star_rays();
        vis.star_limb = make_star_limb();
        vis.sky = make_sky_texture();
        vis.nebula_atlas = sl::load_bitmap((sim.root() + "/data/open-game-art/textures/nebula/proc_sheet_nebula.jpg").c_str());
        if (vis.nebula_atlas)
        {
            vis.nebula_atlas->smooth = true;
            vis.nebula_atlas->mipmaps = true;
        }
        vis.puff_texture = make_puff_texture();
        vis.cloud_texture = fx::make_cloud_texture();
        vis.starfield = make_starfield();
        vis.tail = make_tail_mesh();
        if (!vis.sphere || !vis.glow || !vis.quad || !vis.glow_texture || !vis.puff_texture || !vis.cloud_texture || !vis.starfield)
        {
            std::fprintf(stderr, "Failed to create resources: %s\n", sl::last_error().c_str());
            sl::shutdown();
            return -1;
        }
        loading.note("Loading Earth's clouds and night lights");
        // Orbiter's Earth cloud and night-light layers, when imported with tools/orbiter_import.py.
        if (sl::Bitmap *clouds = load_optional_texture(find_texture_file(sim.root(), "earth_clouds", {".dds"}), options.max_texture))
        {
            sl::destroy_bitmap(vis.cloud_texture);
            vis.cloud_texture = clouds;
        }
        vis.lights_texture = load_optional_texture(find_texture_file(sim.root(), "earth_lights", {".dds", ".jpg", ".png"}), options.max_texture);
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
