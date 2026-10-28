#include "sl.h"
#include "props3d.h"
#include "flight.h"
#include "example_fx.h"
#include "sim.h"
#include "lightning.h"
#include "lua_console.h"
#include "joystick.h"
#include "menu.h"
#include "netplay.h"
#include "planet_data.h"
#include "terrain_clutter.h"
#include "planet_bake.h"
#include "blender_edit.h"
#include "usercam.h"
#include "eva.h"
#include "eva_solids.h"
#include "ordnance.h"
#include "settings.h"
#include "ship_visuals.h"
#include "exhaust_fx.h"
#include "cockpit_panels.h"
#include "map_views.h"
#include "body_terrain.h"
#include "loading_screen.h"
#include "surface_textures.h"
#include "scenario_start.h"
#include "multiplayer_ui.h"
#include "app.h"
#include "procedural.h"
#include "selftest.h"
#include "engine_sound.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <thread>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif
#include <mutex>
#include <vector>

// Kosmos: a space flight simulator. The first milestone flies a two-stage launcher from the pad to Earth orbit.
//
//   B: wheel brakes   F: cycle flaps (aircraft)   G or L: landing gear up/down (retractable gear)   N: galaxy map
//   Keypad +/-/0/.: hover engines (ships with a hover group)
//   Z/X: full / cut throttle   Shift/Ctrl: raise / lower throttle   Space: stage
//   W/S: pitch   A/D: yaw   Q/E: roll   Arrows: RCS translate   T: cycle attitude hold   P: ascent autopilot
//   , / . : time warp   [ / ] : zoom   Right-drag: look   O: orbit line   M: sound   R: reset   Esc: exit

#ifndef KOSMOS_ROOT
#define KOSMOS_ROOT "."
#endif

namespace
{
    using namespace kosmos::app;
    using kosmos::Controls;
    using kosmos::Ship;
    using kosmos::Sim;
    using kosmos::EngineSound;
    using kosmos::PropertyBrowser;
    using kosmos::apply_sets;
    using kosmos::draw_property_browser;
    using kosmos::settings_defaults;
    using kosmos::settings_load;
    using kosmos::settings_path;
    using kosmos::settings_save;

    // ---------------------------------------------------------------------------------------------------------
    // Launch site and scenario
    // ---------------------------------------------------------------------------------------------------------

    // ---------------------------------------------------------------------------------------------------------
    // Visuals
    // ---------------------------------------------------------------------------------------------------------

    using kosmos::make_cloud_deck_mesh;
    using kosmos::make_glow_texture;
    using kosmos::make_limb_mesh;
    using kosmos::make_nebulae;
    using kosmos::make_puff_texture;
    using kosmos::make_rain_mesh;
    using kosmos::make_ring_mesh;
    using kosmos::make_sky_texture;
    using kosmos::make_star_corona;
    using kosmos::make_star_limb;
    using kosmos::make_star_rays;
    using kosmos::make_star_surface;
    using kosmos::make_starfield;
    using kosmos::make_tail_mesh;
    using kosmos::update_cloud_deck;
    using kosmos::update_limb_mesh;
    using kosmos::update_rain_mesh;


} // namespace

// A packaged build keeps data/ beside the executable; a development build falls back to the source tree.
static std::string default_root(const char *argv0)
{
    namespace fs = std::filesystem;
    std::vector<fs::path> candidates;
    std::error_code ec;
#ifndef _WIN32
    candidates.push_back(fs::read_symlink("/proc/self/exe", ec).parent_path());
#endif
    if (argv0 && *argv0)
        candidates.push_back(fs::absolute(fs::path(argv0), ec).parent_path());
    candidates.push_back(fs::current_path(ec));
    for (const fs::path &c : candidates)
        if (!c.empty() && fs::exists(c / "data" / "sol.json", ec))
            return c.string();
    return KOSMOS_ROOT;
}


