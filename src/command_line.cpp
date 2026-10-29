#include "command_line.h"

#include "body_terrain.h"
#include "surface_textures.h"
#include "scenario.h"

namespace kosmos::app
{
    // A packaged build keeps data/ beside the executable; a development build falls back to the source tree.
    std::string default_root(const char *argv0)
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

    int parse_command_line(int argc, char **argv, Options &options, std::vector<char *> &backend_args)
    {
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
            else if (!std::strcmp(argv[i], "--clean"))
                options.clean = options.hide_info = true;
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
            else if (!std::strcmp(argv[i], "--keys-window"))
                options.keys_window = true;
            else if (!std::strcmp(argv[i], "--link-window"))
                options.link_window = true;
            else if (!std::strcmp(argv[i], "--link"))
                options.link_listen = true;
            else if (!std::strcmp(argv[i], "--link-port") && i + 1 < argc)
                options.link_port = std::clamp(std::atoi(argv[++i]), 0, 65535);
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
            else if (!std::strcmp(argv[i], "--interior-at") && i + 4 < argc)
            {
                options.interior_at = true;
                for (double &v : options.interior_pos)
                    v = std::atof(argv[++i]);
            }
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
            else if (!std::strcmp(argv[i], "--selftest-weapons"))
                options.selftest_weapons = true;
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
            else if (!std::strcmp(argv[i], "--auto-stage"))
                options.auto_stage = true;
            else if (!std::strcmp(argv[i], "--record-after-stage") && i + 1 < argc)
                options.record_after_stage = std::atof(argv[++i]);
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
            else if (!std::strcmp(argv[i], "--cam-dolly") && i + 1 < argc)
                options.cam_dolly = std::atof(argv[++i]);
            else if (!std::strcmp(argv[i], "--cam-spin") && i + 1 < argc)
                options.cam_spin = std::atof(argv[++i]);
            else if (!std::strcmp(argv[i], "--scenario-file") && i + 1 < argc)
            {
                options.scenario_file = argv[++i];
            }
            else if (!std::strcmp(argv[i], "--set-at") && i + 2 < argc)
            {
                const double at = std::atof(argv[i + 1]);
                options.set_at.emplace_back(at, argv[i + 2]);
                i += 2;
            }
            else if (!std::strcmp(argv[i], "--drop") && i + 3 < argc)
            {
                options.drop_at = std::atof(argv[++i]);
                options.drop_count = std::atoi(argv[++i]);
                options.drop_interval = std::atof(argv[++i]);
            }
            else if (!std::strcmp(argv[i], "--launch-at") && i + 1 < argc)
                options.launch_at = std::atof(argv[++i]);
            else if (!std::strcmp(argv[i], "--alone"))
                options.alone = true;
            else if (!std::strcmp(argv[i], "--cam-tilt") && i + 1 < argc)
                options.cam_tilt = std::atof(argv[++i]);
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
            else if (!std::strcmp(argv[i], "--sun-angle") && i + 1 < argc)
                options.sun_angle = std::atof(argv[++i]);
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
        return keep_going;
    }
} // namespace kosmos::app