int main(int argc, char **argv)
{
    Options options;
    options.root = default_root(argc > 0 ? argv[0] : nullptr);
    std::vector<char *> backend_args{argv[0]};
    for (int i = 1; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--version"))
        {
            std::printf("Kosmos %s\n", KOSMOS_VERSION);
            return 0;
        }
        else if (!std::strcmp(argv[i], "--gen-planet") && i + 3 < argc)
        {
            // Developer preview: --gen-planet TYPE SEED OUT.png [WIDTH] writes an invented world's far-view image and exits.
            kosmos::PlanetStyle style;
            style.type = kosmos::archetype_from_name(argv[i + 1]);
            style.seed = std::atoi(argv[i + 2]);
            const std::string out = argv[i + 3];
            const int width = i + 4 < argc && std::atoi(argv[i + 4]) > 0 ? std::atoi(argv[i + 4]) : 1024;
            static const float tints[][3] = {{0.6f, 0.6f, 0.6f}, {0.6f, 0.6f, 0.6f}, {0.88f, 0.75f, 0.47f}, {0.24f, 0.43f, 0.67f}, {0.75f, 0.4f, 0.25f}, {0.85f, 0.7f, 0.5f}};
            std::copy(tints[static_cast<int>(style.type)], tints[static_cast<int>(style.type)] + 3, style.tint);
            if (!style.valid())
            {
                std::fprintf(stderr, "types: mercury venus earth mars gas\n");
                return 1;
            }
            const kosmos::PlanetSurface surface(style, 6.0e6);
            sl::Bitmap *bitmap = surface.make_texture(width, width / 2);
            if (!bitmap || !sl::save_bitmap(bitmap, out))
            {
                std::fprintf(stderr, "could not write %s\n", out.c_str());
                return 1;
            }
            std::printf("wrote %s\n", out.c_str());
            return 0;
        }
        else if (!std::strcmp(argv[i], "--root") && i + 1 < argc)
            options.root = argv[++i];
        else if (!std::strcmp(argv[i], "--selftest") && i + 1 < argc)
            options.selftest = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--system") && i + 1 < argc)
            options.system_name = argv[++i];
        else if (!std::strcmp(argv[i], "--stars"))
            options.list_stars = true;
        else if (!std::strcmp(argv[i], "--list-worlds") && i + 1 < argc)
            options.list_worlds = argv[++i];
        else if (!std::strcmp(argv[i], "--galaxy-map"))
            options.galaxy_view = true;
        else if (!std::strcmp(argv[i], "--quantum"))
            options.quantum_window = true;
        else if (!std::strcmp(argv[i], "--test-jump-body") && i + 1 < argc)
            options.test_jump_body = argv[++i];
        else if (!std::strcmp(argv[i], "--world-texture") && i + 1 < argc)
            options.world_texture = std::max(0, std::atoi(argv[++i]));
        else if (!std::strcmp(argv[i], "--test-scenario") && i + 1 < argc)
            options.test_scenario = argv[++i];
        else if (!std::strcmp(argv[i], "--test-jump") && i + 1 < argc)
            options.test_jump = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--max-texture") && i + 1 < argc)
            options.max_texture = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--host"))
        {
            options.net_host = true;
            if (i + 1 < argc && std::isdigit(static_cast<unsigned char>(argv[i + 1][0])))
                options.net_port = std::atoi(argv[++i]);
        }
        else if (!std::strcmp(argv[i], "--join") && i + 1 < argc)
            options.net_join = argv[++i];
        else if (!std::strcmp(argv[i], "--name") && i + 1 < argc)
            options.net_name = argv[++i];
        else if (!std::strcmp(argv[i], "--advertise") && i + 1 < argc)
            options.net_advertise = argv[++i];
        else if (!std::strcmp(argv[i], "--irc") && i + 1 < argc)
            options.net_irc = argv[++i];
        else if (!std::strcmp(argv[i], "--channel") && i + 1 < argc)
            options.net_channel = argv[++i];
        else if (!std::strcmp(argv[i], "--lobby"))
            options.net_window = true;
        else if (!std::strcmp(argv[i], "--fullscreen"))
            options.fullscreen = true;
        else if (!std::strcmp(argv[i], "--render-style") && i + 1 < argc)
        {
            const std::string v = argv[++i];
            options.render_style = v == "flat" ? 1 : v == "wireframe" ? 2 : 0;
        }
        else if (!std::strcmp(argv[i], "--no-clouds"))
            options.no_clouds = true;
        else if (!std::strcmp(argv[i], "--no-clutter"))
            options.no_clutter = true;
        else if (!std::strcmp(argv[i], "--eva-walk"))
            options.eva = options.eva_walk = true;
        else if (!std::strcmp(argv[i], "--eva"))
            options.eva = true;
        else if (!std::strcmp(argv[i], "--perf"))
            options.perf = true;
        else if (!std::strcmp(argv[i], "--no-tiles"))
            options.no_tiles = true;
        else if (!std::strcmp(argv[i], "--no-detail"))
            detail_disabled = true;
        else if (!std::strcmp(argv[i], "--no-relief"))
            options.no_relief = true;
        else if (!std::strcmp(argv[i], "--blender") && i + 1 < argc)
            options.blender = argv[++i];
        else if (!std::strcmp(argv[i], "--part-editor"))
            options.part_window = true;
        else if (!std::strcmp(argv[i], "--hide-info"))
            options.hide_info = true;
        else if (!std::strcmp(argv[i], "--show-info"))
            options.show_info = true;
        else if (!std::strcmp(argv[i], "--systems-window"))
            options.systems_window = true;
        else if (!std::strcmp(argv[i], "--life-support") && i + 1 < argc)
            options.life_support = argv[++i];
        else if (!std::strcmp(argv[i], "--hud") && i + 1 < argc)
            options.hud = argv[++i];
        else if (!std::strcmp(argv[i], "--camera-editor"))
            options.camera_editor = true;
        else if (!std::strcmp(argv[i], "--user-camera") && i + 1 < argc)
            options.user_camera = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--camera-window"))
            options.camera_window = true;
        else if (!std::strcmp(argv[i], "--aircraft-autopilot-window"))
            options.aircraft_autopilot = true;
        else if (!std::strcmp(argv[i], "--spacecraft-autopilot-window"))
            options.spacecraft_autopilot = true;
        else if (!std::strcmp(argv[i], "--settings-window"))
            options.settings_window = true;
        else if (!std::strcmp(argv[i], "--lua-window"))
            options.lua_window = true;
        else if (!std::strcmp(argv[i], "--export-base") && i + 2 < argc)
        {
            options.export_base = argv[++i];
            options.export_file = argv[++i];
        }
        else if (!std::strcmp(argv[i], "--check-data"))
            options.check_data = true;
        else if (!std::strcmp(argv[i], "--menu"))
            options.menu_visible = true;
        else if (!std::strcmp(argv[i], "--scenario-editor"))
            options.scenario_editor = true;
        else if (!std::strcmp(argv[i], "--base-editor"))
            options.base_editor = true;
        else if (!std::strcmp(argv[i], "--property-window"))
            options.property_window = true;
        else if (!std::strcmp(argv[i], "--enter-building") && i + 1 < argc)
            options.enter_building = argv[++i];
        else if (!std::strcmp(argv[i], "--base-cam") && i + 1 < argc)
            options.view_base = argv[++i];
        else if (!std::strcmp(argv[i], "--selftest-throttle") && i + 1 < argc)
            options.selftest_throttle = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--selftest-gear-up"))
            options.selftest_gear_up = true;
        else if (!std::strcmp(argv[i], "--selftest-drop") && i + 1 < argc)
            options.selftest_drop = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--selftest-arsenal"))
            options.selftest_arsenal = true;
        else if (!std::strcmp(argv[i], "--selftest-drop-atom"))
            options.selftest_drop_atom = true;
        else if (!std::strcmp(argv[i], "--selftest-warp") && i + 1 < argc)
            options.selftest_warp = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--panel") && i + 1 < argc)
            options.panel = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--shot-stages") && i + 1 < argc)
            options.shot_stages = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--shot-throttle") && i + 1 < argc)
            options.shot_throttle = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--shot") && i + 1 < argc)
            options.shot = argv[++i];
        else if (!std::strcmp(argv[i], "--record") && i + 1 < argc)
            options.record_seconds = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--shot-at") && i + 1 < argc)
            options.shot_at = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--autopilot"))
            options.autopilot = true;
        else if (!std::strcmp(argv[i], "--cockpit"))
            options.cockpit = true;
        else if (!std::strcmp(argv[i], "--cockpit3d"))
            options.cockpit = options.cockpit3d = true;
        else if (!std::strcmp(argv[i], "--scenario") && i + 1 < argc)
            options.scenario = argv[++i];
        else if (!std::strcmp(argv[i], "--target") && i + 1 < argc)
            options.target = argv[++i];
        else if (!std::strcmp(argv[i], "--set") && i + 1 < argc)
            options.sets.push_back(argv[++i]);
        else if (!std::strcmp(argv[i], "--bake-planet") && i + 1 < argc)
            options.bake_body = argv[++i];
        else if (!std::strcmp(argv[i], "--bake-width") && i + 1 < argc)
            options.bake_width = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--bake-lazy"))
            options.bake_lazy = true;
        else if (!std::strcmp(argv[i], "--bake-estimate"))
            options.bake_estimate = true;
        else if (!std::strcmp(argv[i], "--list-scenarios"))
            options.list_scenarios = true;
        else if (!std::strcmp(argv[i], "--mfd") && i + 1 < argc)
            options.mfd_display = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--mfd-left") && i + 1 < argc)
            options.mfd_left = argv[++i];
        else if (!std::strcmp(argv[i], "--mfd-right") && i + 1 < argc)
            options.mfd_right = argv[++i];
        else if (!std::strcmp(argv[i], "--ship") && i + 1 < argc)
        {
            // A number picks among the ships spawned in orbit; a name starts the scenario with that ship.
            const char *value = argv[++i];
            if (std::isdigit(static_cast<unsigned char>(value[0])))
                options.ship_index = std::atoi(value);
            else
                options.ship_name = value;
        }
        else if (!std::strcmp(argv[i], "--map-scale") && i + 1 < argc)
            options.map_scale = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--map-focus") && i + 1 < argc)
            options.map_focus = argv[++i];
        else if (!std::strcmp(argv[i], "--map"))
            options.map = true;
        else if (!std::strcmp(argv[i], "--orbit"))
            options.orbit_start = true;
        else if (!std::strcmp(argv[i], "--orbit-body") && i + 1 < argc)
            options.orbit_body = argv[++i];
        else if (!std::strcmp(argv[i], "--cam-pitch") && i + 1 < argc)
            options.cam_pitch = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--cam-yaw") && i + 1 < argc)
            options.cam_yaw = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--cam-fov") && i + 1 < argc)
            options.cam_fov = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--cam-antisun"))
            options.cam_antisun = true;
        else if (!std::strcmp(argv[i], "--cam-sunward"))
            options.cam_sunward = true;
        else if (!std::strcmp(argv[i], "--cam-distance") && i + 1 < argc)
            options.cam_distance = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--ambient") && i + 1 < argc)
            options.ambient = static_cast<float>(std::atof(argv[++i]));
        else if (!std::strcmp(argv[i], "--orbit-phase") && i + 1 < argc)
            options.orbit_phase = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--sunlit"))
            options.sunlit = true;
        else if (!std::strcmp(argv[i], "--orbit-half"))
            options.orbit_half = true;
        else if (!std::strcmp(argv[i], "--orbit-alt") && i + 1 < argc)
            options.orbit_altitude = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--date") && i + 1 < argc)
        {
            if (!kosmos::parse_epoch(argv[++i], options.start_jd))
            {
                std::fprintf(stderr, "kosmos: cannot read --date \"%s\" (use \"YYYY-MM-DD HH:MM:SS\" in GMT, or an MJD)\n", argv[i]);
                return 1;
            }
        }
        else if (!std::strcmp(argv[i], "--mjd") && i + 1 < argc)
            options.start_jd = std::atof(argv[++i]) + kosmos::mjd_offset;
        else if (!std::strcmp(argv[i], "--earth-turn") && i + 1 < argc)
            options.earth_turn = std::atof(argv[++i]) * 3.14159265358979 / 180.0;
        else if (!std::strcmp(argv[i], "--warp-to") && i + 1 < argc)
            options.shot_time = std::atof(argv[++i]);
        else
            backend_args.push_back(argv[i]);
    }

    Sim sim;
    std::string error;
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
    loading.begin();
    sl::Surface3D *surface = sl::create_surface_3d(screen_w, screen_h, 1);
    int surface_w = screen_w, surface_h = screen_h;
    if (!surface)
    {
        std::fprintf(stderr, "3D surface unavailable on this backend: %s\n", sl::last_error().c_str());
        sl::shutdown();
        return -1;
    }

    loading.note("Generating the sky and star textures");
    Visuals vis;
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
    int earth_id = -1;
    const kosmos::Body *earth_body = nullptr;
    std::vector<std::unique_ptr<BodyTerrain>> body_terrains;
    sl::Terrain *terrain = nullptr;
    TerrainSet terrain_set;
    sl::Mesh *cloud_shell = nullptr;
    sl::Bitmap *home_cloud_texture = nullptr; // generated weather for an invented home world; Earth uses the imported image
    double home_cloud_height = cloud_height;
    kosmos::BaseRenderer base_renderer;
    base_renderer.root = sim.root();
    const std::size_t base_beacon_mark = sim.nav.beacon_count();

    // Everything drawn or collided with that belongs to one star system; a quantum jump drops it and builds the next.
    auto leave_system = [&]()
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
    };
    // Dates and bodies that depend on where the planets stand: re-run when the start date changes without a rebuild of the system.
    auto place_worlds = [&]()
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
    };
    // A scenario on another date in the same system keeps the terrain, textures and cloud layers; only the planets' positions
    // and the turn of each terrain body (baked into the terrain when it was built) are brought to the new date.
    auto change_date = [&](double epoch_jd, std::string &error) -> bool
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
        if (!sim.change_system(sim.system_index(), error))
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
    };
    // Remakes the far-view image of every invented world at the width now chosen in the settings, without leaving the system.
    auto reload_world_textures = [&]()
    {
        for (std::size_t i = 0; i < sim.universe.bodies.size() && i < vis.body_textures.size(); ++i)
        {
            const kosmos::Body &b = sim.universe.bodies[i];
            if (b.minor || !b.style.valid() || !b.texture.empty())
                continue;
            const std::string image = generated_surface_image(b);
            if (image.empty())
                continue;
            if (sl::Bitmap *surface = load_surface_texture(image, options.max_texture))
            {
                sl::destroy_bitmap(vis.body_textures[i]);
                vis.body_textures[i] = surface;
            }
        }
    };
    auto enter_system = [&]() -> bool
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
                vis.body_textures.push_back(b.name == "Moon" ? fx::make_moon_texture() : fx::make_detail_texture(average, 31 + static_cast<int>(i) * 7, 0.45f, 0.15f, 256));
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
                    if (sl::Bitmap *surface = load_surface_texture(image, options.max_texture))
                    {
                        sl::destroy_bitmap(vis.body_textures.back());
                        vis.body_textures.back() = surface;
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
                    celestia_normal = find_image_in_folder(sim.root() + "/data/celestia/hires/" + lower + "_normal");
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

        // Tiled Earth imagery (optional, not in the repository: see the Developers Manual) replaces the single image on the sphere.
        if (!options.no_tiles)
        {
            const int home = sim.universe.home_id();
            if (home >= 0 && sim.universe.bodies[static_cast<std::size_t>(home)].name == "Earth")
            {
                loading.note("Opening the Earth imagery tiles");
                const std::string hires = sim.root() + "/data/celestia/hires/";
                sl::PlanetTilesOptions tiles;
                // Skirts show as bright lines along tile edges from orbit.
                tiles.skirt = 0.0f;
                // Full-strength, tight glint blows out into a white disc from orbit.
                tiles.specular_strength = 0.45f;
                tiles.specular_min_roughness = 0.3f;
                tiles.colour = find_tile_source(hires + "earth");
                tiles.specular = find_tile_source(hires + "earth_specular");
                if (!options.no_relief)
                    tiles.normal = find_tile_source(hires + "earth_normal/textures/medres/earth-normals-hs-32k");
                if (!tiles.colour.empty())
                {
                    vis.earth_tiles = sl::PlanetTiles::create(tiles);
                    if (!vis.earth_tiles)
                        std::fprintf(stderr, "kosmos: tiled Earth unavailable: %s\n", sl::last_error().c_str());
                }
                if (vis.earth_tiles)
                {
                    const std::size_t index = static_cast<std::size_t>(home);
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
        // Weather for the other worlds that have it: a generated cloud layer on its own shell.
        vis.body_cloud_meshes.assign(sim.universe.bodies.size(), nullptr);
        vis.body_cloud_textures.assign(sim.universe.bodies.size(), nullptr);
        for (std::size_t i = 0; i < sim.universe.bodies.size(); ++i)
        {
            const kosmos::Body &b = sim.universe.bodies[i];
            if (b.cloud_cover <= 0.0 || static_cast<int>(i) == earth_id)
                continue;
            vis.body_cloud_textures[i] = fx::make_cloud_texture(2048, 1024, b.cloud_cover, 101 + static_cast<std::uint32_t>(i), 7.0);
            vis.body_cloud_meshes[i] = sl::create_sphere_mesh(static_cast<float>(b.radius + b.cloud_height), 128, 64, {255, 255, 255, 255});
        }
        loading.note("Building the cloud layers");
        if (earth_body->name != "Earth" && earth_body->cloud_cover > 0.0)
        {
            home_cloud_texture = fx::make_cloud_texture(2048, 1024, earth_body->cloud_cover, 101 + static_cast<std::uint32_t>(earth_id), 7.0);
            home_cloud_height = earth_body->cloud_height;
        }
        cloud_shell = sl::create_sphere_mesh(static_cast<float>(earth_body->radius + home_cloud_height), 256, 128, {255, 255, 255, 255});
        return true;
    };
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

    loading.note("Assembling the ship");
    Ship *ship = start_scenario(sim, options, terrain_set);
    if (!ship)
    {
        std::fprintf(stderr, "No Kestrel addon found under %s\n", options.root.c_str());
        sl::shutdown();
        return 1;
    }

    sl::Hud hud;
    if (!options.hud.empty())
    {
        sl::HudMode start_mode = sl::HudMode::off;
        if (sl::hud_parse_mode(options.hud, start_mode))
            hud.set_mode(start_mode);
        else
            std::fprintf(stderr, "unknown --hud mode '%s' (off, orbit, surface, docking)\n", options.hud.c_str());
    }
    EngineSound sound;
    kosmos::Lightning lightning;
    bool sound_on = true, first_frame = true, show_orbit = true, show_info = options.show_info && !options.hide_info, running = true, shot_saved = false, map_mode = options.map;
    MapView map;
    GalaxyView gview;
    bool galaxy_mode = options.galaxy_view;
    int jump_to = -1;
    bool jump_to_star_orbit = false;
    std::string jump_to_body;      // a body of the target system to arrive at; empty picks the best world
    std::string quantum_body;      // the planet or moon chosen in the quantum window for quantum_selected
    int quantum_bodies_for = -1;
    std::vector<std::pair<std::string, std::string>> quantum_bodies; // name, description
    bool quantum_window = options.quantum_window;
    int quantum_selected = -1;
    gview.refresh(sim.galaxy, sim.system_index());
    bool cockpit_view = options.cockpit;
    kosmos::Astronaut eva;
    kosmos::Arsenal arsenal;
    bool mouse_captured = false;
    bool pointer_free = false; // ` toggles between mouselook and a free pointer for the menus when on foot
    if (!options.target.empty())
        sim.props.set_string("/sim/target", options.target);
    apply_sets(sim.props, options.sets);
    int mfd_display = options.mfd_display;
    // In the 2D cockpit the MFDs are the dashboard by default; F5 steps dashboard, heads-up, off.
    int cockpit_mfd = 0;
    bool first_ship_seen = true;
    int panel_index = options.panel; // which of the ship's 2D panels the 2D cockpit shows
    bool mfd_mouse_prev = false;
    bool cockpit3d = options.cockpit3d;
    // Camera selection (F2): a ship's interior camera or a base camera, besides the chase and cockpit views.
    int interior_cam = -1, base_view = -1, base_view_cam = 0;
    // Cameras from the Camera editor (an orbiting external view to begin with); user_cam is the one looked through, or -1.
    const std::string user_cam_file = kosmos::user_camera_path(kosmos::Sim::user_dir());
    std::vector<kosmos::UserCamera> user_cams = kosmos::load_user_cameras(user_cam_file);
    int user_cam = options.user_camera >= 0 && options.user_camera < static_cast<int>(user_cams.size()) ? options.user_camera : -1;
    bool camera_editor_window = options.camera_editor;
    int camera_editor_pick = 0;
    bool systems_window = options.systems_window, camera_window = options.camera_window, settings_window = options.settings_window, property_window = options.property_window, lua_window = options.lua_window;
    bool net_window = options.net_window;
    // Part editor: meshes are edited in Blender while the simulation waits (the pause property is restored afterwards).
    bool part_window = options.part_window, edit_paused = false, edit_was_paused = false;
    kosmos::BlenderEdit blender_edit;
    struct PartEdit
    {
        kosmos::PartSpec *part = nullptr;
        kosmos::PartSpec original;
        bool converted = false;
        std::filesystem::path file;
        int stage = 0, index = 0;
        std::string model;
    } part_edit;
    std::string part_status;
    char blender_buffer[260] = "";
    double model_poll = 0.0;
    kosmos::NetPlay netplay;
    NetUi net_ui;
    copy_text(net_ui.nick, sizeof net_ui.nick, options.net_name.empty() ? "Pilot" : options.net_name);
    copy_text(net_ui.irc_host, sizeof net_ui.irc_host, options.net_irc);
    copy_text(net_ui.channel, sizeof net_ui.channel, options.net_channel);
    copy_text(net_ui.advertise, sizeof net_ui.advertise, options.net_advertise);
    copy_text(net_ui.game_port, sizeof net_ui.game_port, std::to_string(options.net_port));
    if (options.net_host)
        netplay.host(static_cast<std::uint16_t>(options.net_port), net_ui.nick, sim.system_index());
    else if (!options.net_join.empty())
    {
        std::string join_text = options.net_join, join_address;
        std::uint16_t join_port = kosmos::NetPlay::default_port;
        if (join_text.find(':') == std::string::npos)
            join_text += ":" + std::to_string(kosmos::NetPlay::default_port);
        if (lobby::split_endpoint(join_text, join_address, join_port))
            netplay.join(join_address, join_port, net_ui.nick, sim.system_index());
    }
    kosmos::LuaConsole lua_console;
    PropertyBrowser property_browser;
    {
        const std::string settings_file = settings_path();
        lua_console.init(sim, sim.root() + "/data/scripts", settings_file.empty() ? std::string() : (std::filesystem::path(settings_file).parent_path() / "scripts").string());
    }
    kosmos::set_stock_model_folder(sim.root() + "/data");
    if (!kosmos::Sim::user_dir().empty())
        kosmos::set_base_model_folder((std::filesystem::path(kosmos::Sim::user_dir()) / "bases").string());
    if (!options.blender.empty())
        sim.props.set_string("/sim/settings/blender", options.blender);
    int applied_volume = -1;
    if (!options.view_base.empty())
    {
        const std::size_t colon = options.view_base.find(':');
        const std::string id = options.view_base.substr(0, colon);
        for (std::size_t i = 0; i < sim.bases.size(); ++i)
            if (sim.bases[i].id == id)
                base_view = static_cast<int>(i), base_view_cam = colon == std::string::npos ? 0 : std::atoi(options.view_base.c_str() + colon + 1);
    }
    sl::gui_init();
    sl::Bitmap *mfd_target[2] = {sl::create_render_target(mfd_target_size, mfd_target_size), sl::create_render_target(mfd_target_size, mfd_target_size)};
    // The boards are minified on screen; nearest sampling made thin lines shimmer as the ship rocked.
    for (sl::Bitmap *t : mfd_target)
        if (t)
            t->smooth = true;
    sl::Bitmap *panel_target = nullptr;
    int panel_target_height = 0;
    vis.screen_quad = make_screen_quad();
    vis.cockpit_box = sl::create_cube_mesh(1.0f, {255, 255, 255, 255});
    const Ship *mfd_ship = nullptr;
    std::string mfd_ship_id;
    float look_yaw = 0.0f, look_pitch = 0.0f;
    double eva_look_yaw = 0.0, eva_look_pitch = 0.0;
    sl::dvec3 eye_shift{0.0}; // the pilot's head moved from the seat with the arrow keys and Page Up/Down, in the ship's frame
    const Ship *eye_ship = nullptr;
    if (options.map_scale > 0.0)
        map.metres_per_pixel = options.map_scale;
    if (!options.map_focus.empty())
        map.focus = sim.universe.find(options.map_focus);
    int sas_index = 0;
    // With the Earth relief option the terrain mesh (with sea glint) takes over at 300 km instead.
    // Altitude below which the terrain mesh replaces the tiled Earth sphere. Lower keeps the 64k imagery and sea glint longer, but the sphere has no relief, so peaks pop up at the swap.
    double earth_sphere_switch_m = 20.0e3;
    sl::gamepad_init();
    kosmos::Joystick joystick;
    joystick.load();
    bool joystick_window = false;
    float cam_yaw = static_cast<float>(options.cam_yaw), cam_pitch = static_cast<float>(options.cam_pitch);
    double cam_distance = options.cam_distance > 0.0 ? options.cam_distance : ship->spec().camera_distance;
    // Right-drag in the chase view turns the camera on the spot; the ship moves across the screen instead of the camera swinging round it.
    float free_yaw = 0.0f, free_pitch = 0.0f;
    int last_mouse_x = 0, last_mouse_y = 0;
    kosmos::MenuBar menu_bar;
    bool screenshot_requested = false;
    // The menu item is clicked while its menu is open, so the shot waits a moment for the menu to close.
    decltype(sl::time_ms()) screenshot_due_ms = 0;
    // Pinned open unless the menu is switched off from its View menu; --hide-info (screenshots) leaves it to slide away.
    menu_bar.keep_visible = options.menu_visible || !options.hide_info;
    menu_bar.scenario_editor_open = options.scenario_editor;
    menu_bar.aircraft_autopilot_open = options.aircraft_autopilot;
    menu_bar.spacecraft_autopilot_open = options.spacecraft_autopilot;
    menu_bar.base_editor_open = options.base_editor;
    std::vector<kosmos::MenuWindow> menu_windows = {{"Part editor", "", &part_window}, {"Cameras", "F2", &camera_window}, {"Camera editor", "", &camera_editor_window}, {"Settings", "F3", &settings_window}, {"Joystick", "", &joystick_window}, {"Ship systems", "F1", &systems_window}, {"Properties", "F4", &property_window}, {"Lua console", "F11", &lua_window}, {"Multiplayer", "F12", &net_window}, {"Quantum navigation", "", &quantum_window}};
    kosmos::MenuActions menu_actions;
    menu_actions.start_scenario = [&](const kosmos::Scenario &sc)
    {
        vis.clear_parts();
        // A scenario belongs to one star system (Sol unless it names another): go there first.
        {
            const int star = sc.system.empty() ? 0 : sim.galaxy.find(sc.system);
            if (star >= 0 && star != sim.system_index())
            {
                leave_system();
                std::string system_error;
                if (!sim.change_system(star, system_error) || !enter_system())
                {
                    std::fprintf(stderr, "kosmos: cannot go to %s: %s\n", sc.system.c_str(), system_error.c_str());
                    running = false;
                    return;
                }
                map.focus = -1;
                quantum_selected = -1;
                gview.refresh(sim.galaxy, sim.system_index());
            }
        }
        // A scenario with its own date rebuilds the system on that date; one without keeps the clock it has.
        if (sc.epoch_jd > 0.0 && std::fabs(sc.epoch_jd - sim.universe.epoch_jd) > 1.0e-9 && sim.system_index() == 0)
        {
            std::string epoch_error;
            if (!change_date(sc.epoch_jd, epoch_error))
            {
                std::fprintf(stderr, "kosmos: cannot start on that date: %s\n", epoch_error.c_str());
                running = false;
                return;
            }
        }
        if (sc.fullscreen && !sl::is_fullscreen())
            sl::set_fullscreen(true);
        if (Ship *flown = apply_scenario(sim, sc, terrain_set))
        {
            ship = flown;
            cam_distance = ship->spec().camera_distance;
            cockpit_view = cockpit3d = false;
            interior_cam = base_view = user_cam = -1;
            look_yaw = look_pitch = 0.0f;
        }
    };
    menu_actions.restart = [&]()
    {
        vis.clear_parts();
        ship = start_scenario(sim, options, terrain_set);
        eva.active = false;
        arsenal.reset();
        cam_distance = ship->spec().camera_distance;
    };
    auto terrain_of = [&](int body) -> const sl::Terrain *
    {
        for (const std::unique_ptr<BodyTerrain> &bt : body_terrains)
            if (bt->body == body)
                return bt->terrain.get();
        return nullptr;
    };
    // The ship nearest the astronaut's eye, by index, and how far away its origin is.
    auto nearest_ship = [&](double &distance) -> int
    {
        int best = -1;
        distance = 1.0e30;
        const sl::dvec3 eye = eva.eye_world(sim.universe);
        for (int i = 0; i < sim.ship_count(); ++i)
        {
            if (sim.ship(i)->spec().building())
                continue;
            const double d = glm::length(sim.ship(i)->origin() - eye);
            if (d < distance)
                distance = d, best = i;
        }
        return best;
    };
    // The door of a building is a point in its frame; this is where it is in the world.
    auto door_world = [&](const Ship &building) { return building.origin() + building.body().direction_to_world(building.spec().door); };
    // A building whose door the astronaut stands at, or -1.
    auto building_at_door = [&]() -> int
    {
        const sl::dvec3 eye = eva.eye_world(sim.universe);
        for (int i = 0; i < sim.ship_count(); ++i)
            if (sim.ship(i)->spec().building() && glm::length(door_world(*sim.ship(i)) - eye) <= sim.ship(i)->spec().door_radius + 1.7)
                return i;
        return -1;
    };
    kosmos::ArsenalContext arsenal_ctx;
    arsenal_ctx.sim = &sim;
    arsenal_ctx.eva = &eva;
    arsenal_ctx.terrain = terrain_of;
    auto board_ship = [&](bool force)
    {
        double distance = 0.0;
        const int best = nearest_ship(distance);
        if (best < 0 || (!force && distance > 30.0))
        {
            sim.log("Too far from a ship to board: go within 30 m");
            return;
        }
        eva.active = false;
        sim.activate(best);
        ship = sim.active_ship();
        cam_distance = ship->spec().camera_distance;
        look_yaw = look_pitch = 0.0f;
        sim.log("Back aboard " + ship->name);
    };
    auto toggle_eva = [&]()
    {
        if (eva.active)
        {
            const int door = building_at_door();
            if (door >= 0)
            {
                eva.active = false;
                sim.activate(door);
                ship = sim.active_ship();
                cockpit_view = ship->spec().cockpit.present;
                cockpit3d = false;
                look_yaw = look_pitch = 0.0f;
                sim.log("Inside " + ship->name);
                return;
            }
            board_ship(false);
            return;
        }
        Ship *flown = sim.active_ship();
        if (!flown)
            return;
        if (flown->spec().building())
        {
            const kosmos::Telemetry &bt = flown->telemetry();
            if (bt.soi < 0)
                return;
            const sl::dvec3 spot = door_world(*flown);
            eva.suit = kosmos::Suit();
            eva.leave(sim.universe, bt.soi, spot, flown->body().velocity, flown->origin() - spot, terrain_of(bt.soi), true);
            eva.home_ship = sim.props.get_int("/sim/active-ship");
            arsenal.equip_person(sim, flown);
            sim.props.set_bool("/autopilot/enabled", false);
            cockpit_view = cockpit3d = false;
            interior_cam = base_view = user_cam = -1;
            eva_look_yaw = eva_look_pitch = 0.0;
            sim.log("Outside " + flown->name + ": Enter at the door to go back in");
            return;
        }
        const kosmos::Telemetry &t = flown->telemetry();
        if (t.soi < 0 || t.soi >= static_cast<int>(sim.universe.bodies.size()))
            return;
        const sl::FlightBody &fb = flown->body();
        if (!t.landed && sim.universe.world.air_density_at(fb.position) > 0.01)
        {
            sim.log("Land before leaving the ship");
            return;
        }
        const sl::dvec3 origin = flown->origin();
        const sl::dvec3 centre = sim.universe.position(t.soi);
        const sl::dvec3 up = glm::normalize(origin - centre);
        const double reach = std::clamp(flown->spec().camera_distance * 0.25, 4.0, 25.0);
        sl::dvec3 side = fb.direction_to_world(sl::dvec3(-1.0, 0.0, 0.0));
        sl::dvec3 spot;
        if (t.landed)
        {
            side -= up * glm::dot(side, up);
            side = glm::length(side) > 1e-6 ? glm::normalize(side) : glm::normalize(glm::cross(sim.universe.pole(t.soi), up));
            spot = origin + side * reach;
        }
        else
        {
            side = fb.direction_to_world(sl::dvec3(0.0, 1.0, 0.0));
            spot = origin + side * (reach * 0.5);
        }
        eva.suit = kosmos::Suit();
        eva.leave(sim.universe, t.soi, spot, fb.velocity + side * (t.landed ? 0.0 : 0.3), origin - spot, terrain_of(t.soi), t.landed);
        eva.home_ship = sim.props.get_int("/sim/active-ship");
        arsenal.equip_person(sim, flown);
        sim.props.set_bool("/autopilot/enabled", false);
        sim.props.set_double("/controls/throttle", 0.0);
        sim.props.set_double("/controls/hover", 0.0);
        cockpit_view = cockpit3d = false;
        interior_cam = base_view = user_cam = -1;
        eva_look_yaw = eva_look_pitch = 0.0;
        sim.log("On foot: Enter near the ship to board");
    };
    menu_actions.toggle_eva = toggle_eva;
    menu_actions.on_foot = [&]() { return eva.active; };
    menu_actions.in_building = [&]() { return !eva.active && sim.active_ship() && sim.active_ship()->spec().building(); };
    menu_actions.show_panel = [&](int index)
    {
        const auto panels = sim.active_ship()->spec().cockpit.panels_2d();
        if (index < 0 || index >= static_cast<int>(panels.size()))
            return;
        panel_index = index;
        sim.mfd.set_unit_count(static_cast<int>(panels[static_cast<std::size_t>(index)].instruments.size()));
        for (std::size_t i = 0; i < panels[static_cast<std::size_t>(index)].instruments.size(); ++i)
            sim.mfd.set_mode(static_cast<int>(i), std::max(0, sim.mfd.mode_by_id(panels[static_cast<std::size_t>(index)].instruments[i].mode)));
        cockpit_view = true;
        cockpit3d = false;
        cockpit_mfd = 0;
        interior_cam = base_view = -1;
        map_mode = galaxy_mode = false;
        look_yaw = look_pitch = 0.0f;
    };
    menu_actions.quit = [&]() { running = false; };
    menu_actions.screenshot = [&]() { screenshot_requested = true; screenshot_due_ms = sl::time_ms() + 300; };
    // Video capture: raw frames from the screen are piped to ffmpeg, which stamps them by wall clock and encodes 30 fps H.264.
    FILE *record_pipe = nullptr, *record_pcm = nullptr;
    std::string record_file, record_video, record_pcm_file;
    long record_frames = 0;
    int record_w = 0, record_h = 0, record_rate = 48000, record_channels = 2;
    std::mutex record_audio_mutex;
    std::vector<std::int16_t> record_audio;
    auto flush_record_audio = [&]()
    {
        std::vector<std::int16_t> chunk;
        {
            std::lock_guard<std::mutex> lock(record_audio_mutex);
            chunk.swap(record_audio);
        }
        if (record_pcm && !chunk.empty())
            std::fwrite(chunk.data(), sizeof(std::int16_t), chunk.size(), record_pcm);
    };
    auto stop_recording = [&]()
    {
        if (!record_pipe)
            return;
        sl::audio_set_capture({});
        pclose(record_pipe);
        record_pipe = nullptr;
        flush_record_audio();
        bool audio = false;
        if (record_pcm)
        {
            audio = std::ftell(record_pcm) > 0;
            std::fclose(record_pcm);
            record_pcm = nullptr;
        }
        bool ok = true;
        if (audio)
        {
            const std::string mux = "ffmpeg -loglevel error -y -i \"" + record_video + "\" -f s16le -ar " + std::to_string(record_rate) +
                                    " -ac " + std::to_string(record_channels) + " -i \"" + record_pcm_file +
                                    "\" -c:v copy -c:a aac -b:a 192k -shortest -movflags +faststart \"" + record_file + "\"";
            ok = std::system(mux.c_str()) == 0;
        }
        if (!audio || !ok)
            std::rename(record_video.c_str(), record_file.c_str());
        else
            std::remove(record_video.c_str());
        std::remove(record_pcm_file.c_str());
        sim.log("Saved " + record_file + " (" + std::to_string(record_frames) + " frames" + (audio && ok ? ", with sound)" : ")"));
    };
    menu_actions.recording = [&]() { return record_pipe != nullptr; };
    menu_actions.toggle_recording = [&]()
    {
        if (record_pipe)
        {
            stop_recording();
            return;
        }
        record_w = sl::screen->width & ~1;
        record_h = sl::screen->height & ~1;
        record_file = "kosmos-" + std::to_string(sl::time_ms()) + ".mp4";
        record_video = record_file + ".video.mp4";
        record_pcm_file = record_file + ".pcm";
        const std::string command = "ffmpeg -loglevel error -y -use_wallclock_as_timestamps 1 -f rawvideo -pix_fmt rgba -s " +
                                    std::to_string(sl::screen->width) + "x" + std::to_string(sl::screen->height) +
                                    " -i - -vf \"crop=" + std::to_string(record_w) + ":" + std::to_string(record_h) +
                                    ",format=yuv420p\" -r 30 -c:v libx264 -preset veryfast -crf 20 -movflags +faststart \"" + record_video + "\"";
        record_pipe = popen(command.c_str(), "w");
        record_frames = 0;
        if (record_pipe)
        {
            record_pcm = std::fopen(record_pcm_file.c_str(), "wb");
            if (record_pcm && !sl::audio_set_capture(
                                  [&](const std::int16_t *samples, std::size_t frames)
                                  {
                                      std::lock_guard<std::mutex> lock(record_audio_mutex);
                                      record_audio.insert(record_audio.end(), samples, samples + frames * static_cast<std::size_t>(record_channels));
                                  },
                                  &record_rate, &record_channels))
            {
                std::fclose(record_pcm);
                record_pcm = nullptr;
            }
            sim.log("Recording " + record_file);
        }
        else
            sim.log("Could not start ffmpeg");
    };
    ExhaustFx exhaust_fx;
    sl::Event event_discard;
    loading.note("Settling the terrain");
    double clock = 0.0;
    double lightning_flash = 0.0;
    std::uint32_t last_time = sl::time_ms();
    // The engine's own limiter only sleeps when vsync is off, so turn it off and let simlib pace the frames.
    sl::set_vsync(false);
    int applied_fps = 0;

    while (running)
    {
        // The command-line style wins for this run without being saved.
        const int gfx_style = options.render_style >= 0 ? options.render_style : sim.props.get_int("/sim/settings/gfx-style");
        const bool gfx_clouds = sim.props.get_bool("/sim/settings/gfx-clouds") && gfx_style == 0;
        sl::set_render_style_3d(static_cast<sl::RenderStyle3D>(gfx_style));
        if (const int fps = sim.props.get_int("/sim/settings/gfx-max-fps"); fps != applied_fps)
        {
            sl::set_fps(fps);
            applied_fps = fps;
        }
        if (jump_to >= 0)
        {
            // The world stops while the next system loads; show a short hyperspace effect so the window never closes.
            const int target_system = jump_to;
            jump_to = -1;
            const std::string target_name = sim.galaxy.star(target_system).name;
            // change_system deletes every ship, so the flown ship's name is taken now.
            const std::string flown_name = ship ? ship->spec().name : std::string();
            auto warp_frame = [&](float t)
            {
                while (sl::poll_event(&event_discard))
                {
                }
                sl::clear_to_colour(sl::screen, {0, 0, 0});
                const float cx = screen_w * 0.5f, cy = screen_h * 0.5f;
                std::uint32_t seed = 12345u;
                for (int i = 0; i < 260; ++i)
                {
                    seed = seed * 1664525u + 1013904223u;
                    const float a = static_cast<float>(seed >> 8) * (6.2831853f / 16777216.0f);
                    seed = seed * 1664525u + 1013904223u;
                    const float r0 = 0.05f + 0.95f * static_cast<float>(seed >> 8) / 16777216.0f;
                    const float r1 = r0 + t * (0.15f + r0) * 1.6f, r2 = r0 + t * 0.3f * r0;
                    const int shade = static_cast<int>(std::min(1.0f, 0.3f + t) * 255.0f * (0.4f + 0.6f * r0));
                    sl::line(sl::screen, static_cast<int>(cx + std::cos(a) * r2 * screen_w * 0.5f), static_cast<int>(cy + std::sin(a) * r2 * screen_h * 0.5f),
                                  static_cast<int>(cx + std::cos(a) * r1 * screen_w * 0.5f), static_cast<int>(cy + std::sin(a) * r1 * screen_h * 0.5f), {static_cast<std::uint8_t>(shade * 0.7f), static_cast<std::uint8_t>(shade * 0.85f), static_cast<std::uint8_t>(shade)});
                }
                const int fade = static_cast<int>(255.0f * std::clamp((t - 0.7f) / 0.3f, 0.0f, 1.0f));
                if (fade > 0)
                    sl::rectfill(sl::screen, 0, 0, screen_w, screen_h, {static_cast<std::uint8_t>(fade), static_cast<std::uint8_t>(fade), static_cast<std::uint8_t>(fade)});
                sl::gprintf_center(screen_h / 2 - 8, {200, 220, 255}, "Quantum jump to %s", target_name.c_str());
                sl::show_video_bitmap();
                sl::end_frame();
            };
            const std::uint32_t begin = sl::time_ms();
            warp_frame(0.0f);
            if (netplay.joined())
                netplay.leave();
            vis.clear_parts();
            leave_system();
            std::string jump_error;
            bool loaded = sim.change_system(target_system, jump_error) && enter_system();
            if (!loaded)
            {
                std::fprintf(stderr, "kosmos: quantum jump failed: %s\n", jump_error.c_str());
                break;
            }
            options.scenario.clear();
            options.orbit_altitude = 0.0;
            options.orbit_body = jump_to_star_orbit ? "auto" : "";
            if (jump_to_star_orbit && !jump_to_body.empty())
            {
                const int chosen = sim.universe.find(jump_to_body);
                if (chosen >= 0)
                {
                    options.orbit_body = jump_to_body;
                    options.orbit_altitude = std::min(200.0, sim.universe.bodies[static_cast<std::size_t>(chosen)].radius * 0.5 / 1000.0);
                }
            }
            jump_to_body.clear();
            options.orbit_start = !jump_to_star_orbit;
            if (jump_to_star_orbit && !flown_name.empty())
                options.ship_name = flown_name;
            ship = start_scenario(sim, options, terrain_set);
            eva.active = false;
            arsenal.reset();
            if (!ship)
                break;
            cam_distance = ship->spec().camera_distance;
            cockpit_view = cockpit3d = false;
            interior_cam = base_view = -1;
            look_yaw = look_pitch = 0.0f;
            map.focus = -1;
            quantum_selected = -1;
            quantum_body.clear();
            galaxy_mode = false;
            exhaust_fx = ExhaustFx();
            gview.refresh(sim.galaxy, sim.system_index());
            // Hold the effect for at least a couple of seconds, fading to black before the new sky appears.
            for (std::uint32_t t = sl::time_ms() - begin; t < 2500; t = sl::time_ms() - begin)
                warp_frame(static_cast<float>(t) / 2500.0f);
            last_time = sl::time_ms();
        }
        // The setting is a property, so the settings window, the F4 browser and scripts all change it the same way.
        if (sim.props.get_int("/sim/settings/world-texture") != generated_texture_width)
        {
            generated_texture_width = sim.props.get_int("/sim/settings/world-texture");
            reload_world_textures();
            last_time = sl::time_ms();
        }
        if (!options.test_scenario.empty() && clock > 2.0)
        {
            if (const kosmos::Scenario *next = sim.find_scenario(options.test_scenario))
            {
                const kosmos::Scenario chosen = *next;
                options.scenario = chosen.id;
                std::fprintf(stderr, "[test] switching to %s\n", chosen.id.c_str());
                menu_actions.start_scenario(chosen);
            }
            options.test_scenario.clear();
        }
        if (options.enter_building.empty() && clock > 1.0)
            options.enter_building = sim.props.get_string("/sim/enter-building");
        if (!options.enter_building.empty() && clock > 1.0)
        {
            sim.props.set_string("/sim/enter-building", "");
            for (int i = 0; i < sim.ship_count(); ++i)
                if (sim.ship(i)->spec().building() && sim.ship(i)->name.find(options.enter_building) != std::string::npos)
                {
                    sim.activate(i);
                    ship = sim.active_ship();
                    cockpit_view = true;
                    cockpit3d = options.cockpit3d;
                    break;
                }
            options.enter_building.clear();
        }
        if (options.eva && clock > 1.0)
        {
            options.eva = false;
            if (!eva.active)
                toggle_eva();
        }
        if (options.test_jump >= 0 && clock > 1.0)
        {
            jump_to = options.test_jump;
            jump_to_star_orbit = true;
            jump_to_body = options.test_jump_body;
            options.test_jump = -1;
        }
        const std::uint32_t now = sl::time_ms();
        const double frame = std::clamp(static_cast<double>(now - last_time) * 0.001, 0.001, 0.1);
        last_time = now;
        clock += frame;
        kosmos::Universe &universe = sim.universe;
        sl::PropertyTree &props = sim.props;

        sl::Event event;
        while (sl::poll_event(&event))
        {
            sl::gui_handle_event(event);
            sl::gamepad_handle_event(event);
            if (event.type() == sl::Event::Type::quit)
                running = false;
            else if (event.type() == sl::Event::Type::key_down && sl::gui_available() && ImGui::GetIO().WantTextInput)
                continue;
            else if (event.type() == sl::Event::Type::key_down)
            {
                const auto key = event.key();
                if (key == sl::Event::Key::f2)
                    camera_window = !camera_window;
                else if (key == sl::Event::Key::f3)
                    settings_window = !settings_window;
                else if (key == sl::Event::Key::f4)
                    property_window = !property_window;
                else if (key == sl::Event::Key::f1)
                    systems_window = !systems_window;
                else if (key == sl::Event::Key::backquote)
                    pointer_free = !pointer_free;
                else if (key == sl::Event::Key::f11)
                    lua_window = !lua_window;
                else if (key == sl::Event::Key::f12)
                    net_window = !net_window;
                else if (key == sl::Event::Key::escape)
                {
                    if (!menu_bar.dismiss(menu_windows))
                        running = false;
                }
                else if (key == sl::Event::Key::letter_r)
                {
                    vis.clear_parts();
                    ship = start_scenario(sim, options, terrain_set);
                    eva.active = false;
                    arsenal.reset();
                    cam_distance = ship->spec().camera_distance;
                }
                else if (eva.active && !map_mode && key >= sl::Event::Key::digit_1 && key <= sl::Event::Key::digit_9)
                    arsenal.select(static_cast<int>(key) - static_cast<int>(sl::Event::Key::digit_1));
                else if (!eva.active && !map_mode && key == sl::Event::Key::letter_k)
                {
                    Ship *flown = sim.active_ship();
                    int index = -1;
                    // Shift releases from the last loaded hardpoint (the special store) instead of the first.
                    if (flown && (sl::key_down(sl::Key::left_shift) || sl::key_down(sl::Key::right_shift)))
                        for (int i = static_cast<int>(flown->spec().hardpoints.size()) - 1; i >= 0 && index < 0; --i)
                            if (arsenal.rack_count(*flown, static_cast<std::size_t>(i)) > 0)
                                index = i;
                    if (flown && !arsenal.release(arsenal_ctx, *flown, index))
                        sim.log("Nothing to release");
                }
                else if (eva.active && (key == sl::Event::Key::letter_z || key == sl::Event::Key::letter_x || key == sl::Event::Key::space || key == sl::Event::Key::letter_p ||
                                        key == sl::Event::Key::letter_t || key == sl::Event::Key::letter_g || key == sl::Event::Key::letter_l || key == sl::Event::Key::letter_u ||
                                        key == sl::Event::Key::keypad_0 || key == sl::Event::Key::keypad_period || key == sl::Event::Key::letter_c || key == sl::Event::Key::letter_v ||
                                        key == sl::Event::Key::letter_h || key == sl::Event::Key::letter_j))
                {
                    // On foot these ship keys do nothing; Space jumps (Ctrl crouches) and is read as a held key.
                }
                else if (eva.active && !map_mode && key == sl::Event::Key::letter_f)
                {
                    if (eva.suit.jetpack)
                    {
                        eva.pack = !eva.pack;
                        sim.log(eva.pack ? "Manoeuvring pack on" : "Manoeuvring pack off");
                    }
                }
                else if (key == sl::Event::Key::letter_z)
                {
                    props.set_bool("/autopilot/enabled", false);
                    props.set_double("/controls/throttle", 1.0);
                }
                else if (key == sl::Event::Key::letter_x)
                {
                    props.set_bool("/autopilot/enabled", false);
                    props.set_double("/controls/throttle", 0.0);
                }
                else if (key == sl::Event::Key::keypad_0)
                    props.set_double("/controls/hover", 1.0);
                else if (key == sl::Event::Key::keypad_period)
                    props.set_double("/controls/hover", 0.0);
                else if (key == sl::Event::Key::space)
                    props.set_bool("/controls/stage", true);
                else if (key == sl::Event::Key::letter_p)
                {
                    const bool enable = !props.get_bool("/autopilot/enabled");
                    props.set_bool("/autopilot/enabled", enable);
                    if (!enable)
                        props.set_string("/autopilot/status", "");
                }
                else if (key == sl::Event::Key::letter_t)
                {
                    props.set_bool("/autopilot/enabled", false);
                    sas_index = (sas_index + 1) % sas_mode_count;
                    props.set_string("/controls/sas", sas_modes[sas_index]);
                }
                else if (key == sl::Event::Key::f10 && !sim.scenarios().empty())
                {
                    // Load the next scenario.
                    const std::vector<kosmos::Scenario> &all = sim.scenarios();
                    std::size_t next = 0;
                    for (std::size_t i = 0; i < all.size(); ++i)
                        if (!options.scenario.empty() && sim.find_scenario(options.scenario) == &all[i])
                            next = (i + 1) % all.size();
                    options.scenario = all[next].id;
                    const kosmos::Scenario chosen = all[next];
                    menu_actions.start_scenario(chosen);
                }
                else if (key == sl::Event::Key::f5)
                {
                    const bool on_dashboard = cockpit_view && !cockpit3d && ship->spec().cockpit.present && ship->spec().cockpit.mfds &&
                                              !map_mode && interior_cam < 0 && base_view < 0;
                    if (on_dashboard)
                        cockpit_mfd = (cockpit_mfd + 1) % 3;
                    else
                        mfd_display = (mfd_display + 1) % 3;
                }
                else if (key == sl::Event::Key::f6 || key == sl::Event::Key::f7)
                {
                    const int side = key == sl::Event::Key::f6 ? 0 : 1;
                    const int count = static_cast<int>(sim.mfd.modes().size());
                    if (count > 0)
                        sim.mfd.set_mode(side, (sim.mfd.unit(side).mode + 1) % count);
                }
                else if (key == sl::Event::Key::f8 || key == sl::Event::Key::f9)
                    sim.mfd.press(key == sl::Event::Key::f8 ? 0 : 1, 12);
                else if ((key == sl::Event::Key::letter_g || key == sl::Event::Key::letter_l) && ship->spec().retractable_gear)
                {
                    const bool down = props.get_double("/controls/gear") > 0.5;
                    props.set_double("/controls/gear", down ? 0.0 : 1.0);
                    sound.play(down ? "gear_up" : "gear_down", 200);
                }
                else if (key == sl::Event::Key::letter_u)
                    props.set_bool("/dock/undock", true);
                else if (key == sl::Event::Key::letter_o)
                    props.set_bool("/sim/settings/orbit-line", !props.get_bool("/sim/settings/orbit-line"));
                else if (key == sl::Event::Key::tab)
                    map_mode = !map_mode;
                else if (key == sl::Event::Key::letter_n)
                    galaxy_mode = !galaxy_mode;
                else if ((key == sl::Event::Key::return_key || key == sl::Event::Key::keypad_enter) && (sl::key_down(sl::Key::left_alt) || sl::key_down(sl::Key::right_alt)))
                    sl::toggle_fullscreen();
                else if (!galaxy_mode && !map_mode && (key == sl::Event::Key::return_key || key == sl::Event::Key::keypad_enter))
                    toggle_eva();
                else if (galaxy_mode && key == sl::Event::Key::arrow_right)
                    gview.step_selection(1);
                else if (galaxy_mode && key == sl::Event::Key::arrow_left)
                    gview.step_selection(-1);
                else if (galaxy_mode && (key == sl::Event::Key::return_key || key == sl::Event::Key::keypad_enter) && gview.selected >= 0 && gview.selected != sim.system_index())
                {
                    jump_to = gview.selected;
                    jump_to_star_orbit = false;
                }
                else if (key == sl::Event::Key::letter_c)
                {
                    free_yaw = free_pitch = 0.0f;
                    // C cycles chase view, 2D cockpit, (for ships with MFDs) the 3D cockpit, then each camera from the Camera editor.
                    interior_cam = base_view = -1;
                    if (user_cam >= 0)
                    {
                        if (++user_cam >= static_cast<int>(user_cams.size()))
                            user_cam = -1;
                    }
                    else if (!cockpit_view)
                        cockpit_view = true, cockpit3d = false;
                    else if (!cockpit3d && ship->spec().cockpit.mfds)
                        cockpit3d = true;
                    else
                    {
                        cockpit_view = cockpit3d = false;
                        if (!user_cams.empty())
                            user_cam = 0;
                    }
                    look_yaw = look_pitch = 0.0f;
                }
                else if (key == sl::Event::Key::letter_v && sim.ship_count() > 1)
                {
                    props.set_bool("/autopilot/enabled", false);
                    props.set_string("/autopilot/status", "");
                    int next = props.get_int("/sim/active-ship");
                    for (int tries = 0; tries < sim.ship_count(); ++tries)
                    {
                        next = (next + 1) % sim.ship_count();
                        if (!sim.ship(next)->spec().building())
                            break;
                    }
                    sim.activate(next);
                    ship = sim.active_ship();
                    props.set_double("/controls/throttle", 0.0);
                    props.set_double("/controls/hover", 0.0);
                    props.set_string("/controls/sas", "stability");
                    sas_index = 0;
                    cam_distance = ship->spec().camera_distance;
                    look_yaw = look_pitch = 0.0f;
                    interior_cam = -1;
                }
                else if (key == sl::Event::Key::letter_f && map_mode)
                {
                    do
                        map.focus = map.focus + 1 >= static_cast<int>(universe.bodies.size()) ? -1 : map.focus + 1;
                    while (map.focus >= 0 && universe.bodies[static_cast<std::size_t>(map.focus)].minor);
                    map.metres_per_pixel = map.focus < 0 ? std::min(map.metres_per_pixel, 1.0e5) : std::max(universe.bodies[static_cast<std::size_t>(map.focus)].radius * 0.02, 1.0);
                }
                else if (key == sl::Event::Key::letter_f && !map_mode)
                    props.set_double("/controls/flaps", std::fmod(props.get_double("/controls/flaps") + 0.34, 1.02));
                else if (key == sl::Event::Key::letter_i)
                    show_info = !show_info;
                else if (key == sl::Event::Key::letter_h)
                {
                    if (sl::key_down(sl::Key::left_shift) || sl::key_down(sl::Key::right_shift))
                        hud.next_colour();
                    else
                        hud.set_mode(next_hud_mode(hud.mode(), ship->spec().hud_modes()));
                    if (!(cockpit_view || interior_cam >= 0))
                        sim.log("HUD is shown in cockpit view (press C)");
                }
                else if (key == sl::Event::Key::letter_j)
                {
                    // Jump to the closest other player.
                    const kosmos::Ghost *best = nullptr;
                    double best_d = 0.0;
                    for (const kosmos::Ghost &g : netplay.ghosts())
                    {
                        if (!g.have_state || g.age > 3.0 || g.state.soi < 0 || g.state.soi >= static_cast<int>(universe.bodies.size()))
                            continue;
                        const double d = glm::length(universe.position(g.state.soi) + g.shown_position() - ship->body().position);
                        if (!best || d < best_d)
                        {
                            best = &g;
                            best_d = d;
                        }
                    }
                    if (best && snap_to_ghost(sim, *ship, *best, terrain_set))
                        sim.log("Moved next to " + best->nick);
                }
                else if (key == sl::Event::Key::letter_m)
                    props.set_bool("/sim/settings/sound", !props.get_bool("/sim/settings/sound"));
                else if (key == sl::Event::Key::period)
                    props.set_int("/sim/warp-index", std::min(props.get_int("/sim/warp-index") + 1, Sim::warp_levels - 1));
                else if (key == sl::Event::Key::comma)
                    props.set_int("/sim/warp-index", std::max(props.get_int("/sim/warp-index") - 1, 0));
            }
            sl::display_handle_event(event);
        }

        // Follow the window: resizing or going fullscreen resizes the 3D surface to match.
        if (sl::screen_width() > 0 && sl::screen_height() > 0 && (sl::screen_width() != screen_w || sl::screen_height() != screen_h))
        {
            screen_w = sl::screen_width();
            screen_h = sl::screen_height();
        }
        if (screen_w != surface_w || screen_h != surface_h)
        {
            if (sl::Surface3D *resized = sl::create_surface_3d(screen_w, screen_h, 1))
            {
                sl::destroy_surface_3d(surface);
                surface = resized;
                surface_w = screen_w;
                surface_h = screen_h;
            }
        }

        // M and O keep their own flags; the settings (and the F4 browser) share them through the tree.
        sound_on = sim.props.get_bool("/sim/settings/sound");
        show_orbit = sim.props.get_bool("/sim/settings/orbit-line");
        {
            const int volume = std::clamp(sim.props.get_int("/sim/settings/volume"), 0, 100);
            if (volume != applied_volume)
            {
                applied_volume = volume;
                sl::audio_fx_set_volume(volume * 255 / 100);
            }
        }
        const bool typing = sl::gui_available() && ImGui::GetIO().WantTextInput;

        // Keyboard controls write the property tree; scripts and the cockpit use the same paths.
        const double throttle_rate = 0.5 * frame;
        const double throttle = props.get_double("/controls/throttle");
        const bool on_foot = eva.active;
        const bool eva_moving = on_foot && !typing &&
                                (options.eva_walk || sl::key_down(sl::Key::letter_w) || sl::key_down(sl::Key::letter_a) || sl::key_down(sl::Key::letter_s) || sl::key_down(sl::Key::letter_d) ||
                                 sl::key_down(sl::Key::left_control) || sl::key_down(sl::Key::right_control) || sl::key_down(sl::Key::space));
        if (!typing && !on_foot && sl::key_down(sl::Key::left_shift))
            props.set_double("/controls/throttle", std::min(1.0, throttle + throttle_rate));
        if (!typing && !on_foot && sl::key_down(sl::Key::left_control))
        {
            props.set_bool("/autopilot/enabled", false);
            props.set_double("/controls/throttle", std::max(0.0, throttle - throttle_rate));
        }
        const double hover = props.get_double("/controls/hover");
        if (!typing && !on_foot && sl::key_down(sl::Key::keypad_plus))
            props.set_double("/controls/hover", std::min(1.0, hover + throttle_rate));
        if (!typing && !on_foot && sl::key_down(sl::Key::keypad_minus))
            props.set_double("/controls/hover", std::max(0.0, hover - throttle_rate));
        if (!typing && !on_foot && ship->spec().has_pods())
        {
            // Engine pods: keypad 9 and 3 set their thrust, 8 and 2 swing them (forward and back), 4, 5 and 6 jump to retro, hover and forward.
            auto bump = [&](const char *path, double d) { props.set_double(path, std::clamp(props.get_double(path) + d, 0.0, 1.0)); };
            const double pod_step = throttle_rate * (sl::key_down(sl::Key::keypad_9) ? 1.0 : sl::key_down(sl::Key::keypad_3) ? -1.0 : 0.0);
            const double tilt_step = frame * 0.4 * (sl::key_down(sl::Key::keypad_8) ? 1.0 : sl::key_down(sl::Key::keypad_2) ? -1.0 : 0.0);
            for (const char *side : {"left", "right"})
            {
                bump((std::string("/controls/pod-") + side).c_str(), pod_step);
                bump((std::string("/controls/pod-tilt-") + side).c_str(), tilt_step);
            }
            const int preset = sl::key_down(sl::Key::keypad_4) ? 0 : sl::key_down(sl::Key::keypad_5) ? 1 : sl::key_down(sl::Key::keypad_6) ? 2 : -1;
            if (preset >= 0)
            {
                props.set_double("/controls/pod-tilt-left", preset * 0.5);
                props.set_double("/controls/pod-tilt-right", preset * 0.5);
            }
        }
        for (; options.shot_stages > 0; --options.shot_stages)
            ship->stage_once();
        if (options.shot_throttle >= 0.0)
            props.set_double("/controls/throttle", options.shot_throttle);
        auto axis = [typing, on_foot](sl::Key plus, sl::Key minus) { return typing || on_foot ? 0.0 : (sl::key_down(plus) ? 1.0 : 0.0) - (sl::key_down(minus) ? 1.0 : 0.0); };
        props.set_double("/controls/pitch", axis(sl::Key::letter_s, sl::Key::letter_w));
        props.set_double("/controls/yaw", axis(sl::Key::letter_a, sl::Key::letter_d));
        props.set_double("/controls/roll", axis(sl::Key::letter_q, sl::Key::letter_e));
        props.set_double("/controls/brake", !typing && !on_foot && sl::key_down(sl::Key::letter_b) ? 1.0 : 0.0);
        // In the cockpit the arrow keys move the head instead of firing RCS translation.
        const bool head_keys = !typing && !on_foot && cockpit_view && interior_cam < 0 && base_view < 0 && !galaxy_mode && !map_mode;
        if (eye_ship != ship || !cockpit_view)
        {
            eye_ship = ship;
            eye_shift = sl::dvec3(0.0);
        }
        if (head_keys)
        {
            const double step = 0.6 * frame;
            eye_shift.z -= axis(sl::Key::arrow_up, sl::Key::arrow_down) * step;
            eye_shift.x += axis(sl::Key::arrow_right, sl::Key::arrow_left) * step;
            eye_shift.y += axis(sl::Key::page_up, sl::Key::page_down) * step;
            eye_shift = glm::clamp(eye_shift, sl::dvec3(-0.8), sl::dvec3(0.8));
        }
        if (const std::string request = props.get_string("/sim/hud-request"); !request.empty())
        {
            sl::HudMode requested = sl::HudMode::off;
            if (request == "off" || sl::hud_parse_mode(request, requested))
                hud.set_mode(requested);
            props.set_string("/sim/hud-request", "");
        }
        props.set_double("/controls/rcs-x", head_keys ? 0.0 : axis(sl::Key::arrow_right, sl::Key::arrow_left));
        props.set_double("/controls/rcs-y", head_keys ? 0.0 : axis(sl::Key::arrow_up, sl::Key::arrow_down));
        if (joystick.learning())
            joystick.poll_learn();
        else if (!typing && !on_foot)
        {
            const kosmos::JoyResult joy = joystick.update(ship->spec().aircraft());
            for (const auto &[action, value] : joy.axes)
            {
                const std::string path = "/controls/" + action;
                props.set_double(path, std::clamp(props.get_double(path) + static_cast<double>(value), -1.0, 1.0));
            }
            for (const auto &[action, value] : joy.levels)
            {
                if (action == "throttle")
                    props.set_bool("/autopilot/enabled", false);
                props.set_double("/controls/" + action, value);
            }
            if (joy.brake)
                props.set_double("/controls/brake", 1.0);
            for (const std::string &action : joy.pressed)
            {
                if (action == "gear" && ship->spec().retractable_gear)
                {
                    const bool down = props.get_double("/controls/gear") > 0.5;
                    props.set_double("/controls/gear", down ? 0.0 : 1.0);
                    sound.play(down ? "gear_up" : "gear_down", 200);
                }
                else if (action == "flaps")
                    props.set_double("/controls/flaps", std::fmod(props.get_double("/controls/flaps") + 0.34, 1.02));
                else if (action == "stage")
                    props.set_bool("/controls/stage", true);
                else if (action == "autopilot")
                {
                    const bool enable = !props.get_bool("/autopilot/enabled");
                    props.set_bool("/autopilot/enabled", enable);
                    if (!enable)
                        props.set_string("/autopilot/status", "");
                }
                else if (action == "sas")
                {
                    props.set_bool("/autopilot/enabled", false);
                    sas_index = (sas_index + 1) % sas_mode_count;
                    props.set_string("/controls/sas", sas_modes[sas_index]);
                }
                else if (action == "throttle-zero" || action == "throttle-full")
                {
                    props.set_bool("/autopilot/enabled", false);
                    props.set_double("/controls/throttle", action == "throttle-full" ? 1.0 : 0.0);
                }
                else if (action == "hover-on" || action == "hover-off")
                    props.set_double("/controls/hover", action == "hover-on" ? 1.0 : 0.0);
            }
        }
        if (props.get_bool("/autopilot/enabled") && props.get_double("/controls/pitch") + props.get_double("/controls/yaw") + props.get_double("/controls/roll") != 0.0)
            props.set_bool("/autopilot/enabled", false);

        // The shot option can skip ahead in time on rails so a screenshot of an orbit needs no waiting.
        if (options.shot_time > 0.0 && universe.time() < options.shot_time && ship->can_rails(Controls{}))
            props.set_int("/sim/warp-index", Sim::warp_levels - 1);

        // Time warp is held at 1x while others share the sky.
        netplay.update(frame);
        netplay.lobby_update(net_ui.advertise, net_ui.title);
        if (netplay.shared())
            props.set_int("/sim/warp-index", 0);
        // Standing still on the ground you can warp time; moving or floating holds it at 1x.
        else if (eva.active && (eva_moving || !eva.grounded || eva.speed > 0.3) && props.get_int("/sim/warp-index") > 0)
            props.set_int("/sim/warp-index", 0);

        if (edit_paused && !blender_edit.active())
        {
            edit_paused = false;
            props.set_bool("/sim/paused", edit_was_paused);
            props.set_bool("/sim/blender-edit", false);
            part_status = "Blender closed; simulation resumed";
        }
        model_poll += frame;
        if (model_poll > 0.5)
        {
            model_poll = 0.0;
            if (vis.reload_models() > 0 && edit_paused)
                part_status = "Reloaded the edited mesh";
        }
        sim.step(frame);
        {
            const bool storms = sim.props.get_bool("/sim/settings/weather", true) && sim.props.get_bool("/sim/settings/weather-rain", true);
            const double thunder = storms && sim.current_warp() <= 4.0 ? sim.props.get_double("/environment/thunder") : 0.0;
            lightning.update(frame, thunder, sim.props.get_bool("/sim/settings/sound") ? 0.9 : 0.0);
        }
        lua_console.tick(frame);
        ship = sim.active_ship();
        if (eva.active)
        {
            kosmos::EvaInput in;
            double yaw = eva_look_yaw, pitch = eva_look_pitch;
            eva_look_yaw = eva_look_pitch = 0.0;
            if (!typing)
            {
                auto held = [](sl::Key k) { return sl::key_down(k) ? 1.0 : 0.0; };
                in.forward = held(sl::Key::letter_w) - held(sl::Key::letter_s);
                in.right = held(sl::Key::letter_d) - held(sl::Key::letter_a);
                const bool ctrl = sl::key_down(sl::Key::left_control) || sl::key_down(sl::Key::right_control);
                const bool space = sl::key_down(sl::Key::space);
                in.jump = space;
                in.crouch = ctrl;
                in.up = (space ? 1.0 : 0.0) - (ctrl ? 1.0 : 0.0);
                in.run = sl::key_down(sl::Key::left_shift) || sl::key_down(sl::Key::right_shift);
                yaw += (held(sl::Key::arrow_left) - held(sl::Key::arrow_right)) * 1.8 * frame;
                pitch += (held(sl::Key::arrow_up) - held(sl::Key::arrow_down)) * 1.2 * frame;
                in.roll = (held(sl::Key::letter_q) - held(sl::Key::letter_e)) * 1.2 * frame;
            }
            if (options.eva_walk)
            {
                in.forward = 1.0;
                in.run = true;
                in.jump = std::fmod(clock, 3.0) < 0.15;
                static double next_report = 0.0;
                if (clock > next_report)
                {
                    next_report = clock + 1.0;
                    std::fprintf(stderr, "[eva] t=%.1f height=%.2f speed=%.2f grounded=%d upright=%d\n", clock, eva.height, eva.speed, eva.grounded, eva.upright);
                }
            }
            in.yaw = yaw;
            in.pitch = pitch;
            const kosmos::Astronaut::Solids solids = [&](const sl::dvec3 &from, const sl::dvec3 &to, double height)
            {
                kosmos::SolidScene scene;
                scene.universe = &sim.universe;
                scene.body = eva.body;
                scene.bases = &sim.bases;
                for (int i = 0; i < sim.ship_count(); ++i)
                    scene.ships.push_back(sim.ship(i));
                for (const std::unique_ptr<BodyTerrain> &bt : body_terrains)
                    if (bt->body == eva.body)
                        scene.clutter = bt->clutter.get();
                return kosmos::push_out_of_solids(scene, from, to, height);
            };
            eva.step(sim.universe, terrain_of(eva.body), in, frame, solids);
            const double warp_now = sim.current_warp();
            if (warp_now > 1.0)
                eva.carry(sim.universe, terrain_of(eva.body), frame * (warp_now - 1.0));
            double ship_distance = 0.0;
            const int near_ship = nearest_ship(ship_distance);
            bool at_base = false;
            const kosmos::Body &eva_body = sim.universe.bodies[static_cast<std::size_t>(eva.body)];
            for (const kosmos::Base &b : sim.bases)
                if (b.body == eva_body.name && eva.grounded)
                    at_base = at_base || std::acos(std::clamp(glm::dot(kosmos::base_direction(sim.universe, eva.body, b), eva.local_up()), -1.0, 1.0)) * eva_body.radius < 150.0;
            if (near_ship >= 0 && ship_distance < 30.0 || at_base)
                eva.recharge(frame * std::max(1.0, warp_now));
            {
                static bool trigger_was_down = false;
                const bool trigger = !typing && mouse_captured && sl::mouse_button_down(sl::MouseButton::left);
                arsenal.use_held(arsenal_ctx, trigger && !trigger_was_down, trigger);
                trigger_was_down = trigger;
            }
            if (eva.suit.health <= 0.0)
            {
                sim.log("You were killed: revived aboard your ship");
                board_ship(true);
                eva.suit.health = 1.0;
                ship = sim.active_ship();
            }
            if (eva.suit.failed)
            {
                sim.log("Suit failure: the crew pulled you back aboard");
                board_ship(true);
                ship = sim.active_ship();
            }
        }
        if (options.selftest_drop >= 0.0 && clock >= options.selftest_drop && ship)
        {
            options.selftest_drop = -1.0;
            arsenal.release(arsenal_ctx, *ship, options.selftest_drop_atom ? static_cast<int>(ship->spec().hardpoints.size()) - 1 : -1);
        }
        arsenal.set_sound(sound_on);
        arsenal.update(arsenal_ctx, frame * std::min(sim.current_warp(), 4.0), frame);
        if (ship != mfd_ship || ship->spec().name != mfd_ship_id)
        {
            // Each ship brings its own default instruments whenever it becomes the flown one.
            mfd_ship = ship;
            mfd_ship_id = ship->spec().name;
            panel_index = first_ship_seen ? options.panel : 0;
            first_ship_seen = false;
            const auto &cockpit_spec = ship->spec().cockpit;
            sim.mfd.set_domain(ship->spec().mfd_domain());
            if (panel_index > 0 && panel_index < static_cast<int>(cockpit_spec.panels_2d().size()))
                menu_actions.show_panel(panel_index);
            else
            {
                panel_index = 0;
                sim.mfd.set_unit_count(static_cast<int>(cockpit_spec.instruments.size()));
                for (std::size_t i = 0; i < cockpit_spec.instruments.size(); ++i)
                    sim.mfd.set_mode(static_cast<int>(i), std::max(0, sim.mfd.mode_by_id(cockpit_spec.instruments[i].mode)));
                // A default the ship's regime hides falls back to the surface display.
                const auto usable = [&](const std::string &id) { return sim.mfd.allowed(id) ? id : std::string("surface"); };
                if (cockpit_spec.instruments.empty() || !options.mfd_left.empty())
                    sim.mfd.set_mode(0, std::max(0, sim.mfd.mode_by_id(usable(options.mfd_left.empty() ? cockpit_spec.mfd_left : options.mfd_left))));
                if (cockpit_spec.instruments.empty() || !options.mfd_right.empty())
                    sim.mfd.set_mode(1, std::max(0, sim.mfd.mode_by_id(usable(options.mfd_right.empty() ? cockpit_spec.mfd_right : options.mfd_right))));
                for (int side = 0; side < 2 && side < sim.mfd.unit_count(); ++side)
                {
                    const std::string key = side == 0 ? "/sim/scenario-mfd-left" : "/sim/scenario-mfd-right";
                    const std::string wanted = props.get_string(key);
                    if (!wanted.empty() && sim.mfd.allowed(wanted))
                        sim.mfd.set_mode(side, std::max(0, sim.mfd.mode_by_id(wanted)));
                }
            }
        }
        const kosmos::Telemetry &tm = ship->telemetry();
        if (netplay.joined())
        {
            kosmos::GhostState mine;
            const sl::FlightBody &nb = ship->body();
            mine.ship = ship->spec().name;
            mine.soi = tm.soi;
            mine.stage = static_cast<std::uint8_t>(std::clamp(ship->active_stage(), 0, 255));
            mine.throttle = static_cast<float>(props.get_double("/controls/throttle"));
            mine.engine = tm.thrust > 0.0;
            mine.position = nb.position - universe.position(tm.soi);
            mine.velocity = nb.velocity - universe.world.body(tm.soi)->velocity;
            mine.orientation = nb.orientation;
            netplay.publish(mine);
        }
        sl::FlightBody &fb = ship->body();

        if (interior_cam >= static_cast<int>(ship->spec().cameras.size()))
            interior_cam = -1;
        if (base_view >= static_cast<int>(sim.bases.size()) || (base_view >= 0 && base_view_cam >= static_cast<int>(sim.bases[static_cast<std::size_t>(base_view)].cameras.size())))
            base_view = -1;
        if (user_cam >= static_cast<int>(user_cams.size()))
            user_cam = -1;
        const bool in_eva = eva.active && !map_mode && !galaxy_mode;
        const bool in_user = user_cam >= 0 && !map_mode && !galaxy_mode && !in_eva;
        const bool in_base_cam = base_view >= 0 && !map_mode && !galaxy_mode && !in_user && !in_eva;
        const bool in_interior = interior_cam >= 0 && !map_mode && !in_base_cam && !in_user && !in_eva;
        const bool in_cockpit = cockpit_view && ship->spec().cockpit.present && !map_mode && !in_interior && !in_base_cam && !in_user && !in_eva;
        const bool in_cockpit3d = in_cockpit && cockpit3d && ship->spec().cockpit.mfds;
        const int mx = sl::mouse_x(), my = sl::mouse_y();
        const int dx = mx - last_mouse_x, dy = my - last_mouse_y;
        const bool dragging = sl::mouse_button_down(sl::MouseButton::right) && !(sl::gui_available() && ImGui::GetIO().WantCaptureMouse);
        last_mouse_x = mx;
        last_mouse_y = my;
        {
            // On foot the mouse steers the view like a first-person game; the ` key, Alt, or any open window gives the pointer back.
            const bool windows_open = camera_window || settings_window || property_window || lua_window || net_window || systems_window || camera_editor_window || part_window || joystick_window || quantum_window;
            const bool free_pointer = windows_open || pointer_free || sl::key_down(sl::Key::left_alt) || sl::key_down(sl::Key::right_alt) || (sl::gui_available() && ImGui::GetIO().WantTextInput);
            const bool capture = in_eva && options.shot.empty() && !free_pointer;
            if (capture != mouse_captured)
            {
                sl::set_relative_mouse_mode(capture);
                mouse_captured = capture;
            }
        }
        const double zoom_in = sl::key_down(sl::Key::left_bracket) ? 1.0 / (1.0 + 2.0 * frame) : 1.0;
        const double zoom_out = sl::key_down(sl::Key::right_bracket) ? 1.0 + 2.0 * frame : 1.0;
        if (galaxy_mode)
            gview.ly_per_pixel = std::clamp(gview.ly_per_pixel * zoom_in * zoom_out, 0.02, 400.0);
        else if (map_mode)
        {
            if (dragging)
            {
                map.yaw -= static_cast<float>(dx) * 0.008f;
                map.pitch = std::clamp(map.pitch + static_cast<float>(dy) * 0.008f, 0.0f, 1.5708f);
            }
            map.metres_per_pixel = std::clamp(map.metres_per_pixel * zoom_in * zoom_out, 1.0, 1.0e12);
        }
        else if (in_base_cam)
        {
        }
        else if (in_eva)
        {
            if (mouse_captured)
            {
                int rx = 0, ry = 0;
                sl::mouse_relative_state(&rx, &ry);
                eva_look_yaw -= rx * 0.003;
                eva_look_pitch -= ry * 0.003;
            }
            else if (dragging)
            {
                eva_look_yaw -= dx * 0.004;
                eva_look_pitch -= dy * 0.004;
            }
        }
        else if (in_user)
        {
            kosmos::UserCamera &uc = user_cams[static_cast<std::size_t>(user_cam)];
            if (uc.orbit)
            {
                uc.yaw += uc.spin * frame;
                if (dragging)
                {
                    uc.yaw -= dx * 0.4;
                    uc.pitch = std::clamp(uc.pitch + dy * 0.4, -89.0, 89.0);
                }
                if (zoom_in * zoom_out != 1.0)
                {
                    double d = uc.distance;
                    if (d <= 0.0)
                    {
                        const int idx = uc.attach == kosmos::UserCamera::body ? universe.find(uc.attach_name) : -1;
                        d = idx >= 0 ? universe.bodies[static_cast<std::size_t>(idx)].radius * 3.0 : ship->spec().camera_distance;
                    }
                    uc.distance = std::clamp(d * zoom_in * zoom_out, 2.0, 3.0e11);
                }
            }
            else if (dragging)
            {
                look_yaw -= static_cast<float>(dx) * 0.008f;
                look_pitch = std::clamp(look_pitch - static_cast<float>(dy) * 0.008f, -1.4f, 1.4f);
            }
        }
        else if (in_cockpit || in_interior)
        {
            if (dragging)
            {
                look_yaw -= static_cast<float>(dx) * 0.008f;
                look_pitch = std::clamp(look_pitch - static_cast<float>(dy) * 0.008f, -1.4f, 1.4f);
            }
        }
        else
        {
            if (dragging)
            {
                // Dragging swings the chase camera around the ship, which stays centred.
                cam_yaw -= static_cast<float>(dx) * 0.006f;
                cam_pitch = std::clamp(cam_pitch + static_cast<float>(dy) * 0.006f, -1.5f, 1.5f);
            }
            cam_distance = std::clamp(cam_distance * zoom_in * zoom_out, 8.0, 3.0e9);
        }

        // The camera orbits the ship in its local horizon frame, so the horizon stays level as the rocket pitches over.
        sl::dvec3 up, east, north;
        ship->local_frame(up, east, north);
        {
            // True east reverses over a pole, which would swing the camera to the far side. Carry the previous east along the surface
            // instead, and ease back to the compass east only where it is well defined.
            static sl::dvec3 carried_east(0.0);
            static int carried_soi = -1;
            if (carried_soi != tm.soi || glm::length(carried_east) < 0.5)
                carried_east = east;
            carried_soi = tm.soi;
            sl::dvec3 e = carried_east - up * glm::dot(carried_east, up);
            e = glm::length(e) > 1e-6 ? glm::normalize(e) : east;
            const double polar = std::fabs(tm.latitude);
            const double w = std::clamp((70.0 - polar) / 20.0, 0.0, 1.0) * std::min(1.0, 3.0 * frame);
            const sl::dvec3 blended = e * (1.0 - w) + east * w;
            carried_east = glm::length(blended) > 1e-6 ? glm::normalize(blended) : e;
            east = carried_east;
            north = glm::cross(up, east);
        }
        const double yaw_d = cam_yaw, pitch_d = cam_pitch;
        sl::dvec3 offset_dir = (east * std::sin(yaw_d) + north * std::cos(yaw_d)) * std::cos(pitch_d) + up * std::sin(pitch_d);
        if (options.cam_sunward)
            offset_dir = glm::normalize(universe.position(0) - fb.position);
        if (options.cam_antisun)
            offset_dir = glm::normalize(glm::normalize(fb.position - universe.position(0)) + east * std::sin(yaw_d) * 0.9 + up * std::sin(pitch_d) * 0.9);
        // A tall ship can name the point the external camera frames, such as the capsule at the top of a Saturn V.
        const sl::dvec3 focus = ship->spec().has_camera_target ? ship->origin() + fb.direction_to_world(ship->spec().camera_target) : fb.position;
        sl::dvec3 eye_world = focus + offset_dir * cam_distance;
        glm::vec3 ship_rel = glm::vec3(focus - eye_world);
        if (options.cam_antisun)
            ship_rel = glm::vec3(glm::normalize(universe.position(0) - eye_world) * 1000.0);
        if (!in_cockpit && !in_interior && !in_base_cam && !map_mode && !galaxy_mode && (free_yaw != 0.0f || free_pitch != 0.0f))
        {
            const sl::dvec3 fwd = glm::normalize(sl::dvec3(ship_rel));
            const sl::dvec3 right = glm::normalize(glm::cross(fwd, up));
            const sl::dvec3 turned = glm::angleAxis(static_cast<double>(free_yaw), up) * (glm::angleAxis(static_cast<double>(free_pitch), right) * fwd);
            ship_rel = glm::vec3(turned * glm::length(sl::dvec3(ship_rel)));
        }
        double view_fov = options.cam_fov;
        // The radius of a body an orbiting camera is on, so the near plane frames it rather than the flown ship.
        double user_extent = -1.0;
        if (in_cockpit || in_interior)
        {
            // Seen from the pilot's seat: the eye sits in the ship frame and the view looks along -Z, free to turn with the mouse.
            sl::dvec3 seat = ship->spec().cockpit.eye + (in_interior ? sl::dvec3(0.0) : eye_shift);
            double yaw = look_yaw, pitch = look_pitch;
            if (!in_interior)
            {
                yaw += ship->spec().cockpit.yaw * pi / 180.0;
                pitch += ship->spec().cockpit.pitch * pi / 180.0;
                if (ship->spec().cockpit.fov > 0.0)
                    view_fov = ship->spec().cockpit.fov;
            }
            if (in_interior)
            {
                const kosmos::CameraSpec &cs = ship->spec().cameras[static_cast<std::size_t>(interior_cam)];
                seat = cs.position;
                yaw += cs.yaw * pi / 180.0;
                pitch += cs.pitch * pi / 180.0;
                view_fov = cs.fov;
            }
            eye_world = ship->origin() + fb.direction_to_world(seat);
            const double cl = std::cos(pitch);
            sl::dvec3 look(-std::sin(yaw) * cl, std::sin(pitch), -std::cos(yaw) * cl);
            sl::dvec3 view_up(0.0, 1.0, 0.0);
            if (ship->spec().building() && !ship->spec().horizontal && !in_interior)
            {
                // An upright building's roof is -Z; looking along +X (out of the door) is yaw 0.
                look = sl::dvec3(std::cos(yaw) * cl, -std::sin(yaw) * cl, -std::sin(pitch));
                view_up = sl::dvec3(0.0, 0.0, -1.0);
            }
            ship_rel = glm::vec3(fb.direction_to_world(look) * 1000.0);
            up = fb.direction_to_world(view_up);
        }
        else if (in_eva)
        {
            eye_world = eva.eye_world(universe);
            ship_rel = glm::vec3(eva.forward() * 1000.0);
            up = eva.view_up();
            view_fov = std::max(options.cam_fov, 60.0);
        }
        else if (in_user)
        {
            const kosmos::UserCamera &uc = user_cams[static_cast<std::size_t>(user_cam)];
            // Where an attachment is, and the axes its camera offsets are in (a ship's own; for a body, east, up and south).
            struct Anchor
            {
                sl::dvec3 position;
                sl::dvec3 x, y, z;
                double radius = 0.0;
                bool ship = false;
            };
            auto ship_anchor = [](Ship &sh)
            {
                sl::FlightBody &b = sh.body();
                return Anchor{sh.origin(), b.direction_to_world(sl::dvec3(1, 0, 0)), b.direction_to_world(sl::dvec3(0, 1, 0)), b.direction_to_world(sl::dvec3(0, 0, 1)), 0.0, true};
            };
            auto anchor = [&](int kind, const std::string &name)
            {
                if (kind == kosmos::UserCamera::other_ship)
                    for (int si = 0; si < sim.ship_count(); ++si)
                        if (sim.ship(si)->name == name)
                            return ship_anchor(*sim.ship(si));
                if (kind == kosmos::UserCamera::body)
                {
                    const int bi = universe.find(name);
                    if (bi >= 0)
                        return Anchor{universe.position(bi), east, up, -north, universe.bodies[static_cast<std::size_t>(bi)].radius, false};
                }
                return ship_anchor(*ship);
            };
            const Anchor host = anchor(uc.attach, uc.attach_name);
            // The point of a ship a camera frames: its `camera_target` when it names one, else its origin.
            auto focus_of = [&](const int kind, const std::string &name, const Anchor &a)
            {
                Ship *sh = ship;
                if (kind == kosmos::UserCamera::other_ship)
                    for (int si = 0; si < sim.ship_count(); ++si)
                        if (sim.ship(si)->name == name)
                            sh = sim.ship(si);
                if (!a.ship || !sh || !sh->spec().has_camera_target)
                    return a.position;
                return a.position + sh->body().direction_to_world(sh->spec().camera_target);
            };
            view_fov = uc.fov;
            if (uc.orbit)
            {
                const double distance = uc.distance > 0.0 ? uc.distance : host.ship ? ship->spec().camera_distance : host.radius * 3.0;
                const double yaw = uc.yaw * pi / 180.0, pitch = uc.pitch * pi / 180.0;
                const sl::dvec3 dir = (east * std::sin(yaw) + north * std::cos(yaw)) * std::cos(pitch) + up * std::sin(pitch);
                eye_world = focus_of(uc.attach, uc.attach_name, host) + dir * distance;
                ship_rel = glm::vec3(-dir * distance);
                if (!host.ship)
                    user_extent = host.radius;
            }
            else
            {
                eye_world = host.position + host.x * uc.x + host.y * uc.y + host.z * uc.z;
                if (uc.look_at != kosmos::UserCamera::look_ahead)
                {
                    const int kind = uc.look_at == kosmos::UserCamera::look_flown_ship ? 0 : uc.look_at == kosmos::UserCamera::look_ship ? 1 : 2;
                    const sl::dvec3 to = anchor(kind, uc.look_name).position - eye_world;
                    ship_rel = glm::vec3(glm::length(to) > 1e-3 ? to : -host.z);
                    up = host.y;
                }
                else
                {
                    const double yaw = uc.look_yaw * pi / 180.0 + look_yaw, pitch = uc.look_pitch * pi / 180.0 + look_pitch;
                    const double cl = std::cos(pitch);
                    ship_rel = glm::vec3((host.x * -std::sin(yaw) * cl + host.y * std::sin(pitch) + host.z * -std::cos(yaw) * cl) * 1000.0);
                    up = host.y;
                }
            }
        }
        else if (in_base_cam)
        {
            const kosmos::Base &bs = sim.bases[static_cast<std::size_t>(base_view)];
            const kosmos::BaseCamera &bc = bs.cameras[static_cast<std::size_t>(base_view_cam)];
            sl::dvec3 look;
            kosmos::base_camera_pose(universe, bs, bc, &fb.position, eye_world, look, up);
            ship_rel = glm::vec3(look * 1000.0);
            view_fov = bc.fov;
        }

        const int soi = tm.soi;
        const kosmos::Body &soi_body = universe.bodies[static_cast<std::size_t>(soi)];
        const sl::dvec3 soi_pos = universe.position(soi);
        // The chase camera may not dip below the ground.
        if (!in_cockpit && !in_cockpit3d && !in_interior && !in_base_cam && !in_eva)
        {
            const sl::Terrain *soi_terrain = nullptr;
            for (const std::unique_ptr<BodyTerrain> &bt : body_terrains)
                if (bt->body == soi)
                    soi_terrain = bt->terrain.get();
            const sl::dvec3 from_centre = eye_world - soi_pos;
            const double dist = glm::length(from_centre);
            if (soi_terrain && dist > 1.0)
            {
                const sl::dvec3 dir = from_centre / dist;
                const double floor_r = soi_body.radius + soi_terrain->height_at_direction(dir) + 2.0;
                if (dist < floor_r)
                {
                    const sl::dvec3 shift = dir * (floor_r - dist);
                    eye_world += shift;
                    ship_rel -= glm::vec3(shift);
                }
            }
        }
        const bool home = soi == earth_id;
        const sl::dvec3 earth_pos = universe.position(earth_id);
        const sl::dvec3 eye_from_earth = eye_world - earth_pos;
        const double eye_earth_distance = glm::length(eye_from_earth);

        sl::TerrainView view;
        view.position = eye_from_earth;
        view.forward = glm::normalize(sl::dvec3(ship_rel));
        view.up = up;
        view.fov_y_degrees = view_fov;
        view.aspect = static_cast<double>(screen_w) / screen_h;
        view.viewport_height = screen_h;
        view.far_distance = eye_earth_distance + earth_body->radius * 1.3;
        const bool near_earth = eye_earth_distance < earth_body->radius + 2.0e7;
        // With a hi-res surface image, the terrain gives way to a plain textured sphere above 300 km (lower when the tiled imagery is present, which stays sharp down to there), scaled by the body's size.
        const bool earth_has_image = std::find(vis.surface_textured.begin(), vis.surface_textured.end(), earth_id) != vis.surface_textured.end();
        earth_sphere_switch_m = props.get_bool("/sim/settings/gfx-earth-relief") ? 300.0e3 : 20.0e3;
        const bool earth_as_sphere = earth_has_image && eye_earth_distance - earth_body->radius > (vis.earth_tiles ? earth_sphere_switch_m : 300.0e3) * earth_body->radius / 6.371e6;
        for (const std::unique_ptr<BodyTerrain> &bt : body_terrains)
        {
            const sl::FlightBody &fb = *universe.world.body(bt->body);
            bt->terrain->set_rotation(fb.spin_phase + fb.spin_rate * universe.world.time(), fb.spin_axis);
            kosmos::Body &tb = universe.bodies[static_cast<std::size_t>(bt->body)];
            for (kosmos::Base &base : sim.bases)
                if (base.body == tb.name)
                {
                    base.direction = kosmos::base_direction(universe, bt->body, base);
                    base.pole = universe.pole(bt->body);
                }
        }
        if (near_earth)
        {
            if (first_frame)
                settle(*terrain, view);
            else
                terrain->update(view);
        }
        for (std::size_t k = 1; k < body_terrains.size(); ++k)
        {
            BodyTerrain &bt = *body_terrains[k];
            const kosmos::Body &b = universe.bodies[static_cast<std::size_t>(bt.body)];
            bt.eye_rel = eye_world - universe.position(bt.body);
            const double threshold = 300.0e3 * b.radius / 6.371e6;
            const bool has_image = std::find(vis.surface_textured.begin(), vis.surface_textured.end(), bt.body) != vis.surface_textured.end();
            bt.active = glm::length(bt.eye_rel) - b.radius < (has_image ? threshold : 10.0 * threshold);
            if (bt.active)
            {
                sl::TerrainView body_view = view;
                body_view.position = bt.eye_rel;
                body_view.far_distance = glm::length(bt.eye_rel) + b.radius * 1.3;
                if (first_frame)
                    settle(*bt.terrain, body_view);
                else
                    bt.terrain->update(body_view);
            }
        }
        {
            // Trees, houses, rocks and outposts exist only near the ground; build them lazily, outside begin_surface_3d().
            const bool clutter_on = props.get_bool("/sim/settings/gfx-clutter", true) && !options.no_clutter;
            const double clutter_density = std::clamp(props.get_int("/sim/settings/gfx-clutter-density", 100), 10, 200) / 100.0;
            for (std::size_t k = 0; k < body_terrains.size(); ++k)
            {
                BodyTerrain &bt = *body_terrains[k];
                const kosmos::Body &b = universe.bodies[static_cast<std::size_t>(bt.body)];
                const bool drawn = k == 0 ? near_earth && !earth_as_sphere : bt.active;
                const sl::dvec3 &eye_rel = k == 0 ? eye_from_earth : bt.eye_rel;
                bt.clutter_live = clutter_on && drawn && glm::length(eye_rel) - b.radius < 6000.0 && kosmos::has_clutter(b);
                if (bt.clutter_live)
                {
                    // Scatter only where it can matter: within 50 km of a base, or of a ship flying below 5 km.
                    const glm::dquat unturn = glm::inverse(glm::angleAxis(bt.terrain->rotation(), bt.terrain->rotation_axis()));
                    const sl::dvec3 eye_dir = glm::normalize(unturn * eye_rel);
                    constexpr double clutter_range = 50.0e3;
                    bool near = kosmos::distance_to_zones(bt.clutter_zones, eye_dir, b.radius) < clutter_range;
                    const kosmos::Telemetry &tel = ship->telemetry();
                    if (!near && tel.soi == bt.body && tel.altitude < 5000.0)
                    {
                        const sl::dvec3 ship_dir = glm::normalize(unturn * (ship->origin() - universe.position(bt.body)));
                        near = std::acos(std::clamp(glm::dot(eye_dir, ship_dir), -1.0, 1.0)) * b.radius < clutter_range;
                    }
                    bt.clutter_live = near;
                }
                if (!bt.clutter_live)
                    continue;
                if (bt.clutter && bt.clutter_density != clutter_density)
                    bt.clutter.reset();
                if (!bt.clutter)
                {
                    kosmos::ClutterWorld world;
                    world.body = &b;
                    world.frame = bt.frame;
                    world.zones = bt.clutter_zones;
                    world.land = bt.landclass;
                    world.density = clutter_density;
                    world.near_direction = glm::normalize(glm::inverse(glm::angleAxis(bt.terrain->rotation(), bt.terrain->rotation_axis())) * eye_rel);
                    world.root = sim.root();
                    bt.clutter = kosmos::make_clutter(*bt.terrain, world);
                    bt.clutter_density = clutter_density;
                }
                if (!bt.clutter)
                {
                    bt.clutter_live = false;
                    continue;
                }
                sl::TerrainView clutter_view = view;
                clutter_view.position = eye_rel;
                clutter_perf.update_ms += time_ms([&] { bt.clutter->update(clutter_view); });
            }
        }
        first_frame = false;
        if (options.perf)
        {
            ++clutter_perf.frames;
            const auto now = std::chrono::steady_clock::now();
            const double span = std::chrono::duration<double>(now - clutter_perf.last).count();
            if (span >= 3.0)
            {
                std::fprintf(stderr, "[perf] %.1f fps (%.2f ms/frame); clutter CPU: update %.3f ms, draw %.3f ms per frame\n", clutter_perf.frames / span,
                             1000.0 * span / clutter_perf.frames, clutter_perf.update_ms / clutter_perf.frames, clutter_perf.draw_ms / clutter_perf.frames);
                clutter_perf = ClutterPerf();
            }
        }
        loading.end();

        const sl::dvec3 sun_dir_world = glm::normalize(-eye_world);
        const glm::vec3 to_sun = glm::vec3(sun_dir_world);

        sl::Camera camera;
        camera.position = glm::vec3(0.0f);
        camera.target = ship_rel;
        camera.up = glm::vec3(up);
        camera.field_of_view = static_cast<float>(view_fov);
        double extent = 4.0;
        for (const kosmos::StageSpec &st : ship->spec().stages)
            extent = std::max(extent, std::max(std::fabs(st.z - st.length * 0.5), std::fabs(st.z + st.length * 0.5)) + 6.0);
        const double eye_soi_altitude = glm::length(eye_world - soi_pos) - soi_body.radius;
        const double nearest_ground = std::max(1.0, 0.9 * (eye_soi_altitude - 10.0e3));
        const double view_distance = in_user && user_cams[static_cast<std::size_t>(user_cam)].orbit ? glm::length(sl::dvec3(ship_rel)) : cam_distance;
        const bool in_user_mounted = in_user && !user_cams[static_cast<std::size_t>(user_cam)].orbit;
        const double ship_near = in_cockpit3d ? 0.25 : (in_cockpit || in_interior) ? 1.0 : (in_base_cam || in_user_mounted) ? 1.0 : std::max(1.0, std::min(view_distance - (user_extent >= 0.0 ? user_extent : extent), nearest_ground));
        // As Orbiter does: out in space the planets are drawn first with a near plane set by the nearest body, then the depth buffer
        // is cleared and the ships go over them with their own near plane, so neither range has to share one depth buffer.
        static bool depth_layers_work = true;
        const bool layered = depth_layers_work && !in_interior && !in_base_cam && !in_user_mounted && eye_soi_altitude > 20.0e3;
        double near_plane = ship_near;
        if (layered)
        {
            double nearest_surface = 1.0e30;
            for (std::size_t bi = 0; bi < universe.bodies.size(); ++bi)
                nearest_surface = std::min(nearest_surface, glm::length(universe.position(static_cast<int>(bi)) - eye_world) - universe.bodies[bi].radius);
            // Capped so a star-sized distance cannot push the near plane past the far one.
            near_plane = std::max(ship_near, std::min(0.8 * nearest_surface, 1.0e8));
        }
        camera.near_plane = static_cast<float>(near_plane);
        camera.far_plane = static_cast<float>(layered || view_distance * 0.3 > 1.0e3 ? 1.0e10 : near_earth ? std::max(view.far_distance, soi_body.radius * 3.0) : std::max(soi_body.radius * 8.0, 6.0e8));
        sl::Camera ship_camera = camera;
        ship_camera.near_plane = static_cast<float>(ship_near);
        ship_camera.far_plane = 1.0e8f;

        sl::Environment3D environment;
        environment.sun_direction = -to_sun;
        environment.up = glm::vec3(glm::normalize(eye_from_earth));
        environment.camera_altitude = static_cast<float>(eye_earth_distance - earth_body->radius);
        environment.camera_position = eye_from_earth;
        environment.time = clock;
        environment.planet = near_earth;
        environment.planet_radius = static_cast<float>(earth_body->radius);
        environment.sun_intensity = 20.0f;
        environment.haze_scale = 0.35f;
        environment.ground_albedo = {0.1f, 0.25f, 0.12f};
        environment.fog_enabled = near_earth;
        environment.fog_density = near_earth ? 1.4e-4f : 0.0f;
        if (near_earth && props.get_bool("/sim/settings/weather", true))
        {
            const double vis = props.get_double("/environment/visibility", 1.0e5);
            if (vis > 50.0 && vis < 20000.0)
                environment.fog_density = std::max(environment.fog_density, static_cast<float>(3.0 / vis));
        }

        if (in_cockpit3d)
        {
            sim.mfd.bus_power = !sim.active_ship() || !sim.active_ship()->systems() || sim.active_ship()->systems()->instruments_powered();
            sim.mfd.update(frame);
            const kosmos::Cockpit pcp = resolved_cockpit(ship->spec().cockpit, props.get_int("/sim/active-ship"));
            if (pcp.instruments.empty() && pcp.controls.empty())
            {
                for (int side = 0; side < 2; ++side)
                    sim.mfd.draw_to_target(side, mfd_target[side], mfd_target_layout());
            }
            else
            {
                const int wanted = static_cast<int>(std::ceil(panel_target_width * pcp.panel_height / pcp.panel_width));
                if (!panel_target || wanted != panel_target_height)
                    panel_target = sl::create_render_target(panel_target_width, panel_target_height = wanted);
                if (panel_target)
                    panel_target->smooth = true;
                if (panel_target && sl::begin_render_target(panel_target))
                {
                    sl::clear_render_target({18, 20, 26, 255});
                    draw_panel(sim, pcp, pcp.panel_image.empty() ? "" : (std::filesystem::path(ship->spec().directory) / pcp.panel_image).string(),
                               0.0f, 0.0f, static_cast<float>(panel_target_width) / pcp.panel_width);
                    sl::end_render_target();
                }
            }
        }
        if (sl::begin_surface_3d(surface, camera))
        {
            sl::clear_surface_3d({0, 0, 0, 255});
            sl::set_environment_3d(environment);
            if (near_earth && gfx_style == 0)
                sl::draw_sky_3d();
            sl::Light3D light;
            light.direction = environment.sun_direction;
            // Above the atmosphere the Sun is not reddened by it, even when it sits on the horizon below.
            light.colour = near_earth && eye_earth_distance < earth_body->radius + 1.0e6 ? sl::atmosphere_sun_colour(environment) : glm::vec3(1.0f);
            light.ambient = options.ambient;
            // Near a surface the sky's fill light drops away after dusk, which is when lamps matter.
            {
                const int body_i = std::clamp(tm.soi, 0, static_cast<int>(universe.bodies.size()) - 1);
                const kosmos::Body &here = universe.bodies[static_cast<std::size_t>(body_i)];
                const sl::dvec3 from_centre = eye_world - universe.position(body_i);
                const double alt_here = glm::length(from_centre) - here.radius;
                if (body_i != 0 && alt_here < 200.0e3)
                {
                    const double sun_height = glm::dot(glm::normalize(from_centre), glm::normalize(universe.position(0) - eye_world));
                    const double dusk = std::clamp((0.10 - sun_height) / 0.16, 0.0, 1.0);
                    const double low = 1.0 - std::clamp((alt_here - 50.0e3) / 150.0e3, 0.0, 1.0);
                    light.ambient = static_cast<float>(glm::mix(static_cast<double>(options.ambient), std::min<double>(options.ambient, universe.world.air_density_at(eye_world) < 0.1 ? 0.02 : 0.07), dusk * dusk * (3.0 - 2.0 * dusk) * low));
                }
            }
            if (sim.system_index() != 0)
            {
                // Other suns light the scene in their own colour, and dimmer or brighter than Sol's 1 AU by their luminosity and our distance.
                const float *c = universe.bodies[0].colour;
                const float peak = std::max({c[0], c[1], c[2], 0.01f});
                const glm::vec3 hue = glm::mix(glm::vec3(c[0], c[1], c[2]) / peak, glm::vec3(1.0f), 0.45f);
                const double au_now = std::max(1.0e-3, glm::length(eye_world) / 1.495978707e11);
                const double flux = sim.galaxy.star(sim.system_index()).luminosity / (au_now * au_now);
                light.colour *= hue * static_cast<float>(std::clamp(std::pow(flux, 0.3), 0.45, 1.0));
            }
            sl::set_light_3d(light);

            // Lamps: the bases' floodlights and street lamps after dusk, and every ship's running lights.
            std::vector<kosmos::PlacedLight> lamps;
            {
                std::vector<sl::dvec3> ship_positions;
                for (int s = 0; s < sim.ship_count(); ++s)
                    ship_positions.push_back(sim.ship(s)->body().position);
                kosmos::collect_base_lights(universe, sim.bases, eye_world, static_cast<double>(sim.props.get_int("/sim/settings/gfx-light-radius")), clock, ship_positions, lamps);
                for (int s = 0; s < sim.ship_count(); ++s)
                {
                    const Ship &lit_ship = *sim.ship(s);
                    const sl::FlightBody &lb = lit_ship.body();
                    const glm::dmat3 turn = glm::mat3_cast(lb.orientation);
                    // A ship on the ground turns its lamps off while the Sun is up.
                    bool daylight_on_ground = false;
                    if (lit_ship.telemetry().landed && lit_ship.telemetry().soi > 0)
                    {
                        const sl::dvec3 up_here = glm::normalize(lit_ship.origin() - universe.position(lit_ship.telemetry().soi));
                        daylight_on_ground = glm::dot(up_here, glm::normalize(universe.position(0) - lit_ship.origin())) > 0.05;
                    }
                    if (daylight_on_ground)
                        continue;
                    for (const kosmos::LightSpec &spec : lit_ship.spec().lights)
                    {
                        if (spec.stage >= 0 && lit_ship.active_stage() > spec.stage)
                            continue;
                        if (lit_ship.systems() && !lit_ship.systems()->instruments_powered())
                            continue;
                        for (int i = 0; i < spec.count; ++i)
                        {
                            const sl::dvec3 at = lit_ship.origin() + turn * sl::dvec3(spec.x + spec.step_x * i, spec.y + spec.step_y * i, spec.z + spec.step_z * i);
                            const double distance = glm::length(at - eye_world);
                            const float on = kosmos::light_pattern(spec, clock + i * 0.31);
                            if (distance > 40000.0 || on <= 0.001f)
                                continue;
                            kosmos::PlacedLight p;
                            p.light.position = glm::vec3(at - eye_world);
                            p.light.colour = glm::vec3(spec.colour.red, spec.colour.green, spec.colour.blue) / 255.0f;
                            p.light.intensity = spec.intensity * on;
                            p.light.range = spec.range;
                            p.light.inner_angle = spec.inner;
                            p.light.outer_angle = spec.outer;
                            p.light.direction = glm::vec3(glm::normalize(turn * sl::dvec3(spec.dx, spec.dy, spec.dz)));
                            p.glow_colour = p.light.colour;
                            p.glow_size = spec.glow;
                            p.glow_strength = on;
                            if (spec.navigation)
                            {
                                p.light.intensity *= 0.35f;
                                p.glow_size *= 0.5f;
                                p.glow_strength *= 0.7f;
                                p.far_size = 0.003f;
                            }
                            p.distance = static_cast<float>(distance);
                            lamps.push_back(p);
                        }
                    }
                }
                std::vector<const kosmos::PlacedLight *> order;
                for (const kosmos::PlacedLight &p : lamps)
                    order.push_back(&p);
                // The lamps that reach nearest the camera win the sixteen slots.
                std::sort(order.begin(), order.end(), [](const kosmos::PlacedLight *a, const kosmos::PlacedLight *b)
                          { return a->distance - 0.5f * a->light.range < b->distance - 0.5f * b->light.range; });
                std::vector<sl::LocalLight3D> chosen;
                for (const kosmos::PlacedLight *p : order)
                    if (static_cast<int>(chosen.size()) < sl::max_local_lights_3d && p->light.intensity > 0.0f && p->distance < 4000.0f)
                    {
                        chosen.push_back(p->light);
                        chosen.back().intensity *= 4.0f;
                        chosen.back().range *= 1.4f;
                    }
                sl::set_local_lights_3d(chosen);
            }

            // Where the sky and the stars sit: inside the far plane, and beyond the near plane even when that is pushed out by a distant nearest body.
            const double backdrop = std::clamp(std::min(camera.far_plane * 0.05, camera.near_plane * 1.0e5), camera.near_plane * 1.5, camera.far_plane * 0.9);
            // Every star is a real sphere at its true angular size, placed inside the far plane without changing how big it looks.
            struct StarPlace
            {
                bool valid = false;
                glm::vec3 dir{0.0f}, tint{1.0f};
                double distance = 0.0, radius = 0.0, angular = 0.0;
            };
            auto place_star = [&](std::size_t i) -> StarPlace
            {
                StarPlace sp;
                const kosmos::Body &b = universe.bodies[i];
                const sl::dvec3 rel = universe.position(static_cast<int>(i)) - eye_world;
                const double dist = glm::length(rel);
                if (b.kind != kosmos::BodyKind::star || dist <= b.radius * 1.02)
                    return sp;
                const double limit = backdrop;
                sp.valid = true;
                sp.dir = glm::vec3(rel / dist);
                sp.angular = std::asin(std::min(1.0, b.radius / dist));
                sp.distance = std::min(dist, limit);
                sp.radius = dist < limit ? b.radius : b.radius * limit / dist;
                const float peak = std::max({b.colour[0], b.colour[1], b.colour[2], 0.01f});
                // Cool and red stars keep their hue but are lifted toward white so no star's surface reads dark.
                sp.tint = glm::mix(glm::vec3(b.colour[0], b.colour[1], b.colour[2]) / peak, glm::vec3(1.0f), 0.3f);
                return sp;
            };
            auto star_basis_base = [&](const StarPlace &sp, double distance, double half, float spin)
            {
                const glm::vec3 to_eye = -sp.dir;
                glm::vec3 right = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), to_eye));
                glm::vec3 up_v = glm::cross(to_eye, right);
                const float c = std::cos(spin), sn = std::sin(spin);
                const glm::vec3 r2 = right * c + up_v * sn, u2 = -right * sn + up_v * c;
                return glm::mat4(glm::vec4(r2 * static_cast<float>(half), 0.0f), glm::vec4(to_eye, 0.0f), glm::vec4(u2 * static_cast<float>(half), 0.0f),
                                 glm::vec4(sp.dir * static_cast<float>(distance), 1.0f));
            };
            auto tint_of = [](const glm::vec3 &c, float alpha)
            {
                return sl::Colour{static_cast<std::uint8_t>(255.0f * std::clamp(c.r, 0.0f, 1.0f)), static_cast<std::uint8_t>(255.0f * std::clamp(c.g, 0.0f, 1.0f)),
                                  static_cast<std::uint8_t>(255.0f * std::clamp(c.b, 0.0f, 1.0f)), static_cast<std::uint8_t>(255.0f * std::clamp(alpha, 0.0f, 1.0f))};
            };

            // The photosphere: an alpha-blended shell and an additive shell of filaments turning at different rates. Blended draws never write
            // depth, so the layers cannot z-fight at star distances where the depth buffer has no resolution; the late star loop calls this.
            auto draw_photosphere = [&](const StarPlace &sp)
            {
                if (!sp.valid || sp.angular < 2.0e-4)
                    return;
                const glm::mat4 at = glm::translate(glm::mat4(1.0f), sp.dir * static_cast<float>(sp.distance));
                sl::Material surface;
                surface.texture = vis.star_surface;
                surface.lit = false;
                surface.fog = false;
                surface.blend = sl::MaterialBlend::alpha;
                surface.tint = tint_of(sp.tint * 1.0f, 1.0f);
                sl::draw_mesh(vis.sphere, at * glm::rotate(glm::mat4(1.0f), static_cast<float>(clock * 0.015), glm::vec3(0, 1, 0)) *
                                              glm::scale(glm::mat4(1.0f), glm::vec3(static_cast<float>(sp.radius))), surface);
                sl::Material bloom;
                bloom.texture = vis.star_surface;
                bloom.lit = false;
                bloom.fog = false;
                bloom.blend = sl::MaterialBlend::additive;
                bloom.tint = tint_of(sp.tint, 0.08f);
                sl::draw_mesh(vis.sphere, at * glm::rotate(glm::mat4(1.0f), static_cast<float>(clock * 0.015), glm::vec3(0, 1, 0)) *
                                              glm::scale(glm::mat4(1.0f), glm::vec3(static_cast<float>(sp.radius * 1.0005))), bloom);
                sl::Material filaments;
                filaments.texture = vis.star_filaments;
                filaments.lit = false;
                filaments.fog = false;
                filaments.blend = sl::MaterialBlend::additive;
                filaments.tint = tint_of(glm::mix(sp.tint, glm::vec3(1.0f), 0.5f), 0.45f);
                sl::draw_mesh(vis.sphere, at * glm::rotate(glm::mat4(1.0f), static_cast<float>(-clock * 0.022 + 1.3), glm::vec3(0.3f, 1.0f, 0.1f)) *
                                              glm::scale(glm::mat4(1.0f), glm::vec3(static_cast<float>(sp.radius * 1.002))), filaments);
            };

            // The sky, stars and every star's glare go first, close in where the depth buffer still has resolution, and write no depth;
            // the opaque scene drawn afterwards then covers them wherever it is in front.
            // Stars fade in as the sky darkens.
            {
                const double altitude = home ? tm.altitude : 1.0e6;
                const float star_alpha = static_cast<float>(fx::smoothstep(30.0e3, 110.0e3, altitude));
                if (star_alpha > 0.0f)
                {
                    sl::Material stars;
                    stars.texture = vis.puff_texture;
                    stars.lit = false;
                    stars.fog = false;
                    stars.cull = sl::CullMode::none;
                    stars.blend = sl::MaterialBlend::additive;
                    stars.tint.alpha = static_cast<std::uint8_t>(255.0f * star_alpha);
                    sl::Material sky;
                    sky.texture = vis.sky;
                    sky.lit = false;
                    sky.fog = false;
                    sky.cull = sl::CullMode::none;
                    sky.blend = sl::MaterialBlend::additive;
                    sky.tint.alpha = stars.tint.alpha;
                    // Close enough that depth keeps its resolution against the far plane, and turned so the pinched pole triangles face an axis the camera rarely looks along.
                    sl::draw_mesh(vis.sphere, glm::rotate(glm::mat4(1.0f), 1.5708f, glm::vec3(1, 0, 0)) * glm::scale(glm::mat4(1.0f), glm::vec3(static_cast<float>(backdrop))), sky);
                    // Nebulae only away from Sol, and behind the stars.
                    if (sim.system_index() != 0 && vis.nebula_atlas)
                    {
                        if (vis.nebulae_system != sim.system_index())
                        {
                            if (vis.nebulae)
                                sl::destroy_mesh(vis.nebulae);
                            vis.nebulae = make_nebulae(sim.system_index());
                            vis.nebulae_system = sim.system_index();
                        }
                        if (vis.nebulae)
                        {
                            sl::Material neb = sky;
                            neb.texture = vis.nebula_atlas;
                            sl::draw_mesh(vis.nebulae, glm::scale(glm::mat4(1.0f), glm::vec3(static_cast<float>(backdrop))), neb);
                        }
                    }
                    sl::draw_mesh(vis.starfield, glm::scale(glm::mat4(1.0f), glm::vec3(static_cast<float>(backdrop))), stars);
                }
            }

            // Limb darkening, corona and glare for every star: constant-looking at any distance, bigger for bigger or nearer stars.
                        for (std::size_t i = 0; i < universe.bodies.size(); ++i)
            {
                const StarPlace sp = place_star(i);
                if (!sp.valid)
                    continue;
                const double D = sp.distance;
                const double disc = D * std::tan(sp.angular);
                const glm::vec3 white = glm::vec3(1.0f);
                const double min_half = D * std::tan(glm::radians(1.8));
                auto star_basis = [&](const StarPlace &s, double distance, double half, float spin) { return star_basis_base(s, distance, half, spin); };
                draw_photosphere(sp);
                // Up close the glare thins out so the surface shows through; from far away it stays full.
                const float gk = 1.0f - 0.92f * static_cast<float>(fx::smoothstep(0.05, 0.5, sp.angular / std::tan(glm::radians(view_fov * 0.5))));
                if (sp.angular >= 2.0e-4)
                {
                    sl::Material limb;
                    limb.texture = vis.star_limb;
                    limb.lit = false;
                    limb.fog = false;
                    limb.cull = sl::CullMode::none;
                    limb.blend = sl::MaterialBlend::alpha;
                    sl::draw_mesh(vis.quad, star_basis(sp, D, disc, 0.0f), limb);
                }
                sl::Material glow;
                glow.lit = false;
                glow.fog = false;
                glow.cull = sl::CullMode::none;
                glow.blend = sl::MaterialBlend::additive;
                // The corona: two layers of streamers turning against each other so they shimmer.
                const double corona_half = std::max(disc * 5.0, min_half * 1.5);
                glow.texture = vis.star_corona;
                glow.tint = tint_of(glm::mix(sp.tint, white, 0.35f), 0.9f * gk);
                sl::draw_mesh(vis.quad, star_basis(sp, D, corona_half, static_cast<float>(clock * 0.012)), glow);
                glow.tint = tint_of(glm::mix(sp.tint, white, 0.2f), 0.7f * gk);
                sl::draw_mesh(vis.quad, star_basis(sp, D, corona_half * 0.85, static_cast<float>(-clock * 0.017 + 2.0)), glow);
                // The far halo, the old soft glow and the diffraction spikes.
                glow.texture = vis.star_corona_far;
                glow.tint = tint_of(sp.tint, 0.55f * gk);
                sl::draw_mesh(vis.quad, star_basis(sp, D, std::max(disc * 14.0, min_half * 4.0), static_cast<float>(clock * 0.004)), glow);
                glow.texture = vis.glow_texture;
                glow.tint = tint_of(sp.tint, 1.0f * gk);
                sl::draw_mesh(vis.quad, star_basis(sp, D, std::max(disc * 3.0, min_half * 2.2), 0.0f), glow);
                glow.texture = vis.star_rays;
                glow.tint = tint_of(glm::mix(sp.tint, white, 0.5f), 0.8f * gk);
                const double rays_half = std::max(disc * 18.0, min_half * 6.0) * (1.0 + 0.04 * std::sin(clock * 1.7 + static_cast<double>(i)));
                sl::draw_mesh(vis.quad, star_basis(sp, D, rays_half, 0.0f), glow);
                sl::draw_mesh(vis.quad, star_basis(sp, D, rays_half * 0.55, 0.7853982f), glow);
            }

            if (near_earth)
            {
                if (!earth_as_sphere)
                {
                    sl::Material ground;
                    if (props.get_bool("/sim/settings/gfx-earth-relief"))
                    {
                        ground.pbr = true;
                        ground.metallic = 0.0f;
                        ground.roughness = 0.4f;
                    }
                    terrain->draw(eye_from_earth, ground);
                    if (body_terrains[0]->clutter_live)
                        clutter_perf.draw_ms += time_ms([&] { body_terrains[0]->clutter->draw(eye_from_earth); });
                }
            }

            for (std::size_t k = 1; k < body_terrains.size(); ++k)
                if (body_terrains[k]->active)
                {
                    sl::Material ground;
                    body_terrains[k]->terrain->draw(body_terrains[k]->eye_rel, ground);
                    if (body_terrains[k]->clutter_live)
                        clutter_perf.draw_ms += time_ms([&] { body_terrains[k]->clutter->draw(body_terrains[k]->eye_rel); });
                }
            {
                sl::Material props;
                base_renderer.draw(universe, sim.bases, eye_world, 15000.0, props);
            }

            // A sunlit limb: a camera-facing disc whose opacity follows the atmosphere's thickness along each grazing ray.
            std::size_t limb_used = 0;
            auto draw_atmosphere_glow = [&](const sl::dvec3 &rel, const kosmos::Body &b)
            {
                if (b.atmosphere_density <= 0.0)
                    return;
                // Every atmosphere gets a broad haze, just under Venus's; denser ones, Venus most of all, are stronger still.
                const double dense = std::clamp(std::log10(1.0 + b.atmosphere_density) / std::log10(66.0), 0.0, 1.0);
                const double haze = 3.0 + 1.4 * dense;
                const double thickness = std::max(b.atmosphere_scale_height * 10.0, b.radius * 0.04) * haze * 0.7;
                if (glm::length(rel) - b.radius < thickness)
                    return;
                if (limb_used == vis.limb_pool.size())
                    vis.limb_pool.push_back(make_limb_mesh());
                sl::Mesh *mesh = vis.limb_pool[limb_used++];
                update_limb_mesh(mesh, glm::vec3(rel), b.radius, thickness, std::max(b.atmosphere_scale_height * haze * haze, b.radius * 0.04 * (0.8 + 0.2 * dense)), to_sun,
                                 glm::vec3(b.atmosphere_colour[0], b.atmosphere_colour[1], b.atmosphere_colour[2]));
                sl::Material shell;
                shell.lit = false;
                shell.fog = false;
                shell.cull = sl::CullMode::none;
                shell.blend = sl::MaterialBlend::additive;
                sl::draw_mesh(mesh, glm::mat4(1.0f), shell);
            };

            // Other bodies are spheres while inside the far plane; farther ones become markers on the overlay.
            // Planets and their clouds have no fill light of their own: the night side stays dark (the ambient term is for ships).
            sl::Light3D body_light = light;
            body_light.ambient = std::min(light.ambient, 0.04f);
            sl::set_light_3d(body_light);
            for (std::size_t i = 0; i < universe.bodies.size(); ++i)
            {
                const kosmos::Body &b = universe.bodies[i];
                if (b.kind == kosmos::BodyKind::star || (static_cast<int>(i) == earth_id && near_earth && !earth_as_sphere))
                    continue;
                bool terrain_drawn = false;
                for (std::size_t k = 1; k < body_terrains.size(); ++k)
                    terrain_drawn = terrain_drawn || (body_terrains[k]->body == static_cast<int>(i) && body_terrains[k]->active);
                if (terrain_drawn)
                {
                    // The terrain stands in for the surface sphere, but the weather above it still has to be drawn.
                    if (i < vis.body_cloud_meshes.size() && vis.body_cloud_meshes[i] && !options.no_clouds && gfx_clouds)
                    {
                        const sl::dvec3 rel = universe.position(static_cast<int>(i)) - eye_world;
                        const double distance = glm::length(rel);
                        if (distance - b.radius > b.cloud_height * 1.5)
                        {
                            sl::Material weather;
                            weather.texture = vis.body_cloud_textures[i];
                            weather.blend = sl::MaterialBlend::alpha;
                            weather.fog = false;
                            weather.tint = {static_cast<std::uint8_t>(b.cloud_colour[0] * 255.0f), static_cast<std::uint8_t>(b.cloud_colour[1] * 255.0f),
                                            static_cast<std::uint8_t>(b.cloud_colour[2] * 255.0f), 255};
                            // Terrain is coarse from afar, so the shell is lifted with the depth resolution, as Earth's is.
                            const double altitude_now = distance - b.radius;
                            const double resolution = altitude_now * altitude_now / (near_plane * 1.68e7);
                            double shell_height = std::clamp(2.0 * resolution, b.cloud_height, 600.0e3);
                            if (layered)
                                shell_height = std::min(shell_height, std::max(1.0e3, altitude_now - 1.05 * near_plane));
                            shell_height = std::max(shell_height, b.cloud_height);
                            const float k = static_cast<float>((b.radius + shell_height) / (b.radius + b.cloud_height));
                            const glm::mat4 shell_frame = glm::translate(glm::mat4(1.0f), glm::vec3(rel)) * glm::rotate(glm::mat4(1.0f), static_cast<float>(b.tilt), glm::vec3(0, 0, 1));
                            sl::draw_mesh(vis.body_cloud_meshes[i], shell_frame * glm::mat4_cast(glm::quat(universe.orientation(static_cast<int>(i)))) * glm::scale(glm::mat4(1.0f), glm::vec3(k)), weather);
                        }
                    }
                    continue;
                }
                const sl::dvec3 rel = universe.position(static_cast<int>(i)) - eye_world;
                const double distance = glm::length(rel);
                if (distance - b.radius > camera.far_plane * 0.95)
                    continue;
                // Earth's sphere image has to be turned half a revolution to put longitude 0 where the terrain and the bases have it.
                const glm::dquat sphere_flip = static_cast<int>(i) == earth_id ? glm::angleAxis(pi, sl::dvec3(0.0, 1.0, 0.0)) : glm::dquat(1.0, 0.0, 0.0, 0.0);
                sl::Material rock;
                rock.texture = vis.body_textures[i];
                rock.normal_map = vis.body_normals[i];
                rock.fog = false;
                const glm::mat4 frame = glm::translate(glm::mat4(1.0f), glm::vec3(rel)) * glm::rotate(glm::mat4(1.0f), static_cast<float>(b.tilt), glm::vec3(0, 0, 1));
                const glm::mat4 model = frame * glm::mat4_cast(glm::quat(universe.orientation(static_cast<int>(i)) * sphere_flip)) *
                                        glm::scale(glm::mat4(1.0f), glm::vec3(static_cast<float>(b.radius)));
                if (static_cast<int>(i) == earth_id && vis.earth_tiles)
                {
                    // The tile mesh already runs east the same way as the body frame, so only the half turn above is needed.
                    const glm::mat4 tile_model = model;
                    const glm::mat4 view_projection = camera.projection_matrix(static_cast<float>(screen_w) / screen_h) * camera.view_matrix() * tile_model;
                    const glm::dvec3 eye_local = glm::dvec3(glm::inverse(tile_model) * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
                    // The terrain below the switch altitude has no sea glint, so it fades out on the way down instead of vanishing at the swap.
                    const double glint_altitude = (glm::length(rel) - b.radius) * 6.371e6 / b.radius;
                    vis.earth_tiles->set_specular_strength(0.45f * static_cast<float>(fx::smoothstep(earth_sphere_switch_m, 300.0e3, glint_altitude)));
                    vis.earth_tiles->update(eye_local, glm::radians(view_fov), screen_h, &view_projection);
                    vis.earth_tiles->draw(tile_model, rock);
                }
                else
                {
                    // The single-image sphere mesh runs east the other way, so mirror it (and swap the culling to match).
                    if (static_cast<int>(i) == earth_id)
                        rock.cull = sl::CullMode::front;
                    sl::draw_mesh(i < vis.body_meshes.size() && vis.body_meshes[i] ? vis.body_meshes[i]->parts[0].mesh : vis.sphere,
                                  static_cast<int>(i) == earth_id ? model * glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 1.0f, -1.0f)) : model, rock);
                }
                // Depth precision thins with distance, so each layer over the surface is scaled about the eye towards the camera: it looks the
                // same but sits clear of the planet in depth. The layers are lifted by different amounts so they keep their order too.
                const double depth_gap = std::min(16.0 * distance * distance / (near_plane * 1.68e7), 0.3 * distance);
                auto lifted = [&](double fraction)
                {
                    const float lift = static_cast<float>(1.0 - depth_gap * fraction / distance);
                    return glm::translate(glm::mat4(1.0f), glm::vec3(rel) * lift) * glm::rotate(glm::mat4(1.0f), static_cast<float>(b.tilt), glm::vec3(0, 0, 1)) *
                           glm::mat4_cast(glm::quat(universe.orientation(static_cast<int>(i)) * sphere_flip)) *
                           glm::scale(glm::mat4(1.0f), glm::vec3(static_cast<float>(b.radius) * lift));
                };
                if (static_cast<int>(i) == earth_id && vis.lights_texture)
                {
                    // City lights: lit by a light shining from the night side, so they glow only where the Sun is not.
                    sl::Light3D night;
                    night.direction = to_sun;
                    night.ambient = 0.0f;
                    sl::set_light_3d(night);
                    sl::Material lights;
                    lights.texture = vis.lights_texture;
                    lights.fog = false;
                    lights.blend = sl::MaterialBlend::additive;
                    // Drawn on the same single-image sphere as the surface, so it takes the same mirroring.
                    lights.cull = sl::CullMode::front;
                    sl::draw_mesh(vis.sphere, lifted(0.25) * glm::scale(glm::mat4(1.0f), glm::vec3(1.0005f, 1.0005f, -1.0005f)), lights);
                    sl::set_light_3d(body_light);
                }
                if (b.atmosphere_density > 0.5 && b.atmosphere_density < 5.0)
                {
                    // Rayleigh scattering over the sunlit side: a faint, lit blue veil that lifts the oceans and hazes the land.
                    sl::Material veil;
                    veil.lit = true;
                    veil.fog = false;
                    veil.blend = sl::MaterialBlend::additive;
                    veil.tint = {static_cast<std::uint8_t>(b.atmosphere_colour[0] * 255.0f * 0.5f), static_cast<std::uint8_t>(b.atmosphere_colour[1] * 255.0f * 0.5f),
                                 static_cast<std::uint8_t>(b.atmosphere_colour[2] * 255.0f * 0.5f), 255};
                    sl::draw_mesh(vis.sphere, lifted(0.5) * glm::scale(glm::mat4(1.0f), glm::vec3(1.003f)), veil);
                }
                if (i < vis.body_cloud_meshes.size() && vis.body_cloud_meshes[i])
                {
                    sl::Material weather;
                    weather.texture = vis.body_cloud_textures[i];
                    weather.blend = sl::MaterialBlend::alpha;
                    weather.fog = false;
                    weather.tint = {static_cast<std::uint8_t>(b.cloud_colour[0] * 255.0f), static_cast<std::uint8_t>(b.cloud_colour[1] * 255.0f),
                                    static_cast<std::uint8_t>(b.cloud_colour[2] * 255.0f), 200};
                    const float k = static_cast<float>(1.0 - depth_gap / distance);
                    const glm::mat4 shell_frame = glm::translate(glm::mat4(1.0f), glm::vec3(rel) * k) * glm::rotate(glm::mat4(1.0f), static_cast<float>(b.tilt), glm::vec3(0, 0, 1));
                    sl::draw_mesh(vis.body_cloud_meshes[i], shell_frame * glm::mat4_cast(glm::quat(universe.orientation(static_cast<int>(i)) * sphere_flip)) * glm::scale(glm::mat4(1.0f), glm::vec3(k)), weather);
                }
                draw_atmosphere_glow(rel, b);
                if (vis.rings[i])
                {
                    // Rings are lit from one side or the other; edge-on they thin to a line.
                    const glm::vec3 axis = glm::normalize(glm::vec3(frame * glm::vec4(0, 1, 0, 0)));
                    sl::Material ring;
                    ring.lit = false;
                    ring.fog = false;
                    ring.cull = sl::CullMode::none;
                    ring.blend = sl::MaterialBlend::alpha;
                    ring.tint.alpha = static_cast<std::uint8_t>(255.0f * (0.45f + 0.55f * std::fabs(glm::dot(axis, to_sun))));
                    sl::draw_mesh(vis.rings[i], frame * glm::scale(glm::mat4(1.0f), glm::vec3(static_cast<float>(b.ring_outer))), ring);
                }

                if (b.kind == kosmos::BodyKind::comet)
                {
                    // The tail streams away from the Sun and grows as the comet nears it.
                    const sl::dvec3 from_sun = universe.position(static_cast<int>(i)) - universe.position(0);
                    const double sun_au = glm::length(from_sun) / 1.495978707e11;
                    const double activity = fx::smoothstep(6.0, 0.6, sun_au);
                    if (activity > 0.01)
                    {
                        const glm::vec3 dir = glm::vec3(glm::normalize(from_sun));
                        const glm::vec3 x_axis = glm::normalize(glm::cross(std::fabs(dir.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0), dir));
                        const glm::mat4 basis(glm::vec4(x_axis, 0.0f), glm::vec4(glm::cross(dir, x_axis), 0.0f), glm::vec4(dir, 0.0f), glm::vec4(0, 0, 0, 1));
                        sl::Material tail;
                        tail.lit = false;
                        tail.fog = false;
                        tail.cull = sl::CullMode::none;
                        tail.blend = sl::MaterialBlend::additive;
                        tail.tint.alpha = static_cast<std::uint8_t>(80.0 * activity);
                        const float length = static_cast<float>(1.0e6 + activity * 9.0e8);
                        for (const float width : {1.0f, 0.62f, 0.36f, 0.16f})
                            sl::draw_mesh(vis.tail, glm::translate(glm::mat4(1.0f), glm::vec3(rel)) * basis * glm::scale(glm::mat4(1.0f), glm::vec3(length * width, length * width, length)), tail);
                    }
                }
            }

            if (near_earth && !options.no_clouds && gfx_clouds)
            {
                // From space the terrain hides the sky's clouds, so the weather is a translucent shell lifted as depth coarsens.
                sl::Material cloud;
                cloud.texture = home_cloud_texture ? home_cloud_texture : vis.cloud_texture;
                if (home_cloud_texture)
                    cloud.tint = {static_cast<std::uint8_t>(earth_body->cloud_colour[0] * 255.0f), static_cast<std::uint8_t>(earth_body->cloud_colour[1] * 255.0f), static_cast<std::uint8_t>(earth_body->cloud_colour[2] * 255.0f), 255};
                cloud.blend = sl::MaterialBlend::alpha;
                cloud.fog = false;
                const double altitude_now = eye_earth_distance - earth_body->radius;
                const double resolution = altitude_now * altitude_now / (near_plane * 1.68e7);
                double shell_height = earth_as_sphere ? home_cloud_height : std::clamp(2.0 * resolution, home_cloud_height, 600.0e3);
                // A shell nearer the eye than the near plane is sliced off along a line across the view, so keep it beyond that plane.
                if (layered)
                    shell_height = std::min(shell_height, std::max(1.0e3, altitude_now - 1.05 * near_plane));
                const float shell_scale = static_cast<float>((earth_body->radius + shell_height) / (earth_body->radius + home_cloud_height));
                sl::draw_mesh(cloud_shell, glm::translate(glm::mat4(1.0f), glm::vec3(earth_pos - eye_world)) * glm::scale(glm::mat4(1.0f), glm::vec3(shell_scale)), cloud);
            }

            if (earth_id >= 0 && near_earth)
                draw_atmosphere_glow(universe.position(earth_id) - eye_world, universe.bodies[static_cast<std::size_t>(earth_id)]);
            sl::set_light_3d(light);

            if (layered)
            {
                if (sl::clear_depth_3d())
                    sl::set_camera_3d(ship_camera);
                else
                    depth_layers_work = false;
            }

            // Ships and their jettisoned stages.
            for (int s = 0; s < sim.ship_count(); ++s)
            {
                Ship &other = *sim.ship(s);
                const sl::FlightBody &ob = other.body();
                const glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(other.origin() - eye_world)) *
                                        glm::mat4(glm::mat3_cast(glm::quat(ob.orientation)));
                if (!((in_cockpit || in_interior) && &other == ship))
                    draw_parts(vis, other.spec(), other.active_stage(), other.stage_count() - 1, model, true, other.gear_position(), other.pod_angle(false), other.pod_angle(true), &other);
                else if (in_cockpit3d)
                {
                    const kosmos::Cockpit &cp = other.spec().cockpit;
                    const glm::mat3 basis = cockpit_screen_basis();
                    const glm::mat3 frame = interface_frame(other.spec());
                    sl::Material console;
                    console.tint = {38, 40, 46, 255};
                    console.cull = sl::CullMode::none;
                    if (!cp.instruments.empty() || !cp.controls.empty())
                    {
                        const glm::vec3 centre = glm::vec3(cp.eye) + frame * glm::vec3(cp.panel_pos);
                        const float pw = static_cast<float>(cp.panel_size), ph = pw * cp.panel_height / cp.panel_width;
                        const glm::mat3 pb = frame * panel_basis(cp);
                        // The console is a slab just behind the board.
                        sl::draw_mesh(vis.cockpit_box, model * glm::translate(glm::mat4(1.0f), centre - pb[2] * 0.1f) * glm::mat4(pb) * glm::scale(glm::mat4(1.0f), glm::vec3(pw * 1.04f, ph * 1.06f, 0.18f)), console);
                        if (panel_target)
                        {
                            sl::Material board;
                            board.texture = panel_target;
                            board.lit = false;
                            board.fog = false;
                            board.cull = sl::CullMode::none;
                            sl::draw_mesh(vis.screen_quad, model * glm::translate(glm::mat4(1.0f), centre) * glm::mat4(pb) * glm::scale(glm::mat4(1.0f), glm::vec3(pw, ph, 1.0f)), board);
                        }
                    }
                    else
                    {
                    sl::draw_mesh(vis.cockpit_box, model * glm::translate(glm::mat4(1.0f), glm::vec3(cp.eye) + glm::vec3(0.0f, -0.36f, -0.8f)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.8f, 0.3f, 0.5f)), console);
                    for (int side = 0; side < 2; ++side)
                    {
                        const CockpitScreen cs = cockpit_screen(cp, side);
                        sl::Material screen;
                        screen.texture = mfd_target[side];
                        screen.lit = false;
                        screen.fog = false;
                        screen.cull = sl::CullMode::none;
                        sl::draw_mesh(vis.screen_quad, model * glm::translate(glm::mat4(1.0f), cs.centre) * glm::mat4(basis) * glm::scale(glm::mat4(1.0f), glm::vec3(cs.width * 1.18f, cs.height * 1.13f, 1.0f)), screen);
                    }
                    }
                }
                for (const kosmos::Debris &d : other.debris())
                {
                    const sl::FlightBody *db = universe.world.body(d.body);
                    const glm::mat4 dm = glm::translate(glm::mat4(1.0f), glm::vec3(db->position - eye_world)) *
                                         glm::mat4(glm::mat3_cast(glm::quat(db->orientation))) *
                                         glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, static_cast<float>(-d.centre_z)));
                    draw_parts(vis, other.spec(), d.stage, d.stage, dm);
                }
            }

            // Other players' ships.
            for (const kosmos::Ghost &g : netplay.ghosts())
            {
                const kosmos::ShipSpec *gspec = g.have_state ? sim.find_ship_spec(g.state.ship) : nullptr;
                if (!gspec || g.state.soi < 0 || g.state.soi >= static_cast<int>(universe.bodies.size()) || g.age > 5.0)
                    continue;
                const sl::dvec3 gpos = universe.position(g.state.soi) + g.shown_position();
                const glm::mat4 gmodel = glm::translate(glm::mat4(1.0f), glm::vec3(gpos - eye_world)) * glm::mat4(glm::mat3_cast(glm::quat(g.shown)));
                const int last = static_cast<int>(gspec->stages.size()) - 1;
                draw_parts(vis, *gspec, std::min<int>(g.state.stage, last), last, gmodel);
            }

            // Weather clouds: a layer at the reported cloud base over the flown ship, below 100 km only.
            if (props.get_bool("/sim/settings/weather", true) && props.get_bool("/sim/settings/gfx-clouds") && gfx_style == 0 && !map_mode)
            {
                const kosmos::Telemetry &wt = ship->telemetry();
                const double cover = props.get_double("/environment/cloud-cover");
                if (wt.soi >= 0 && wt.soi < static_cast<int>(universe.bodies.size()) && cover > 0.04 && wt.altitude < 100.0e3 && universe.bodies[static_cast<std::size_t>(wt.soi)].atmosphere_density > 0.0)
                {
                    const kosmos::Body &wb = universe.bodies[static_cast<std::size_t>(wt.soi)];
                    const sl::dvec3 centre_world = universe.position(wt.soi);
                    const sl::dvec3 from_centre = eye_world - centre_world;
                    const double eye_distance = glm::length(from_centre);
                    const sl::dvec3 local_up = from_centre / eye_distance;
                    const double eye_alt = eye_distance - wb.radius;
                    const double ground = eye_alt - wt.altitude;
                    const double layer = ground + std::max(150.0, props.get_double("/environment/cloud-base", 1500.0));
                    if (!vis.local_cloud)
                        vis.local_cloud = make_cloud_deck_mesh();
                    update_cloud_deck(vis.local_cloud, centre_world - eye_world, local_up, universe.orientation(wt.soi), wb.radius, layer, eye_alt, cover,
                                      props.get_double("/environment/wind-east") * universe.time(), glm::dot(local_up, sl::dvec3(to_sun)), props.get_double("/environment/rain") > 0.1);
                    sl::Material deck;
                    deck.lit = false;
                    deck.fog = false;
                    deck.cull = sl::CullMode::none;
                    deck.blend = sl::MaterialBlend::alpha;
                    sl::draw_mesh(vis.local_cloud, glm::mat4(1.0f), deck);
                }
            }

            // Falling rain in the air around the camera, below the cloud base.
            if (props.get_bool("/sim/settings/weather", true) && props.get_bool("/sim/settings/weather-rain", true) && gfx_style == 0 && !map_mode)
            {
                const kosmos::Telemetry &rt = ship->telemetry();
                const double rain = props.get_double("/environment/rain");
                if (rain > 0.02 && rt.soi >= 0 && rt.soi < static_cast<int>(universe.bodies.size()) && rt.altitude < std::max(props.get_double("/environment/cloud-base", 3000.0), 1500.0))
                {
                    const sl::dvec3 from_centre = eye_world - universe.position(rt.soi);
                    const sl::dvec3 local_up = glm::normalize(from_centre);
                    const sl::dvec3 air_v = universe.world.air_velocity_at(ship->body().position);
                    const sl::dvec3 fall = -local_up * 9.0;
                    const sl::dvec3 relative = air_v - ship->body().velocity + fall;
                    if (!vis.rain_mesh)
                        vis.rain_mesh = make_rain_mesh();
                    update_rain_mesh(vis.rain_mesh, local_up, relative, vis.rain_offset, frame, std::min(1.0, rain * 1.1), fx::smoothstep(-0.12, 0.3, glm::dot(local_up, sl::dvec3(to_sun))));
                    sl::Material drops;
                    drops.lit = false;
                    drops.fog = false;
                    drops.cull = sl::CullMode::none;
                    drops.blend = sl::MaterialBlend::alpha;
                    sl::draw_mesh(vis.rain_mesh, glm::mat4(1.0f), drops);
                }
            }

            // Lightning: the flash is drawn over the finished scene, the bolt here from the cloud base to the ground.
            lightning_flash = lightning.flash();
            {
                const kosmos::Telemetry &lt = ship->telemetry();
                const double cloud_agl = props.get_double("/environment/cloud-base", 1500.0);
                if (lt.soi >= 0 && lt.soi < static_cast<int>(universe.bodies.size()) && lt.altitude < cloud_agl && lt.altitude < 20.0e3)
                {
                    const sl::dvec3 up = glm::normalize(eye_world - universe.position(lt.soi));
                    const sl::dvec3 e1 = glm::normalize(glm::cross(std::fabs(up.y) < 0.9 ? sl::dvec3(0, 1, 0) : sl::dvec3(1, 0, 0), up));
                    const sl::dvec3 e2 = glm::cross(up, e1);
                    std::vector<sl::Vertex3D> local(static_cast<std::size_t>(kosmos::Lightning::bolt_quads) * 4);
                    const int used = lightning.bolt_vertices(-lt.altitude, cloud_agl - lt.altitude, local.data(), kosmos::Lightning::bolt_quads);
                    if (used > 0)
                    {
                        if (!vis.bolt_mesh)
                        {
                            std::vector<sl::Vertex3D> blank(local.size());
                            std::vector<std::uint32_t> indices;
                            for (int i = 0; i < kosmos::Lightning::bolt_quads; ++i)
                                for (const std::uint32_t k : {0u, 1u, 2u, 2u, 1u, 3u})
                                    indices.push_back(static_cast<std::uint32_t>(i * 4) + k);
                            vis.bolt_mesh = sl::create_mesh(blank, indices);
                        }
                        for (std::size_t i = 0; i < local.size(); ++i)
                        {
                            const glm::dvec3 p = e1 * static_cast<double>(local[i].position[0]) + e2 * static_cast<double>(local[i].position[1]) + up * static_cast<double>(local[i].position[2]);
                            for (int k = 0; k < 3; ++k)
                            {
                                local[i].position[k] = static_cast<float>(p[k]);
                                local[i].normal[k] = static_cast<float>(up[k]);
                            }
                        }
                        sl::update_mesh_vertices(vis.bolt_mesh, local);
                        sl::Material bolt;
                        bolt.lit = false;
                        bolt.fog = false;
                        bolt.cull = sl::CullMode::none;
                        bolt.blend = sl::MaterialBlend::additive;
                        sl::draw_mesh(vis.bolt_mesh, glm::mat4(1.0f), bolt);
                    }
                }
            }

            // The bulbs themselves: a small bright core with a soft halo, drawn facing the camera.
            if (!lamps.empty())
            {
                sl::Material bulb;
                bulb.texture = vis.glow_texture;
                bulb.lit = false;
                bulb.fog = false;
                bulb.cull = sl::CullMode::none;
                bulb.blend = sl::MaterialBlend::additive;
                const auto facing = [](const glm::vec3 &at, float half)
                {
                    const glm::vec3 to_eye = -glm::normalize(at);
                    const glm::vec3 right = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), to_eye));
                    const glm::vec3 up_v = glm::cross(to_eye, right);
                    return glm::mat4(glm::vec4(right * half, 0.0f), glm::vec4(to_eye, 0.0f), glm::vec4(up_v * half, 0.0f), glm::vec4(at, 1.0f));
                };
                for (const kosmos::PlacedLight &p : lamps)
                {
                    if (p.glow_size <= 0.0f || p.distance < 0.5f)
                        continue;
                    // Far lamps stay a few pixels wide so a strobe still reads from kilometres away.
                    const float size = std::max(p.glow_size, p.distance * p.far_size);
                    const glm::vec3 c = glm::clamp(p.glow_colour * 255.0f, 0.0f, 255.0f);
                    bulb.tint = {static_cast<std::uint8_t>(c.r), static_cast<std::uint8_t>(c.g), static_cast<std::uint8_t>(c.b), static_cast<std::uint8_t>(std::clamp(p.glow_strength * 230.0f, 0.0f, 255.0f))};
                    sl::draw_mesh(vis.quad, facing(p.light.position, size * 7.0f), bulb);
                    sl::draw_mesh(vis.quad, facing(p.light.position, size * 3.0f), bulb);
                    bulb.tint.alpha = static_cast<std::uint8_t>(std::clamp(p.glow_strength * 255.0f, 0.0f, 255.0f));
                    bulb.tint.red = static_cast<std::uint8_t>(std::min(255.0f, c.r * 0.5f + 128.0f));
                    bulb.tint.green = static_cast<std::uint8_t>(std::min(255.0f, c.g * 0.5f + 128.0f));
                    bulb.tint.blue = static_cast<std::uint8_t>(std::min(255.0f, c.b * 0.5f + 128.0f));
                    sl::draw_mesh(vis.quad, facing(p.light.position, size * 1.2f), bulb);
                }
            }

            // Engine and reaction-control exhaust.
            {
                const glm::mat4 hull = glm::translate(glm::mat4(1.0f), glm::vec3(ship->origin() - eye_world)) * glm::mat4(glm::mat3_cast(glm::quat(fb.orientation)));
                const double vacuum = home ? fx::smoothstep(8.0e3, 70.0e3, tm.altitude) : 1.0;
                const double com_altitude = universe.world.altitude_of(fb.position);
                const sl::dvec3 world_up = glm::normalize(fb.position - universe.world.body(tm.soi)->position);
                const glm::vec3 local_up = glm::vec3(glm::inverse(fb.orientation) * world_up);
                exhaust_fx.update(*ship, vacuum, static_cast<float>(frame), !ship->on_rails() && sim.current_warp() < 4.5, com_altitude < 150.0 && !ship->on_rails(), static_cast<float>(com_altitude), local_up);
                exhaust_fx.draw(vis.glow, hull);
            }
            arsenal.draw_world(universe, eye_world, vis.glow);
            if (in_eva && arsenal.held(sim) && sl::clear_depth_3d())
            {
                sl::Camera held_camera = ship_camera;
                held_camera.near_plane = 0.03f;
                held_camera.far_plane = 50.0f;
                sl::set_camera_3d(held_camera);
                const glm::vec3 fwd = glm::vec3(eva.forward()), upv = glm::vec3(eva.view_up());
                arsenal.draw_held(sim, glm::mat4(glm::mat3(glm::cross(fwd, upv), upv, -fwd)));
            }
            sl::end_surface_3d();
        }
        sl::clear_to_colour(sl::screen, {0, 0, 0});
        sl::draw_sprite(sl::surface_3d_bitmap(surface), 0, 0);
        if (map_mode)
            map.draw(universe, *ship, tm);

        const glm::mat4 view_proj = camera.projection_matrix(static_cast<float>(screen_w) / screen_h) * camera.view_matrix();

        // Orbit line about the sphere-of-influence body, hidden where the body covers it.
        sl::OrbitElements elements;
        const bool have_orbit = universe.world.orbit_of(ship->body_id(), soi, elements);
        if (have_orbit && show_orbit && !options.hide_info && !map_mode && view_distance > 2.0e3 && !ship->on_rails())
        {
            const std::vector<sl::dvec3> path = sl::orbit_path(soi_body.gm, elements, 360, std::min(soi_body.soi, 1.0e13));
            const float body_r = static_cast<float>(soi_body.radius);
            const glm::vec3 body_p = glm::vec3(soi_pos - eye_world);
            bool have_previous = false;
            float px = 0.0f, py = 0.0f;
            for (const sl::dvec3 &point : path)
            {
                const glm::vec3 p = glm::vec3(point + soi_pos - eye_world);
                const float ray_length = glm::length(p);
                const glm::vec3 dir = p / ray_length;
                const float along = glm::dot(body_p, dir);
                const bool hidden = along > 0.0f && along < ray_length && glm::length(body_p - dir * along) < body_r * 0.999f;
                float x = 0.0f, y = 0.0f;
                const bool visible = !hidden && project(view_proj, p, x, y) && std::fabs(x) < 1.0e4f && std::fabs(y) < 1.0e4f;
                if (visible && have_previous)
                    sl::line(sl::screen, px, py, x, y, {80, 255, 120});
                have_previous = visible;
                px = x;
                py = y;
            }
        }

        // Head-up display.
        if (hud.mode() != sl::HudMode::off && hud_allowed(hud.mode(), ship->spec().hud_modes()) && (in_cockpit || in_interior) && !map_mode && !galaxy_mode)
        {
            sl::HudState hs;
            const glm::dvec3 fwd = glm::normalize(sl::dvec3(ship_rel));
            hs.camera.forward = fwd;
            hs.camera.right = glm::normalize(glm::cross(fwd, sl::dvec3(up)));
            hs.camera.up = glm::cross(hs.camera.right, fwd);
            hs.camera.focal = screen_h * 0.5 / std::tan(glm::radians(view_fov * 0.5));
            hs.camera.width = screen_w;
            hs.camera.height = screen_h;
            hs.nose = fb.direction_to_world(sl::dvec3(0.0, 0.0, -1.0));
            hs.roof = fb.direction_to_world(sl::dvec3(0.0, 1.0, 0.0));
            const sl::FlightBody &soi_fb = *universe.world.body(soi);
            const sl::dvec3 from_soi = fb.position - soi_fb.position;
            hs.orbit.valid = true;
            hs.orbit.reference = soi_body.name;
            hs.orbit.velocity = fb.velocity - soi_fb.velocity;
            hs.orbit.position = from_soi;
            hs.surface.valid = true;
            hs.surface.reference = soi_body.name;
            hs.surface.altitude = tm.altitude;
            hs.surface.radar = tm.altitude < 1.0e4;
            hs.surface.speed = tm.surface_speed;
            hs.surface.airspeed = tm.dynamic_pressure > 0.5;
            hs.surface.ground_velocity = ship->relative_velocity();
            hs.surface.up = glm::normalize(from_soi);
            hs.surface.north = universe.pole(soi);
            if (props.get_bool("/dock/valid"))
            {
                const auto vec = [&props](const char *name) {
                    const std::string base = std::string("/dock/hud/") + name + "-";
                    return sl::dvec3(props.get_double(base + "x"), props.get_double(base + "y"), props.get_double(base + "z"));
                };
                hs.docking.valid = true;
                hs.docking.target = props.get_string("/dock/target");
                hs.docking.reference = hs.docking.target + " [" + props.get_string("/dock/own-port-name") + "]";
                hs.docking.velocity = vec("velocity");
                hs.docking.position = vec("position");
                hs.docking.has_port = true;
                hs.docking.port_position = hs.docking.position;
                hs.docking.port_direction = vec("direction");
                hs.docking.port_up = vec("up");
            }
            hud.draw(hs);
        }

        if (in_eva)
        {
            double ship_distance = 0.0;
            const int near_ship = nearest_ship(ship_distance);
            const double ship_speed = near_ship >= 0 ? glm::length(sim.ship(near_ship)->body().velocity - universe.world.body(eva.body)->velocity - eva.velocity) : 0.0;
            // Mark each building's door so it can be found, and say when Enter will go in.
            const sl::dvec3 eva_eye = eva.eye_world(universe);
            for (int s = 0; s < sim.ship_count(); ++s)
            {
                if (!sim.ship(s)->spec().building())
                    continue;
                const sl::dvec3 rel = door_world(*sim.ship(s)) - eva_eye;
                const double distance = glm::length(rel);
                if (distance > 300.0)
                    continue;
                if (s == building_at_door())
                    sl::gprintf_center(screen_h / 2 + 90, {120, 255, 150}, "Press Enter to go into %s", sim.ship(s)->name.c_str());
                float x = 0.0f, y = 0.0f;
                if (distance < 1.0 || !project(view_proj, glm::vec3(rel / distance * 1000.0), x, y) || x < 0 || x > screen_w || y < 0 || y > screen_h)
                    continue;
                sl::circle(sl::screen, x, y, 6.0f, {120, 255, 150}, 1.0f);
                sl::gprintf(static_cast<int>(x) + 10, static_cast<int>(y) - 6, {120, 255, 150}, "DOOR %s  %s", sim.ship(s)->name.c_str(), format_distance(distance).c_str());
            }
            kosmos::draw_suit_hud(eva, universe, screen_w, screen_h, near_ship >= 0 ? ship_distance : -1.0, ship_speed, near_ship >= 0 ? sim.ship(near_ship)->name.c_str() : nullptr);
        }
        arsenal.draw_hud(sim, eva, screen_w, screen_h, eye_world);

        // Labels for bodies too small on screen to recognise.
        const float focal = static_cast<float>(screen_h * 0.5 / std::tan(glm::radians(view_fov * 0.5)));
        const bool body_labels = props.get_bool("/sim/settings/labels");
        if (props.get_bool("/sim/settings/ship-labels") && !map_mode && !galaxy_mode)
            for (int s = 0; s < sim.ship_count(); ++s)
            {
                if (sim.ship(s) == ship)
                    continue;
                const sl::dvec3 rel = sim.ship(s)->body().position - eye_world;
                const double distance = glm::length(rel);
                float x = 0.0f, y = 0.0f;
                if (distance < 1.0 || !project(view_proj, glm::vec3(rel / distance * 1000.0), x, y) || x < 0 || x > screen_w || y < 0 || y > screen_h)
                    continue;
                sl::circle(sl::screen, x, y, 5.0f, {120, 255, 150}, 1.0f);
                sl::gprintf(static_cast<int>(x) + 9, static_cast<int>(y) - 6, {120, 255, 150}, "%s  %s", sim.ship(s)->name.c_str(), format_distance(distance).c_str());
            }
        if (!map_mode && !galaxy_mode)
            for (const kosmos::Ghost &g : netplay.ghosts())
            {
                if (!g.have_state || g.age > 5.0 || g.state.soi < 0 || g.state.soi >= static_cast<int>(universe.bodies.size()))
                    continue;
                const sl::dvec3 rel = universe.position(g.state.soi) + g.shown_position() - eye_world;
                const double distance = glm::length(rel);
                float x = 0.0f, y = 0.0f;
                if (distance < 1.0 || !project(view_proj, glm::vec3(rel / distance * 1000.0), x, y) || x < 0 || x > screen_w || y < 0 || y > screen_h)
                    continue;
                sl::circle(sl::screen, x, y, 6.0f, {255, 200, 90}, 1.0f);
                sl::gprintf(static_cast<int>(x) + 10, static_cast<int>(y) - 6, {255, 200, 90}, "%s  %s", g.nick.c_str(), format_distance(distance).c_str());
            }
        for (std::size_t i = 0; i < universe.bodies.size() && !map_mode && body_labels; ++i)
        {
            const kosmos::Body &b = universe.bodies[i];
            if (b.minor)
                continue;
            const sl::dvec3 rel = universe.position(static_cast<int>(i)) - eye_world;
            const double distance = glm::length(rel);
            const double pixel_radius = b.radius / distance * focal;
            if (pixel_radius > 5.0 || distance < 1.0)
                continue;
            float x = 0.0f, y = 0.0f;
            if (!project(view_proj, glm::vec3(rel / distance * 1000.0), x, y) || x < 0 || x > screen_w || y < 0 || y > screen_h)
                continue;
            sl::circle(sl::screen, x, y, 6.0f, {200, 220, 255}, 1.0f);
            sl::gprintf(static_cast<int>(x) + 10, static_cast<int>(y) - 6, {200, 220, 255}, "%s", b.name.c_str());
        }

        // Sound.
        double rocket_level = 0.0;
        for (const sl::Thruster &t : fb.thrusters)
            if (t.max_thrust > 0.0 && t.level > 0.0 && t.tank >= 0 && fb.tanks[static_cast<std::size_t>(t.tank)].mass > 0.0)
                rocket_level = std::max(rocket_level, t.level);
        const double air = home ? 1.0 - fx::smoothstep(20.0e3, 90.0e3, tm.altitude) : 0.0;
        // Wind: a calm breeze near the ground, rising with airspeed, and drowned out by the engines.
        const double near_ground = home ? 1.0 - fx::smoothstep(200.0, 3000.0, tm.altitude) : 0.0;
        const double wind_level = air * std::max(0.45 * near_ground, std::min(1.0, tm.surface_speed / 200.0)) * (1.0 - std::min(1.0, rocket_level * 2.0));
        double rcs_level = 0.0;
        for (const sl::Thruster &t : fb.thrusters)
            if (t.max_thrust > 0.0 && t.level > 0.0 && t.tank >= 0 && t.max_thrust < 4000.0 && fb.tanks[static_cast<std::size_t>(t.tank)].mass > 0.0)
                rcs_level = std::max(rcs_level, t.level);
        sound.data_root = sim.root();
        sound.set(sound_on ? rocket_level * (0.35 + 0.65 * air) : 0.0, sound_on ? rcs_level : 0.0, sound_on ? wind_level : 0.0);
        {
            // Event cues: one-shots on the edge of a state change. The first frame only records the state.
            static bool seen = false, was_landed = false, was_crashed = false, was_docked = false, was_alarm = false;
            static int was_stage = 0;
            static double alarm_next = 0.0;
            const bool landed_now = tm.landed, crashed_now = ship->crashed(), docked_now = props.get_bool("/dock/docked");
            const bool alarm_now = ship->systems() && !ship->systems()->alarms().empty();
            if (seen && sound_on)
            {
                if (crashed_now && !was_crashed)
                    sound.play("crash", 255);
                else if (landed_now && !was_landed && !crashed_now)
                    sound.play("touchdown", 220);
                if (docked_now != was_docked)
                    sound.play(docked_now ? "docking" : "undocking", 200);
                if (ship->active_stage() != was_stage)
                    sound.play("thump", 230);
                if (alarm_now && (!was_alarm || sl::time_ms() / 1000.0 > alarm_next))
                {
                    sound.play("warning_beep", 160);
                    alarm_next = sl::time_ms() / 1000.0 + 4.0;
                }
            }
            seen = true;
            was_landed = landed_now;
            was_crashed = crashed_now;
            was_docked = docked_now;
            was_alarm = alarm_now;
            was_stage = ship->active_stage();
        }

        // HUD.
        const std::string soi_name = soi_body.name;
        if (show_info)
            sl::gprintf(16, 12, {240, 240, 240}, "KOSMOS   %s   T+%s   %s", ship->name.c_str(), format_time(universe.time()).c_str(), kosmos::format_utc(universe.julian_date()).c_str());
        if (ship->crashed())
            sl::gprintf_center(screen_h / 2 - 20, {255, 90, 80}, ship->systems() && ship->systems()->crew_lost() ? "*** CREW LOST - press R to reset ***" : "*** VESSEL LOST - press R to reset ***");
        else if (ship->systems() && !ship->systems()->alarms().empty() && ((sl::time_ms() / 450) % 4 != 0 || ship->systems()->mode() == kosmos::LifeSupportMode::off))
            sl::gprintf_center(96, {255, 80, 70}, "!! %s !!", ship->systems()->alarm_text().c_str());
        if (show_info)
        {
            sl::gprintf(16, 34, {200, 220, 255}, "%s  Alt %s   Speed %.1f m/s   Vertical %.1f m/s", soi_name.c_str(), format_distance(tm.altitude).c_str(), tm.surface_speed, tm.vertical_speed);
            if (ship->spec().aircraft())
                sl::gprintf(16, 52, {120, 255, 150}, "Climb %.1f m/s   Flight path %.1f deg   AoA %.1f deg   Slip %.1f deg   Q %.0f Pa   Flaps %.0f%%%s%s", tm.vertical_speed, tm.flight_path,
                            tm.alpha, tm.sideslip, tm.dynamic_pressure, props.get_double("/controls/flaps") * 100.0, props.get_double("/controls/brake") > 0.5 ? "   BRAKES" : "",
                            !ship->spec().retractable_gear ? "" : ship->gear_position() >= 0.999 ? "   GEAR DOWN" : ship->gear_position() <= 0.001 ? "   GEAR UP" : "   GEAR MOVING");
            else if (tm.bound)
                sl::gprintf(16, 52, {120, 255, 150}, "Apoapsis %s   Periapsis %s   Period %.1f min   Ecc %.3f", format_distance(tm.apoapsis).c_str(),
                            format_distance(tm.periapsis).c_str(), tm.period / 60.0, tm.eccentricity);
            else
                sl::gprintf(16, 52, {255, 200, 120}, "Escape trajectory   Periapsis %s", format_distance(tm.periapsis).c_str());
            if (ship->spec().has_hover())
                sl::gprintf(16, 106, {255, 220, 150}, "Hover %3.0f%%", props.get_double("/controls/hover") * 100.0);
            sl::gprintf(16, 70, {255, 220, 150}, "Throttle %3.0f%%   Thrust %.0f kN   TWR %.2f   Stage %d/%d fuel %.0f kg   Mass %.1f t", props.get_double("/controls/throttle") * 100.0,
                        tm.thrust / 1000.0, tm.twr, ship->active_stage() + 1, ship->stage_count(), tm.stage_fuel, tm.mass / 1000.0);
            sl::gprintf(16, 88, {220, 220, 220}, "Pitch %.1f  Heading %.1f   Hold: %s   Warp x%g%s", tm.pitch, tm.heading, props.get_string("/controls/sas").c_str(), sim.current_warp(),
                        ship->on_rails() ? " (rails)" : "");
            const std::string status = props.get_string("/autopilot/status");
            if (props.get_bool("/autopilot/enabled") || !status.empty())
                sl::gprintf(16, 106, {120, 220, 255}, "Autopilot: %s", status.c_str());
            int nearest = -1;
            double nearest_distance = 0.0;
            int hud_end_y = 88;
            for (std::size_t i = 0; i < sim.bases.size(); ++i)
            {
                const kosmos::Base &bs = sim.bases[i];
                const int body = universe.find(bs.body);
                if (body < 0)
                    continue;
                const double d = glm::length(universe.position(body) + bs.direction * (universe.bodies[static_cast<std::size_t>(body)].radius + bs.height) - fb.position);
                if (nearest < 0 || d < nearest_distance)
                {
                    nearest = static_cast<int>(i);
                    nearest_distance = d;
                }
            }
            hud_end_y = props.get_bool("/autopilot/enabled") || !status.empty() ? 106 : 88;
            if (nearest >= 0)
                hud_end_y = props.get_bool("/autopilot/enabled") || !status.empty() ? 124 : 106;
            if (nearest >= 0)
                sl::gprintf(16, props.get_bool("/autopilot/enabled") || !status.empty() ? 124 : 106, {190, 190, 255}, "Nearest base: %s (%s)   %s",
                            sim.bases[static_cast<std::size_t>(nearest)].name.c_str(), sim.bases[static_cast<std::size_t>(nearest)].body.c_str(), format_distance(nearest_distance).c_str());
            if (netplay.mode() != kosmos::NetPlay::Mode::none)
            {
                const int line_y = (props.get_bool("/autopilot/enabled") || !status.empty() ? 124 : 106) + (nearest >= 0 ? 18 : 0);
                std::string near_text;
                double near_d = 0.0;
                for (const kosmos::Ghost &g : netplay.ghosts())
                {
                    if (!g.have_state || g.age > 5.0 || g.state.soi < 0 || g.state.soi >= static_cast<int>(universe.bodies.size()))
                        continue;
                    const double d = glm::length(universe.position(g.state.soi) + g.shown_position() - fb.position);
                    if (near_text.empty() || d < near_d)
                    {
                        near_text = g.nick;
                        near_d = d;
                    }
                }
                hud_end_y = line_y;
                sl::gprintf(16, line_y, {255, 200, 90}, "%s%s%s", netplay.status().c_str(), near_text.empty() ? "" : "   Nearest player: ", near_text.empty() ? "" : (near_text + " " + format_distance(near_d)).c_str());
            }
            if (!map_mode && props.get_bool("/sim/settings/help-text"))
            {
                sl::gprintf(16, hud_end_y + 24, {200, 210, 220}, "%s", "[Z/X] Throttle  [Shift/Ctrl] +/-  [Space] Stage  [WASDQE] Rotate  [Arrows] RCS  [T] Hold  [P] Autopilot");
                sl::gprintf(16, hud_end_y + 42, {200, 210, 220}, "%s", "[,/.] Warp  [R-drag] Look  [ [ ] ] Zoom  [O] Orbit  [Tab] Map  [C] Cockpit/3D  [V] Ship  [F2] Cameras  [F3] Settings  [F4] Properties  [F11] Lua  [F12] Multiplayer  [J] Jump to player  [F5-F9] MFD  [F10] Scenario  [R] Reset");
            }
        }
        const std::vector<std::string> &messages = sim.messages();
        for (std::size_t i = 0; i < messages.size(); ++i)
            sl::gprintf(16, screen_h - 60 - static_cast<int>(messages.size() - i) * 16, {255, 200, 120}, "%s", messages[i].c_str());
        const bool dashboard = in_cockpit && !in_cockpit3d && ship->spec().cockpit.mfds && cockpit_mfd == 0;
        const bool panel2d = dashboard && !ship->spec().cockpit.panels_2d().empty();
        const bool cockpit_mfd_overlay = in_cockpit && !in_cockpit3d && ship->spec().cockpit.mfds && cockpit_mfd != 0;
        if (in_cockpit3d)
        {
            // Pick MFD buttons by casting the mouse ray at the two screen quads.
            const bool down = sl::mouse_button_down(sl::MouseButton::left);
            if (down && !mfd_mouse_prev)
            {
                const kosmos::Cockpit cp = resolved_cockpit(ship->spec().cockpit, props.get_int("/sim/active-ship"));
                const glm::vec3 fwd = glm::normalize(ship_rel);
                const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(up)));
                const glm::vec3 cup = glm::cross(right, fwd);
                const float ty = std::tan(glm::radians(25.0f));
                const float nx = (2.0f * sl::mouse_x() / screen_w - 1.0f) * ty * static_cast<float>(screen_w) / screen_h;
                const float ny = (1.0f - 2.0f * sl::mouse_y() / screen_h) * ty;
                const glm::vec3 dir = glm::normalize(fwd + right * nx + cup * ny);
                const glm::mat3 basis = cockpit_screen_basis();
                const glm::mat3 to_world = glm::mat3_cast(glm::quat(fb.orientation));
                const glm::vec3 origin_local = glm::transpose(to_world) * (-glm::vec3(ship->origin() - eye_world));
                const glm::vec3 dir_local = glm::transpose(to_world) * dir;
                if (!cp.instruments.empty() || !cp.controls.empty())
                {
                    const glm::mat3 frame = interface_frame(ship->spec());
                    const glm::mat3 pb = frame * panel_basis(cp);
                    const glm::vec3 centre = glm::vec3(cp.eye) + frame * glm::vec3(cp.panel_pos);
                    const float denom = glm::dot(dir_local, pb[2]);
                    const float t = std::fabs(denom) < 1e-5f ? -1.0f : glm::dot(centre - origin_local, pb[2]) / denom;
                    if (t > 0.0f)
                    {
                        const glm::vec3 hit = origin_local + dir_local * t - centre;
                        const float pw = static_cast<float>(cp.panel_size), ph = pw * cp.panel_height / cp.panel_width;
                        const float u = glm::dot(hit, pb[0]) / pw + 0.5f, v = 0.5f - glm::dot(hit, pb[1]) / ph;
                        if (u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f)
                        {
                            const float scale = static_cast<float>(panel_target_width) / cp.panel_width;
                            panel_click(sim, cp, 0.0f, 0.0f, scale, u * cp.panel_width * scale, v * cp.panel_height * scale);
                        }
                    }
                }
                else
                for (int side = 0; side < 2; ++side)
                {
                    const CockpitScreen cs = cockpit_screen(cp, side);
                    const float denom = glm::dot(dir_local, basis[2]);
                    if (std::fabs(denom) < 1e-5f)
                        continue;
                    const float t = glm::dot(cs.centre - origin_local, basis[2]) / denom;
                    if (t <= 0.0f)
                        continue;
                    const glm::vec3 hit = origin_local + dir_local * t - cs.centre;
                    const float u = glm::dot(hit, basis[0]) / (cs.width * 1.18f) + 0.5f;
                    const float v = 0.5f - glm::dot(hit, basis[1]) / (cs.height * 1.13f);
                    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
                        continue;
                    const int button = sim.mfd.hit_test(side, mfd_target_layout(), u * mfd_target_size, v * mfd_target_size);
                    if (button >= 0)
                        sim.mfd.press(side, button);
                    break;
                }
            }
            mfd_mouse_prev = down;
        }
        if (panel2d)
        {
            kosmos::Cockpit cp = ship->spec().cockpit;
            const auto panels = cp.panels_2d();
            const auto &shown = panels[static_cast<std::size_t>(std::clamp(panel_index, 0, static_cast<int>(panels.size()) - 1))];
            cp.instruments = shown.instruments;
            cp.controls = shown.controls;
            const std::string panel_root = "/ships/ship[" + std::to_string(props.get_int("/sim/active-ship")) + "]";
            for (kosmos::Cockpit::PanelControl &pc : cp.controls)
                if (const std::size_t token = pc.property.find("{ship}"); token != std::string::npos)
                    pc.property.replace(token, 6, panel_root);
            cp.panel_width = shown.width;
            cp.panel_height = shown.height;
            cp.panel_image = shown.image;
            const float scale = std::min(screen_w / cp.panel_width, screen_h * 0.55f / cp.panel_height);
            const float ox = (screen_w - cp.panel_width * scale) * 0.5f, oy = screen_h - cp.panel_height * scale;
            sim.mfd.bus_power = !sim.active_ship() || !sim.active_ship()->systems() || sim.active_ship()->systems()->instruments_powered();
            sim.mfd.update(frame);
            const bool down = sl::mouse_button_down(sl::MouseButton::left);
            if (down && !mfd_mouse_prev)
                panel_click(sim, cp, ox, oy, scale, static_cast<float>(sl::mouse_x()), static_cast<float>(sl::mouse_y()));
            else if (down && panel_slider >= 0 && panel_slider < static_cast<int>(cp.controls.size()))
                panel_slider_set(sim, cp.controls[static_cast<std::size_t>(panel_slider)], oy, scale, static_cast<float>(sl::mouse_y()));
            if (!down)
                panel_slider = -1;
            mfd_mouse_prev = down;
            draw_panel(sim, cp, cp.panel_image.empty() ? "" : (std::filesystem::path(ship->spec().directory) / cp.panel_image).string(), ox, oy, scale);
        }
        else if (dashboard)
        {
            const float size = std::min(260.0f, screen_h * 0.3f);
            const float top = screen_h - size - 100.0f;
            sl::rectfill(sl::screen, 0, top - 70.0f, static_cast<float>(screen_w), static_cast<float>(screen_h), {14, 16, 20, 235});
            sl::rect(sl::screen, 0, top - 70.0f, static_cast<float>(screen_w), static_cast<float>(screen_h), {70, 78, 90, 255}, 2.0f);
            kosmos::MfdLayout left, right;
            left.size = right.size = size;
            left.y = right.y = top;
            left.x = screen_w / 2.0f - size - 70.0f;
            right.x = screen_w / 2.0f + 70.0f;
            sim.mfd.bus_power = !sim.active_ship() || !sim.active_ship()->systems() || sim.active_ship()->systems()->instruments_powered();
            sim.mfd.update(frame);
            const bool down = sl::mouse_button_down(sl::MouseButton::left);
            sim.mfd.handle_mouse(left, right, down && !mfd_mouse_prev, sl::mouse_x(), sl::mouse_y());
            mfd_mouse_prev = down;
            sim.mfd.draw(0, left);
            sim.mfd.draw(1, right);
        }
        if (in_cockpit && !in_cockpit3d && !panel2d)
        {
            const std::string root = "/ships/ship[" + std::to_string(props.get_int("/sim/active-ship")) + "]";
            const std::vector<kosmos::Instrument> &panel = ship->spec().cockpit.panel;
            const int cell = 150;
            const int panel_y = dashboard ? static_cast<int>(screen_h - std::min(260.0f, screen_h * 0.3f) - 100.0f - 62.0f) : screen_h - 120;
            const int left = screen_w / 2 - static_cast<int>(panel.size()) * cell / 2;
            for (std::size_t i = 0; i < panel.size(); ++i)
            {
                std::string path = panel[i].property;
                const std::size_t token = path.find("{ship}");
                if (token != std::string::npos)
                    path.replace(token, 6, root);
                char value[64];
                std::snprintf(value, sizeof value, panel[i].format.c_str(), props.get_double(path) * panel[i].scale);
                const int x = left + static_cast<int>(i) * cell;
                sl::rectfill(sl::screen, x, panel_y, x + cell - 6, panel_y + 42, {10, 24, 20});
                sl::rect(sl::screen, x, panel_y, x + cell - 6, panel_y + 42, {60, 160, 110});
                sl::gprintf(x + 6, panel_y + 4, {110, 200, 150}, "%s", panel[i].label.c_str());
                sl::gprintf(x + 6, panel_y + 22, {200, 255, 220}, "%s", value);
            }
        }
        if ((cockpit_mfd_overlay ? cockpit_mfd == 1 : mfd_display > 0) && !dashboard && !in_cockpit3d && !map_mode && !galaxy_mode)
        {
            const bool hud = cockpit_mfd_overlay || mfd_display == 2;
            const float size = hud ? 300.0f : std::min(320.0f, screen_h * 0.38f);
            kosmos::MfdLayout left, right;
            left.size = right.size = size;
            left.hud = right.hud = hud;
            left.y = right.y = hud ? 140.0f : screen_h - size - 90.0f;
            left.x = hud ? 20.0f : screen_w / 2.0f - size - 70.0f;
            right.x = hud ? screen_w - size - 20.0f : screen_w / 2.0f + 70.0f;
            sim.mfd.bus_power = !sim.active_ship() || !sim.active_ship()->systems() || sim.active_ship()->systems()->instruments_powered();
            sim.mfd.update(frame);
            const bool down = sl::mouse_button_down(sl::MouseButton::left);
            if (!hud)
                sim.mfd.handle_mouse(left, right, down && !mfd_mouse_prev, sl::mouse_x(), sl::mouse_y());
            mfd_mouse_prev = down;
            sim.mfd.draw(0, left);
            sim.mfd.draw(1, right);
        }
        if (lightning_flash > 0.02 && !map_mode && !galaxy_mode)
        {
            sl::set_blend_mode(sl::BlendMode::additive);
            sl::rectfill(sl::screen, 0.0f, 0.0f, static_cast<float>(screen_w), static_cast<float>(screen_h),
                         {static_cast<std::uint8_t>(std::min(1.0, lightning_flash) * 70.0), static_cast<std::uint8_t>(std::min(1.0, lightning_flash) * 75.0), static_cast<std::uint8_t>(std::min(1.0, lightning_flash) * 95.0), 255});
            sl::set_blend_mode(sl::BlendMode::normal);
        }
        // The weather line is a notice: it shows for a few seconds when the METAR (or station) changes.
        static std::string weather_seen;
        static auto weather_until = std::chrono::steady_clock::time_point{};
        {
            const std::string key = props.get_string("/environment/station") + "|" + props.get_string("/environment/metar");
            if (key != weather_seen)
            {
                weather_seen = key;
                weather_until = std::chrono::steady_clock::now() + std::chrono::seconds(8);
            }
        }
        if (!options.hide_info && std::chrono::steady_clock::now() < weather_until && props.get_bool("/sim/settings/weather", true) && !props.get_string("/environment/station").empty())
            sl::gprintf_center(screen_h - 40, {170, 205, 235}, "Wind %03.0f/%.0f m/s  rain %.0f%%  cloud %.0f%%  vis %.1f km   %s%s",
                               props.get_double("/environment/wind-from"), props.get_double("/environment/wind-speed"), props.get_double("/environment/rain") * 100.0,
                               props.get_double("/environment/cloud-cover") * 100.0, props.get_double("/environment/visibility") / 1000.0,
                               props.get_string("/environment/station").c_str(), props.get_bool("/environment/live") ? " (live)" : "");
        if (!options.hide_info && props.get_bool("/sim/settings/clock"))
            sl::gprintf_center(screen_h - 22, {235, 235, 200}, "Warp x%g%s   %s   T+%s", sim.current_warp(), ship->on_rails() ? " (rails)" : "",
                               kosmos::format_utc(universe.julian_date()).c_str(), format_time(universe.time()).c_str());
        if (galaxy_mode)
            gview.draw(sim.galaxy, sim.system_index());
        if (sl::gui_available())
        {
            sl::gui_new_frame();
            if (systems_window)
            {
                ImGui::SetNextWindowPos(ImVec2(330.0f, 60.0f), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowBgAlpha(0.9f);
                if (ImGui::Begin("Ship systems [F1]", &systems_window, ImGuiWindowFlags_AlwaysAutoResize))
                {
                    kosmos::Systems *sys = ship->systems();
                    if (!sys)
                        ImGui::TextUnformatted("This ship has no power or life-support systems.");
                    else
                    {
                        int mode = static_cast<int>(sys->mode());
                        const char *modes[] = {"Off", "Warn", "Fatal"};
                        if (ImGui::Combo("Life support", &mode, modes, 3))
                            sys->set_mode(static_cast<kosmos::LifeSupportMode>(mode));
                        ImGui::SeparatorText("Power");
                        ImGui::Text("Supply %.0f W   Demand %.0f W   %s", sys->supply_w(), sys->demand_w(), sys->powered() ? "" : "SHORTAGE");
                        const double cap = sys->battery_capacity_wh();
                        if (cap > 0.0)
                        {
                            char label[64];
                            std::snprintf(label, sizeof label, "Battery %.0f / %.0f Wh", sys->battery_wh(), cap);
                            ImGui::ProgressBar(static_cast<float>(sys->battery_wh() / cap), ImVec2(260.0f, 0.0f), label);
                        }
                        for (const kosmos::SystemComponent &c : sys->components())
                        {
                            if (!c.alive)
                                continue;
                            if (c.type == "tank" && c.capacity > 0.0)
                            {
                                char label[96];
                                std::snprintf(label, sizeof label, "%s tank %.2f / %.0f kg", c.gas.c_str(), c.amount, c.capacity);
                                ImGui::ProgressBar(static_cast<float>(c.amount / c.capacity), ImVec2(260.0f, 0.0f), label);
                            }
                            else if (c.output_w > 0.0)
                                ImGui::Text("%s  %.0f W", c.name.c_str(), c.output_w);
                            else if ((c.type == "load" || c.type == "scrubber" || c.type == "regulator") && c.enabled)
                                ImGui::TextColored(c.powered ? ImVec4(0.7f, 1.0f, 0.7f, 1.0f) : ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s  %.0f W %s", c.name.c_str(), c.watts, c.powered ? "" : "(shed)");
                        }
                        static int selected_vessel = 0, slice_y = 5, slice_gas = 0;
                        for (int vi = 0; vi < sys->vessel_count(); ++vi)
                        {
                            const kosmos::Systems::Vessel &v = sys->vessels()[static_cast<std::size_t>(vi)];
                            ImGui::SeparatorText(v.spec.name.c_str());
                            if (!v.alive)
                            {
                                ImGui::TextUnformatted("(separated)");
                                continue;
                            }
                            ImGui::Text("%.1f kPa   O2 %.1f   CO2 %.2f kPa   crew %d   health %.0f%%  %s", v.pressure_kpa(), v.ppo2_kpa(), v.ppco2_kpa(), v.spec.crew, v.health * 100.0, v.hazard.c_str());
                            ImGui::PushID(vi);
                            if (ImGui::SmallButton("Open 5 cm2 leak"))
                                sys->breach(vi, 5.0);
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Show gas grid"))
                                selected_vessel = vi;
                            ImGui::PopID();
                        }
                        if (selected_vessel < sys->vessel_count() && sys->vessels()[static_cast<std::size_t>(selected_vessel)].alive)
                        {
                            ImGui::SeparatorText("Cabin gas grid");
                            ImGui::SliderInt("Height", &slice_y, 1, 10);
                            ImGui::RadioButton("O2", &slice_gas, 0);
                            ImGui::SameLine();
                            ImGui::RadioButton("N2", &slice_gas, 1);
                            ImGui::SameLine();
                            ImGui::RadioButton("CO2", &slice_gas, 2);
                            const kosmos::Systems::Vessel &v = sys->vessels()[static_cast<std::size_t>(selected_vessel)];
                            // Colour each cell by its density relative to the well-mixed value; CO2 pockets stand out.
                            const float mixed = static_cast<float>((slice_gas == 0 ? v.o2 : slice_gas == 1 ? v.n2 : v.co2) / 1000.0);
                            const ImVec2 origin = ImGui::GetCursorScreenPos();
                            const float cell = 14.0f;
                            ImDrawList *draw = ImGui::GetWindowDrawList();
                            for (int z = 0; z < 12; ++z)
                                for (int x = 0; x < 12; ++x)
                                {
                                    const float d = mixed > 0.0f ? sys->grid_cell(selected_vessel, x, slice_y, z, slice_gas) / mixed : 0.0f;
                                    const float t = std::min(d / 2.0f, 1.0f);
                                    const bool hull = x == 0 || z == 0 || x == 11 || z == 11;
                                    const ImU32 colour = hull ? IM_COL32(70, 70, 80, 255) : IM_COL32(static_cast<int>(40 + 215 * t), static_cast<int>(60 + 120 * (1.0f - std::abs(t * 2.0f - 1.0f))), static_cast<int>(220 - 190 * t), 255);
                                    draw->AddRectFilled(ImVec2(origin.x + x * cell, origin.y + z * cell), ImVec2(origin.x + (x + 1) * cell - 1.0f, origin.y + (z + 1) * cell - 1.0f), colour);
                                }
                            ImGui::Dummy(ImVec2(12 * cell, 12 * cell));
                        }
                    }
                }
                ImGui::End();
            }
            if (joystick_window)
            {
                ImGui::SetNextWindowPos(ImVec2(360.0f, 40.0f), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowBgAlpha(0.92f);
                if (ImGui::Begin("Joystick", &joystick_window, ImGuiWindowFlags_AlwaysAutoResize))
                {
                    ImGui::Checkbox("Joystick enabled", &joystick.enabled);
                    const int devices = joystick.device_count();
                    if (devices == 0)
                        ImGui::TextDisabled("No joystick connected");
                    for (int d = 0; d < devices; ++d)
                    {
                        if (ImGui::TreeNode(("device" + std::to_string(d)).c_str(), "%s", joystick.device_name(d).c_str()))
                        {
                            for (int a = 0; a < joystick.axis_count(d); ++a)
                                ImGui::ProgressBar(joystick.axis_value(d, a) * 0.5f + 0.5f, ImVec2(160.0f, 0.0f), ("axis " + std::to_string(a)).c_str());
                            std::string pressed;
                            for (int b = 0; b < joystick.button_count(d); ++b)
                                if (joystick.button_value(d, b))
                                    pressed += " " + std::to_string(b);
                            ImGui::Text("Buttons down:%s", pressed.c_str());
                            ImGui::TreePop();
                        }
                    }
                    for (const bool air : {true, false})
                    {
                        ImGui::PushID(air ? 1 : 2);
                        const bool current = ship->spec().aircraft() == air;
                        ImGui::SeparatorText(air ? (current ? "Aircraft profile (in use)" : "Aircraft profile") : (current ? "Spacecraft profile (in use)" : "Spacecraft profile"));
                        kosmos::JoyProfile &p = joystick.profile(air);
                        for (const std::string &action : kosmos::joy_axis_actions(air))
                        {
                            ImGui::PushID(action.c_str());
                            kosmos::AxisBinding &b = p.axes[action];
                            ImGui::Text("%-9s", action.c_str());
                            ImGui::SameLine(90.0f);
                            const bool waiting = joystick.learning() && joystick.learn_action() == action;
                            if (ImGui::Button(waiting ? "Move axis..." : b.axis < 0 ? "Bind" : ("Axis " + std::to_string(b.axis)).c_str(), ImVec2(90.0f, 0.0f)))
                                waiting ? joystick.cancel_learn() : joystick.begin_learn(air, action, true);
                            ImGui::SameLine();
                            ImGui::Checkbox("Invert", &b.invert);
                            if (action != "throttle" && action != "hover")
                            {
                                ImGui::SameLine();
                                ImGui::SetNextItemWidth(80.0f);
                                ImGui::SliderFloat("Dead", &b.deadzone, 0.0f, 0.5f, "%.2f");
                                ImGui::SameLine();
                                ImGui::SetNextItemWidth(80.0f);
                                ImGui::SliderFloat("Curve", &b.expo, 0.0f, 1.0f, "%.2f");
                            }
                            ImGui::PopID();
                        }
                        for (const std::string &action : kosmos::joy_button_actions(air))
                        {
                            ImGui::PushID(action.c_str());
                            kosmos::ButtonBinding &b = p.buttons[action];
                            ImGui::Text("%-13s", action.c_str());
                            ImGui::SameLine(120.0f);
                            const bool waiting = joystick.learning() && joystick.learn_action() == action;
                            if (ImGui::Button(waiting ? "Press..." : b.button < 0 ? "Bind" : ("Button " + std::to_string(b.button)).c_str(), ImVec2(90.0f, 0.0f)))
                                waiting ? joystick.cancel_learn() : joystick.begin_learn(air, action, false);
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Clear"))
                                b.button = -1;
                            ImGui::PopID();
                        }
                        ImGui::PopID();
                    }
                    if (ImGui::Button("Save"))
                        joystick.save();
                    ImGui::SameLine();
                    if (ImGui::Button("Reset to defaults"))
                    {
                        joystick.aircraft = kosmos::joy_default_profile(true);
                        joystick.spacecraft = kosmos::joy_default_profile(false);
                    }
                    ImGui::TextDisabled("Saved to joystick.json beside settings.json");
                }
                ImGui::End();
            }
            if (settings_window)
            {
                ImGui::SetNextWindowPos(ImVec2(330.0f, 12.0f), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowBgAlpha(0.9f);
                if (ImGui::Begin("Settings [F3]", &settings_window, ImGuiWindowFlags_AlwaysAutoResize))
                {
                    auto flag = [&](const char *label, const char *path, const char *tip)
                    {
                        bool v = props.get_bool(path);
                        if (ImGui::Checkbox(label, &v))
                            props.set_bool(path, v);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("%s", tip);
                    };
                    ImGui::SeparatorText("Display");
                    flag("Body labels", "/sim/settings/labels", "Name tags on planets and moons too small to recognise");
                    flag("Ship labels", "/sim/settings/ship-labels", "Name and distance tags on other ships");
                    flag("Orbit line", "/sim/settings/orbit-line", "Draw the flown ship's orbit (O)");
                    flag("Key help text", "/sim/settings/help-text", "The key reminder lines under the info text");
                    flag("Warp and time", "/sim/settings/clock", "Warp factor, date and elapsed time along the bottom of the screen");
                    ImGui::SeparatorText("Graphics");
                    {
                        static const char *styles[] = {"Normal", "Flat shaded", "Wireframe"};
                        int style = props.get_int("/sim/settings/gfx-style");
                        ImGui::SetNextItemWidth(160.0f);
                        if (ImGui::Combo("Render style", &style, styles, 3))
                            props.set_int("/sim/settings/gfx-style", style);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Flat shaded and wireframe skip textures, clouds and haze: much lighter on slow or software graphics.");
                        flag("Clouds", "/sim/settings/gfx-clouds", "Cloud layers");
                        flag("Weather", "/sim/settings/weather", "Wind, rain, cloud and visibility on planets with an atmosphere");
                        flag("Rain", "/sim/settings/weather-rain", "Draw falling rain");
                        {
                            const auto &presets = kosmos::Weather::presets();
                            const std::string cur = props.get_string("/sim/settings/weather-preset", "auto");
                            std::string label = cur == "auto" ? "Automatic" : cur == "custom" ? "Custom METAR" : cur;
                            for (const auto &p : presets)
                                if (cur == p.id)
                                    label = p.label;
                            ImGui::SetNextItemWidth(160.0f);
                            if (ImGui::BeginCombo("Weather preset", label.c_str()))
                            {
                                if (ImGui::Selectable("Automatic", cur == "auto"))
                                    props.set_string("/sim/settings/weather-preset", "auto");
                                for (const auto &p : presets)
                                    if (ImGui::Selectable(p.label, cur == p.id))
                                        props.set_string("/sim/settings/weather-preset", p.id);
                                if (ImGui::Selectable("Custom METAR", cur == "custom"))
                                    props.set_string("/sim/settings/weather-preset", "custom");
                                ImGui::EndCombo();
                            }
                            if (cur == "custom")
                            {
                                static char metar[256] = "";
                                static bool loaded = false;
                                if (!loaded)
                                {
                                    std::snprintf(metar, sizeof metar, "%s", props.get_string("/sim/settings/weather-metar", "").c_str());
                                    loaded = true;
                                }
                                ImGui::SetNextItemWidth(360.0f);
                                if (ImGui::InputText("METAR", metar, sizeof metar))
                                    props.set_string("/sim/settings/weather-metar", metar);
                                if (!props.get_bool("/environment/metar-valid", true))
                                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "Not a valid METAR (needs station and time, e.g. KOSM 011200Z 27015G25KT 3SM TS BKN010)");
                            }
                        }
                        {
                            const bool live_now = props.get_string("/sim/settings/weather-source", "procedural") == "live";
                            if (ImGui::RadioButton("Procedural weather", !live_now))
                                props.set_string("/sim/settings/weather-source", "procedural");
                            ImGui::SameLine();
                            if (ImGui::RadioButton("Live (Earth, NOAA)", live_now))
                                props.set_string("/sim/settings/weather-source", "live");
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("Real METARs from aviationweather.gov, fetched at most hourly. Used only while the simulated date is within three hours of the real one.");
                            if (live_now)
                                ImGui::TextDisabled("%s", sim.weather.live_status().c_str());
                        }
                        int fps = props.get_int("/sim/settings/gfx-max-fps");
                        ImGui::SetNextItemWidth(160.0f);
                        if (ImGui::SliderInt("Max frame rate", &fps, 10, 240))
                            props.set_int("/sim/settings/gfx-max-fps", fps);
                        int radius = props.get_int("/sim/settings/gfx-light-radius");
                        ImGui::SetNextItemWidth(160.0f);
                        if (ImGui::SliderInt("Base light radius (m)", &radius, 500, 20000))
                            props.set_int("/sim/settings/gfx-light-radius", radius);
                        static const char *details[] = {"Ultra", "High", "Medium", "Low"};
                        int detail = props.get_int("/sim/settings/gfx-terrain-detail");
                        ImGui::SetNextItemWidth(160.0f);
                        if (ImGui::Combo("Terrain detail", &detail, details, 4))
                            props.set_int("/sim/settings/gfx-terrain-detail", detail);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Applies the next time a world's terrain is built (restart).");
                        bool clutter_enabled = props.get_bool("/sim/settings/gfx-clutter", true);
                        if (ImGui::Checkbox("Trees, buildings and rocks", &clutter_enabled))
                            props.set_bool("/sim/settings/gfx-clutter", clutter_enabled);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Scatter over the ground near you: trees, shrubs and houses on Earth (from its land cover), boulders on rocky worlds, and outposts on airless ones.");
                        if (clutter_enabled)
                        {
                            int clutter_density = props.get_int("/sim/settings/gfx-clutter-density", 100);
                            ImGui::SetNextItemWidth(160.0f);
                            if (ImGui::SliderInt("Scatter density (%)", &clutter_density, 10, 200))
                                props.set_int("/sim/settings/gfx-clutter-density", clutter_density);
                        }
                        bool relief = props.get_bool("/sim/settings/gfx-earth-relief");
                        if (ImGui::Checkbox("Earth relief from orbit", &relief))
                            props.set_bool("/sim/settings/gfx-earth-relief", relief);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Terrain relief and sea glint from 300 km down, instead of the tiled sphere to 20 km. Slower.");
                    }
                    ImGui::SeparatorText("Sound");
                    flag("Sound on", "/sim/settings/sound", "Engine sound (M)");
                    int volume = props.get_int("/sim/settings/volume");
                    if (ImGui::SliderInt("Volume", &volume, 0, 100, "%d%%"))
                        props.set_int("/sim/settings/volume", volume);
                    ImGui::SeparatorText("Generated worlds");
                    {
                        static const int widths[] = {0, 2048, 4096, 8192, 16384};
                        static const char *names[] = {"Automatic", "2048 (small)", "4096", "8192 (large)", "16384 (huge)"};
                        int current = 0;
                        const int saved = props.get_int("/sim/settings/world-texture");
                        for (int w = 0; w < 5; ++w)
                            if (widths[w] == saved)
                                current = w;
                        ImGui::SetNextItemWidth(160.0f);
                        if (ImGui::Combo("World texture", &current, names, 5))
                        {
                            props.set_int("/sim/settings/world-texture", widths[current]);
                        }
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Detail of the far-view image of invented worlds, applied as soon as it is made.\nEach size is made once and kept on disk: 4096 is about 35 MB, 8192 about 135 MB, 16384 about 540 MB per world,\nand needs that much video memory to show.");
                    }
                    ImGui::SeparatorText("Simulation");
                    int warp = props.get_int("/sim/warp-index");
                    if (ImGui::SliderInt("Time warp", &warp, 0, Sim::warp_levels - 1))
                        props.set_int("/sim/warp-index", warp);
                    ImGui::TextDisabled("Settings are saved when you exit.");
                }
                ImGui::End();
            }
            menu_bar.draw_windows(sim, menu_actions);
            if (part_window)
            {
                ImGui::SetNextWindowPos(ImVec2(330.0f, 80.0f), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowSize(ImVec2(520.0f, 360.0f), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowBgAlpha(0.92f);
                if (ImGui::Begin("Part editor", &part_window))
                {
                    kosmos::Ship &flown = *sim.active_ship();
                    kosmos::ShipSpec &fspec = flown.editable_spec();
                    if (blender_buffer[0] == '\0')
                        copy_text(blender_buffer, sizeof blender_buffer, props.get_string("/sim/settings/blender", "blender"));
                    ImGui::SetNextItemWidth(260.0f);
                    if (ImGui::InputText("Blender", blender_buffer, sizeof blender_buffer))
                        props.set_string("/sim/settings/blender", blender_buffer);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("The Blender executable. Saved with the settings; --blender PATH overrides it.");
                    // Stays after Blender closes, so the converted mesh can still be written into the ship file.
                    if (part_edit.converted && !part_edit.model.empty())
                    {
                        ImGui::Separator();
                        ImGui::Text("Converted: %s", part_edit.model.c_str());
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Use in ship.json"))
                        {
                            std::string error;
                            part_status = kosmos::write_part_model(fspec.source_file, part_edit.stage, part_edit.index, part_edit.model, error)
                                              ? "ship.json now uses " + part_edit.model + " (original kept as ship.json.bak); effective on the next launch"
                                              : "Not written: " + error;
                        }
                        ImGui::Separator();
                    }
                    if (edit_paused)
                    {
                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Editing %s in Blender", part_edit.file.filename().string().c_str());
                        ImGui::TextWrapped("The simulation is paused. Save in Blender (Ctrl+S) to see the change here; closing Blender resumes.");
                        if (ImGui::Button("Resume, keep Blender open"))
                        {
                            edit_paused = false;
                            props.set_bool("/sim/blender-edit", false);
                            props.set_bool("/sim/paused", edit_was_paused);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Cancel and restore"))
                        {
                            blender_edit.stop();
                            if (part_edit.converted && part_edit.part)
                                *part_edit.part = part_edit.original;
                            blender_edit.restore();
                            part_edit.model.clear();
                            part_edit.converted = false;
                            edit_paused = false;
                            props.set_bool("/sim/blender-edit", false);
                            props.set_bool("/sim/paused", edit_was_paused);
                            part_status = "Edit cancelled; the original was restored";
                        }
                    }
                    else
                    {
                        ImGui::SeparatorText("Meshes of the flown ship");
                        int stage_index = 0;
                        for (kosmos::StageSpec &stage : fspec.stages)
                        {
                            int part_index = 0;
                            for (kosmos::PartSpec &part : stage.parts)
                            {
                                ImGui::PushID(&part);
                                const bool is_model = !part.model.empty();
                                const std::string what = is_model ? std::filesystem::path(part.model).filename().string() : (part.prop.empty() ? std::string("profile") : part.prop);
                                ImGui::Text("%s: %s", stage.name.c_str(), what.c_str());
                                ImGui::SameLine(ImGui::GetWindowWidth() - 150.0f);
                                if (ImGui::SmallButton(is_model ? "Edit in Blender" : "Convert + edit") && !netplay.shared())
                                {
                                    std::filesystem::path file;
                                    std::string error;
                                    bool ok = true;
                                    kosmos::PartSpec saved = part;
                                    if (is_model)
                                    {
                                        file = std::filesystem::path(fspec.directory) / part.model;
                                        std::string ext = file.extension().string();
                                        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                                        if (ext != ".glb" && ext != ".gltf" && ext != ".obj")
                                        {
                                            error = "Blender can edit .glb, .gltf and .obj parts; convert " + ext + " first";
                                            ok = false;
                                        }
                                    }
                                    else
                                    {
                                        sl::MeshData data;
                                        ok = part.prop.empty() ? (data = make_lathe_data(part), !data.indices.empty()) : make_prop_data(part, data);
                                        if (!ok)
                                            error = "this part has no geometry to export";
                                        else
                                        {
                                            std::string slug = fspec.name + "_" + std::to_string(stage_index) + "_" + std::to_string(part_index);
                                            for (char &c : slug)
                                                if (!std::isalnum(static_cast<unsigned char>(c)))
                                                    c = '_';
                                            file = std::filesystem::path(fspec.directory) / "models" / (slug + ".glb");
                                            std::error_code ec;
                                            std::filesystem::create_directories(file.parent_path(), ec);
                                            // An earlier conversion may hold edits: use it rather than rebuild the shape.
                                            if (!std::filesystem::exists(file, ec))
                                                ok = kosmos::write_glb(data, file, error);
                                            else
                                                part_status = "Reusing the existing " + file.filename().string();
                                        }
                                    }
                                    if (ok && props.get_bool("/sim/blender-edit"))
                                    {
                                        ok = false;
                                        part_status = "Another Blender edit is already running";
                                    }
                                    if (ok && blender_edit.start(props.get_string("/sim/settings/blender", "blender"), std::filesystem::path(sim.root()) / "data" / "blender" / "kosmos_edit.py", file, error))
                                    {
                                        part_edit.part = &part;
                                        part_edit.original = saved;
                                        part_edit.converted = !is_model;
                                        part_edit.file = file;
                                        part_edit.stage = stage_index;
                                        part_edit.index = part_index;
                                        part_edit.model.clear();
                                        if (!is_model)
                                        {
                                            part.model = std::filesystem::relative(file, fspec.directory).generic_string();
                                            part.prop.clear();
                                            part_edit.model = part.model;
                                            const std::string snippet = "{\"model\": \"" + part.model + "\", \"offset\": [" + std::to_string(part.offset.x) + ", " + std::to_string(part.offset.y) + ", " + std::to_string(part.offset.z) + "]}";
                                            ImGui::SetClipboardText(snippet.c_str());
                                            sim.log("Copied part entry for ship.json: " + snippet);
                                        }
                                        edit_was_paused = props.get_bool("/sim/paused");
                                        props.set_bool("/sim/paused", true);
                                        props.set_bool("/sim/blender-edit", true);
                                        edit_paused = true;
                                        part_status = part_status.rfind("Reusing", 0) == 0 ? part_status + "; Blender started" : "Blender started";
                                    }
                                    else if (!error.empty())
                                        part_status = error;
                                }
                                ImGui::PopID();
                                ++part_index;
                            }
                            ++stage_index;
                        }
                        if (fspec.stages.empty())
                            ImGui::TextDisabled("This ship has no parts.");
                    }
                    if (!part_status.empty())
                        ImGui::TextWrapped("%s", part_status.c_str());
                }
                ImGui::End();
            }
            if (lua_window)
                lua_console.draw(lua_window);
            if (net_window)
                draw_multiplayer_window(netplay, net_ui, net_window, sim.system_index(), [&](const kosmos::Ghost &g)
                                        {
                                            if (snap_to_ghost(sim, *ship, g, terrain_set))
                                                sim.log("Moved next to " + g.nick);
                                        });
            if (property_window)
                draw_property_browser(props, property_window, property_browser);
            if (quantum_window)
            {
                // The quantum drive: pick a star within range and arrive 1 AU above its surface.
                double range_ly = 0.0;
                for (const kosmos::StageSpec &st : ship->spec().stages)
                    for (const kosmos::EngineSpec &e : st.engines)
                        if (e.type == kosmos::EngineType::hyperjump)
                            range_ly = std::max(range_ly, e.range_ly);
                ImGui::SetNextWindowPos(ImVec2(330.0f, 120.0f), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowSize(ImVec2(420.0f, 400.0f), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowBgAlpha(0.9f);
                if (ImGui::Begin("Quantum navigation", &quantum_window))
                {
                    ImGui::Text("Ship: %s", ship->spec().name.c_str());
                    ImGui::Text("Position: %s", sim.galaxy.star(sim.system_index()).name.c_str());
                    if (range_ly <= 0.0)
                        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f), "This ship has no quantum jump drive.");
                    else
                    {
                        ImGui::Text("Drive range: %.0f light years", range_ly);
                        ImGui::Separator();
                        const std::vector<int> near = sim.galaxy.nearest(sim.system_index(), 64);
                        if (ImGui::BeginChild("targets", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() * 3.0f)))
                        {
                            if (ImGui::BeginTable("stars", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
                            {
                                ImGui::TableSetupColumn("Star");
                                ImGui::TableSetupColumn("Class");
                                ImGui::TableSetupColumn("Planets");
                                ImGui::TableSetupColumn("Distance");
                                ImGui::TableHeadersRow();
                                for (const int id : near)
                                {
                                    const double d = sim.galaxy.distance_ly(sim.system_index(), id);
                                    if (d > range_ly)
                                        break;
                                    const kosmos::Star &st = sim.galaxy.star(id);
                                    ImGui::TableNextRow();
                                    ImGui::TableNextColumn();
                                    if (ImGui::Selectable((st.name + "##" + std::to_string(id)).c_str(), quantum_selected == id, ImGuiSelectableFlags_SpanAllColumns))
                                    {
                                        quantum_selected = id;
                                        quantum_body.clear();
                                    }
                                    ImGui::TableNextColumn();
                                    ImGui::Text("%c", st.spectral_class);
                                    ImGui::TableNextColumn();
                                    ImGui::Text("%d", sim.galaxy.planet_count(id));
                                    ImGui::TableNextColumn();
                                    ImGui::Text("%.2f ly", d);
                                }
                                ImGui::EndTable();
                            }
                        }
                        ImGui::EndChild();
                        const bool ok = quantum_selected >= 0 && quantum_selected != sim.system_index() && sim.galaxy.distance_ly(sim.system_index(), quantum_selected) <= range_ly;
                        if (quantum_selected >= 0)
                        {
                            const kosmos::Star &st = sim.galaxy.star(quantum_selected);
                            ImGui::Text("%s: %.1f solar masses, %.2f solar radii, %.0f K", st.name.c_str(), st.mass, st.radius, st.temperature);
                        }
                        else
                            ImGui::TextDisabled("Select a target star.");
                        if (quantum_selected >= 0 && quantum_bodies_for != quantum_selected)
                        {
                            // The planets and moons of the chosen system, read from its description without building it.
                            quantum_bodies.clear();
                            quantum_bodies_for = quantum_selected;
                            std::string text = quantum_selected == 0 ? std::string{} : sim.galaxy.system_json(quantum_selected);
                            if (quantum_selected == 0)
                            {
                                std::ifstream in(sim.root() + "/data/sol.json");
                                text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
                            }
                            const nlohmann::json system = nlohmann::json::parse(text, nullptr, false);
                            if (system.is_object() && system.contains("bodies"))
                                for (const nlohmann::json &b : system["bodies"])
                                {
                                    const std::string kind = b.value("kind", "");
                                    if (kind == "planet" || kind == "moon")
                                        quantum_bodies.emplace_back(b.value("name", ""), (kind == "moon" ? "moon of " : "planet of ") + b.value("parent", ""));
                                }
                        }
                        if (quantum_selected >= 0)
                        {
                            const std::string label = quantum_body.empty() ? "Best world (habitable first)" : quantum_body;
                            ImGui::SetNextItemWidth(-1.0f);
                            if (ImGui::BeginCombo("Arrive at", label.c_str()))
                            {
                                if (ImGui::Selectable("Best world (habitable first)", quantum_body.empty()))
                                    quantum_body.clear();
                                for (const auto &entry : quantum_bodies)
                                    if (ImGui::Selectable((entry.first + "  (" + entry.second + ")").c_str(), quantum_body == entry.first))
                                        quantum_body = entry.first;
                                ImGui::EndCombo();
                            }
                        }
                        if (!ok)
                            ImGui::BeginDisabled();
                        if (ImGui::Button("Engage quantum jump"))
                        {
                            jump_to = quantum_selected;
                            jump_to_star_orbit = true;
                            jump_to_body = quantum_body;
                        }
                        if (!ok)
                            ImGui::EndDisabled();
                        ImGui::SameLine();
                        ImGui::TextDisabled("Arrives in orbit of the chosen world.");
                    }
                }
                ImGui::End();
            }
            if (camera_editor_window)
            {
                ImGui::SetNextWindowPos(ImVec2(330.0f, 120.0f), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowSize(ImVec2(380.0f, 520.0f), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowBgAlpha(0.9f);
                if (ImGui::Begin("Camera editor", &camera_editor_window))
                {
                    bool changed = false;
                    const int count = static_cast<int>(user_cams.size());
                    camera_editor_pick = std::clamp(camera_editor_pick, 0, std::max(0, count - 1));
                    if (ImGui::Button("Add orbit"))
                    {
                        kosmos::UserCamera c;
                        c.name = "Orbit " + std::to_string(count + 1);
                        user_cams.push_back(c);
                        camera_editor_pick = count;
                        changed = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Add mounted"))
                    {
                        kosmos::UserCamera c;
                        c.name = "Mounted " + std::to_string(count + 1);
                        c.orbit = false;
                        c.y = 2.0;
                        user_cams.push_back(c);
                        camera_editor_pick = count;
                        changed = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Copy") && count > 0)
                    {
                        kosmos::UserCamera c = user_cams[static_cast<std::size_t>(camera_editor_pick)];
                        c.name += " copy";
                        user_cams.push_back(c);
                        camera_editor_pick = count;
                        changed = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Delete") && count > 0)
                    {
                        user_cams.erase(user_cams.begin() + camera_editor_pick);
                        if (user_cam == camera_editor_pick)
                            user_cam = -1;
                        else if (user_cam > camera_editor_pick)
                            --user_cam;
                        changed = true;
                    }
                    if (ImGui::BeginListBox("##cams", ImVec2(-FLT_MIN, 100.0f)))
                    {
                        for (int ui = 0; ui < static_cast<int>(user_cams.size()); ++ui)
                        {
                            ImGui::PushID(ui);
                            if (ImGui::Selectable((user_cams[static_cast<std::size_t>(ui)].name + (user_cam == ui ? "  (viewing)" : "")).c_str(), camera_editor_pick == ui))
                                camera_editor_pick = ui;
                            ImGui::PopID();
                        }
                        ImGui::EndListBox();
                    }
                    if (!user_cams.empty())
                    {
                        const int ui = camera_editor_pick;
                        kosmos::UserCamera &uc = user_cams[static_cast<std::size_t>(ui)];
                        if (ImGui::Button(user_cam == ui ? "Back to the ship view" : "Look through"))
                        {
                            user_cam = user_cam == ui ? -1 : ui;
                            cockpit_view = cockpit3d = false;
                            interior_cam = base_view = -1;
                            map_mode = galaxy_mode = false;
                            look_yaw = look_pitch = 0.0f;
                        }
                        ImGui::Separator();
                        char name[64];
                        std::snprintf(name, sizeof(name), "%s", uc.name.c_str());
                        if (ImGui::InputText("Name", name, sizeof(name)))
                            uc.name = name, changed = true;
                        int type = uc.orbit ? 0 : 1;
                        if (ImGui::RadioButton("Orbits it, always looking at it", type == 0))
                            uc.orbit = true, changed = true;
                        if (ImGui::RadioButton("Mounted on it", type == 1))
                            uc.orbit = false, changed = true;

                        // A combo of the ships, or of the bodies, by name.
                        auto pick_name = [&](const char *label, int kind, std::string &value)
                        {
                            const bool bodies = kind == kosmos::UserCamera::body;
                            if (ImGui::BeginCombo(label, value.empty() ? "(choose)" : value.c_str()))
                            {
                                if (bodies)
                                    for (const kosmos::Body &b : universe.bodies)
                                    {
                                        if (b.minor)
                                            continue;
                                        if (ImGui::Selectable(b.name.c_str(), b.name == value))
                                            value = b.name, changed = true;
                                    }
                                else
                                    for (int si = 0; si < sim.ship_count(); ++si)
                                        if (ImGui::Selectable(sim.ship(si)->name.c_str(), sim.ship(si)->name == value))
                                            value = sim.ship(si)->name, changed = true;
                                ImGui::EndCombo();
                            }
                        };
                        ImGui::Separator();
                        ImGui::TextUnformatted("Attached to");
                        if (ImGui::RadioButton("The ship I fly", uc.attach == kosmos::UserCamera::flown_ship))
                            uc.attach = kosmos::UserCamera::flown_ship, changed = true;
                        ImGui::SameLine();
                        if (ImGui::RadioButton("A ship", uc.attach == kosmos::UserCamera::other_ship))
                            uc.attach = kosmos::UserCamera::other_ship, changed = true;
                        ImGui::SameLine();
                        if (ImGui::RadioButton("A body", uc.attach == kosmos::UserCamera::body))
                            uc.attach = kosmos::UserCamera::body, changed = true;
                        if (uc.attach != kosmos::UserCamera::flown_ship)
                            pick_name("##attach", uc.attach, uc.attach_name);
                        ImGui::Separator();
                        if (uc.orbit)
                        {
                            double d = uc.distance;
                            if (ImGui::InputDouble("Distance (m, 0 frames it)", &d, 0.0, 0.0, "%.0f"))
                                uc.distance = std::max(0.0, d), changed = true;
                            float f = static_cast<float>(uc.yaw);
                            if (ImGui::SliderFloat("Around", &f, -180.0f, 180.0f, "%.0f deg"))
                                uc.yaw = f, changed = true;
                            f = static_cast<float>(uc.pitch);
                            if (ImGui::SliderFloat("Above", &f, -89.0f, 89.0f, "%.0f deg"))
                                uc.pitch = f, changed = true;
                            f = static_cast<float>(uc.spin);
                            if (ImGui::SliderFloat("Spin", &f, -30.0f, 30.0f, "%.1f deg/s"))
                                uc.spin = f, changed = true;
                            ImGui::TextDisabled("Right-drag orbits, [ and ] zoom.");
                        }
                        else
                        {
                            float v[3] = {static_cast<float>(uc.x), static_cast<float>(uc.y), static_cast<float>(uc.z)};
                            if (ImGui::InputFloat3("Right, up, back (m)", v))
                                uc.x = v[0], uc.y = v[1], uc.z = v[2], changed = true;
                            ImGui::TextUnformatted("Looks");
                            const char *looks[] = {"The way the object faces", "At the ship I fly", "At a ship", "At a body"};
                            int look = uc.look_at;
                            if (ImGui::Combo("##look", &look, looks, 4))
                                uc.look_at = look, changed = true;
                            if (uc.look_at == kosmos::UserCamera::look_ahead)
                            {
                                float f = static_cast<float>(uc.look_yaw);
                                if (ImGui::SliderFloat("Turned", &f, -180.0f, 180.0f, "%.0f deg"))
                                    uc.look_yaw = f, changed = true;
                                f = static_cast<float>(uc.look_pitch);
                                if (ImGui::SliderFloat("Raised", &f, -89.0f, 89.0f, "%.0f deg"))
                                    uc.look_pitch = f, changed = true;
                            }
                            else if (uc.look_at != kosmos::UserCamera::look_flown_ship)
                                pick_name("##lookname", uc.look_at == kosmos::UserCamera::look_body ? kosmos::UserCamera::body : kosmos::UserCamera::other_ship, uc.look_name);
                            ImGui::TextDisabled("On a ship: x right, y up, z back. On a body: east, up, south. Right-drag peeks around.");
                        }
                        float fov = static_cast<float>(uc.fov);
                        if (ImGui::SliderFloat("Field of view", &fov, 10.0f, 110.0f, "%.0f deg"))
                            uc.fov = fov, changed = true;
                    }
                    ImGui::Separator();
                    ImGui::TextDisabled("C steps through these after the ship's own views.");
                    if (changed)
                        kosmos::save_user_cameras(user_cam_file, user_cams);
                }
                ImGui::End();
            }
            if (camera_window)
            {
                ImGui::SetNextWindowPos(ImVec2(12.0f, 120.0f), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowSize(ImVec2(300.0f, 380.0f), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowBgAlpha(0.85f);
                if (ImGui::Begin("Cameras [F2]", &camera_window))
                {
                    const int active = props.get_int("/sim/active-ship");
                    // Picking a ship's camera also hands you that ship's controls.
                    auto pick = [&](int ship_index, bool cockpit, bool three_d, int interior, int base_index, int base_camera)
                    {
                        if (ship_index >= 0 && ship_index != props.get_int("/sim/active-ship"))
                        {
                            props.set_bool("/autopilot/enabled", false);
                            props.set_string("/autopilot/status", "");
                            sim.activate(ship_index);
                            ship = sim.active_ship();
                            props.set_double("/controls/throttle", 0.0);
                            props.set_double("/controls/hover", 0.0);
                            props.set_string("/controls/sas", "stability");
                            sas_index = 0;
                            cam_distance = ship->spec().camera_distance;
                        }
                        cockpit_view = cockpit;
                        cockpit3d = three_d;
                        interior_cam = interior;
                        user_cam = -1;
                        base_view = base_index;
                        base_view_cam = base_camera;
                        look_yaw = look_pitch = 0.0f;
                        map_mode = galaxy_mode = false;
                    };
                    const bool chase_now = !cockpit_view && interior_cam < 0 && base_view < 0 && user_cam < 0;
                    for (int si = 0; si < sim.ship_count(); ++si)
                    {
                        const Ship &other = *sim.ship(si);
                        const kosmos::ShipSpec &spec = other.spec();
                        const bool mine = si == active;
                        ImGui::PushID(si);
                        if (ImGui::TreeNodeEx((other.name + (mine ? "  (flying)" : "")).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                        {
                            if (ImGui::Selectable("External", mine && chase_now))
                                pick(si, false, false, -1, -1, 0);
                            if (spec.cockpit.present && ImGui::Selectable(spec.building() ? "Interface (2D panel)" : spec.cockpit.mfds || !spec.cockpit.panel.empty() ? "Cockpit (2D panel)" : "Cockpit", mine && cockpit_view && !cockpit3d && interior_cam < 0 && base_view < 0))
                                pick(si, true, false, -1, -1, 0);
                            if (spec.cockpit.present && spec.cockpit.mfds && ImGui::Selectable(spec.building() ? "Interface (3D)" : "Cockpit (3D, MFDs)", mine && cockpit_view && cockpit3d && interior_cam < 0 && base_view < 0))
                                pick(si, true, true, -1, -1, 0);
                            for (std::size_t ci = 0; ci < spec.cameras.size(); ++ci)
                                if (ImGui::Selectable(spec.cameras[ci].name.c_str(), mine && interior_cam == static_cast<int>(ci) && base_view < 0))
                                    pick(si, false, false, static_cast<int>(ci), -1, 0);
                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    }
                    if (!user_cams.empty() && ImGui::TreeNodeEx("My cameras", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        for (std::size_t ui = 0; ui < user_cams.size(); ++ui)
                        {
                            ImGui::PushID(2000 + static_cast<int>(ui));
                            if (ImGui::Selectable(user_cams[ui].name.c_str(), user_cam == static_cast<int>(ui)))
                            {
                                pick(-1, false, false, -1, -1, 0);
                                user_cam = static_cast<int>(ui);
                            }
                            ImGui::PopID();
                        }
                        ImGui::TreePop();
                    }
                    for (std::size_t bi = 0; bi < sim.bases.size(); ++bi)
                    {
                        const kosmos::Base &bs = sim.bases[bi];
                        if (bs.cameras.empty())
                            continue;
                        ImGui::PushID(1000 + static_cast<int>(bi));
                        if (ImGui::TreeNodeEx((bs.name + " base").c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                        {
                            for (std::size_t ci = 0; ci < bs.cameras.size(); ++ci)
                                if (ImGui::Selectable(bs.cameras[ci].name.c_str(), base_view == static_cast<int>(bi) && base_view_cam == static_cast<int>(ci)))
                                    pick(-1, false, false, -1, static_cast<int>(bi), static_cast<int>(ci));
                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    }
                    ImGui::Separator();
                    ImGui::TextDisabled("C cycles the flown ship's views and your cameras. Right-drag looks around.");
                    if (ImGui::Button("Camera editor..."))
                        camera_editor_window = true;
                }
                ImGui::End();
            }
        }
        if (sl::gui_available())
            menu_bar.draw(sim, menu_windows, menu_actions);
        sl::gui_render();

        if (!options.shot.empty() && !shot_saved && clock >= options.shot_at)
        {
            sl::save_bitmap(sl::screen, options.shot);
            shot_saved = true;
            running = false;
        }
        if (screenshot_requested && sl::time_ms() >= screenshot_due_ms)
        {
            screenshot_requested = false;
            const std::string file = "kosmos-" + std::to_string(sl::time_ms()) + ".png";
            sl::save_bitmap(sl::screen, file);
            sim.log("Saved " + file);
        }
        if (options.record_seconds > 0.0)
        {
            if (!record_pipe && record_frames == 0)
                menu_actions.toggle_recording();
            else if (clock >= options.record_seconds)
                running = false;
        }
        if (record_pipe && sl::download_bitmap(sl::screen))
        {
            std::fwrite(sl::screen->pixels.data(), 1, sl::screen->pixels.size(), record_pipe);
            ++record_frames;
            flush_record_audio();
        }
        sl::show_video_bitmap();
        sl::end_frame();
    }

    stop_recording();
    netplay.leave();
    netplay.lobby_disconnect();
    // The launcher's "Resume" starts this scenario: the ships where they were left, with the one you were flying active.
    if (!kosmos::Sim::user_dir().empty() && sim.ship_count() > 0 && options.selftest <= 0.0 && options.shot.empty())
    {
        kosmos::Scenario last = kosmos::capture_scenario(sim, "Last session");
        last.epoch_jd = sim.universe.julian_date();
        std::string error;
        if (!kosmos::save_scenario((std::filesystem::path(kosmos::Sim::user_dir()) / "scenarios" / "last_session.json").string(), last, error))
            std::fprintf(stderr, "kosmos: %s\n", error.c_str());
    }
    settings_save(sim.props);
    sl::gui_shutdown();
    lightning.release();
    sound.stop();
    vis.clear_parts();
    leave_system();
    if (vis.local_cloud)
        sl::destroy_mesh(vis.local_cloud);
    if (vis.rain_mesh)
        sl::destroy_mesh(vis.rain_mesh);
    if (vis.bolt_mesh)
        sl::destroy_mesh(vis.bolt_mesh);
    for (sl::Mesh *m : vis.limb_pool)
        sl::destroy_mesh(m);
    for (sl::Mesh *m : {vis.sphere, vis.glow, vis.quad, vis.starfield, vis.tail})
        sl::destroy_mesh(m);
    sl::destroy_bitmap(vis.lights_texture);
    sl::destroy_bitmap(vis.glow_texture);
    for (sl::Bitmap *b : {vis.star_surface, vis.star_filaments, vis.star_corona, vis.star_corona_far, vis.star_rays, vis.star_limb, vis.sky})
        sl::destroy_bitmap(b);
    sl::destroy_bitmap(vis.puff_texture);
    sl::destroy_bitmap(vis.cloud_texture);
    sl::destroy_bitmap(vis.nebula_atlas);
    if (vis.nebulae)
        sl::destroy_mesh(vis.nebulae);
    sl::destroy_surface_3d(surface);
    sl::gamepad_shutdown();
    sl::shutdown();
    return 0;
}
