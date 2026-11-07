#include "command_line.h"

#include "body_terrain.h"
#include "surface_textures.h"
#include "scenario.h"
#include <algorithm>
#include <functional>
#include <unordered_map>

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

    namespace
    {
    // One entry per command-line flag: the number of arguments it takes and what it does with them (false stops the run with an error).
    // A flag missing its arguments is not recognised and goes to the backend with the rest.
    struct Flag
    {
        const char *name;
        int arity;
        std::function<bool(Options &, char **)> apply;
    };

    using Apply = std::function<bool(Options &, char **)>;

    template <typename T> Apply on(bool T::*field)
    {
        return [field](Options &o, char **) { o.*field = true; return true; };
    }
    Apply text(std::string Options::*field)
    {
        return [field](Options &o, char **a) { o.*field = a[0]; return true; };
    }
    Apply real(double Options::*field)
    {
        return [field](Options &o, char **a) { o.*field = std::atof(a[0]); return true; };
    }
    Apply whole(int Options::*field)
    {
        return [field](Options &o, char **a) { o.*field = std::atoi(a[0]); return true; };
    }

    // A number picks among the ships spawned in orbit; a name starts the scenario with that ship.
    bool pick_ship(Options &o, char **a)
    {
        if (std::isdigit(static_cast<unsigned char>(a[0][0])))
            o.ship_index = std::atoi(a[0]);
        else
            o.ship_name = a[0];
        return true;
    }

    // Kept in time order, so an earlier event given later on the command line is not held up.
    bool set_at(Options &o, char **a)
    {
        const double at = std::atof(a[0]);
        const auto later = std::find_if(o.set_at.begin(), o.set_at.end(), [at](const std::pair<double, std::string> &e) { return e.first > at; });
        o.set_at.insert(later, {at, a[1]});
        return true;
    }

    bool read_date(Options &o, char **a)
    {
        if (kosmos::parse_epoch(a[0], o.start_jd))
            return true;
        std::fprintf(stderr, "kosmos: cannot read --date \"%s\" (use \"YYYY-MM-DD HH:MM:SS\" in GMT, or an MJD)\n", a[0]);
        return false;
    }

    const std::unordered_map<std::string, Flag> &flag_table()
    {
        static const std::unordered_map<std::string, Flag> table = [] {
            const std::vector<Flag> list = {
        {"--selftest", 1, real(&Options::selftest)},
        {"--system", 1, text(&Options::system_name)},
        {"--stars", 0, on(&Options::list_stars)},
        {"--list-worlds", 1, text(&Options::list_worlds)},
        {"--galaxy-map", 0, on(&Options::galaxy_view)},
        {"--quantum", 0, on(&Options::quantum_window)},
        {"--test-scenario", 1, text(&Options::test_scenario)},
        {"--test-jump", 1, whole(&Options::test_jump)},
        {"--test-dial", 1, whole(&Options::test_dial)},
        {"--max-texture", 1, whole(&Options::max_texture)},
        {"--join", 1, text(&Options::net_join)},
        {"--name", 1, text(&Options::net_name)},
        {"--advertise", 1, text(&Options::net_advertise)},
        {"--irc", 1, text(&Options::net_irc)},
        {"--channel", 1, text(&Options::net_channel)},
        {"--lobby", 0, on(&Options::net_window)},
        {"--fullscreen", 0, on(&Options::fullscreen)},
        {"--no-clouds", 0, on(&Options::no_clouds)},
        {"--no-clutter", 0, on(&Options::no_clutter)},
        {"--eva", 0, on(&Options::eva)},
        {"--perf", 0, on(&Options::perf)},
        {"--no-tiles", 0, on(&Options::no_tiles)},
        {"--no-relief", 0, on(&Options::no_relief)},
        {"--blender", 1, text(&Options::blender)},
        {"--part-editor", 0, on(&Options::part_window)},
        {"--hide-info", 0, on(&Options::hide_info)},
        {"--show-info", 0, on(&Options::show_info)},
        {"--systems-window", 0, on(&Options::systems_window)},
        {"--life-support", 1, text(&Options::life_support)},
        {"--hud", 1, text(&Options::hud)},
        {"--camera-editor", 0, on(&Options::camera_editor)},
        {"--user-camera", 1, whole(&Options::user_camera)},
        {"--camera-window", 0, on(&Options::camera_window)},
        {"--aircraft-autopilot-window", 0, on(&Options::aircraft_autopilot)},
        {"--spacecraft-autopilot-window", 0, on(&Options::spacecraft_autopilot)},
        {"--settings-window", 0, on(&Options::settings_window)},
        {"--performance-window", 0, on(&Options::performance_window)},
        {"--keys-window", 0, on(&Options::keys_window)},
        {"--link-window", 0, on(&Options::link_window)},
        {"--link", 0, on(&Options::link_listen)},
        {"--lua-window", 0, on(&Options::lua_window)},
        {"--check-data", 0, on(&Options::check_data)},
        {"--menu", 0, on(&Options::menu_visible)},
        {"--scenario-editor", 0, on(&Options::scenario_editor)},
        {"--base-editor", 0, on(&Options::base_editor)},
        {"--property-window", 0, on(&Options::property_window)},
        {"--enter-building", 1, text(&Options::enter_building)},
        {"--hold", 1, text(&Options::hold_item)},
        {"--base-cam", 1, text(&Options::view_base)},
        {"--selftest-throttle", 1, real(&Options::selftest_throttle)},
        {"--selftest-gear-up", 0, on(&Options::selftest_gear_up)},
        {"--selftest-drop", 1, real(&Options::selftest_drop)},
        {"--selftest-weapons", 0, on(&Options::selftest_weapons)},
        {"--selftest-timed-burn", 0, on(&Options::selftest_timed_burn)},
        {"--selftest-orbital-sas", 0, on(&Options::selftest_orbital_sas)},
        {"--selftest-ui-windows", 0, on(&Options::selftest_ui_windows)},
        {"--selftest-vessel-ascent", 0, on(&Options::selftest_vessel_ascent)},
        {"--selftest-save", 0, on(&Options::selftest_save)},
        {"--selftest-orbitcomp", 0, on(&Options::selftest_orbitcomp)},
        {"--selftest-arsenal", 0, on(&Options::selftest_arsenal)},
        {"--selftest-drop-atom", 0, on(&Options::selftest_drop_atom)},
        {"--selftest-warp", 1, real(&Options::selftest_warp)},
        {"--panel", 1, whole(&Options::panel)},
        {"--shot-stages", 1, whole(&Options::shot_stages)},
        {"--shot-throttle", 1, real(&Options::shot_throttle)},
        {"--shot", 1, text(&Options::shot)},
        {"--record", 1, real(&Options::record_seconds)},
        {"--shot-at", 1, real(&Options::shot_at)},
        {"--auto-stage", 0, on(&Options::auto_stage)},
        {"--record-after-stage", 1, real(&Options::record_after_stage)},
        {"--autopilot", 0, on(&Options::autopilot)},
        {"--cockpit", 0, on(&Options::cockpit)},
        {"--scenario", 1, text(&Options::scenario)},
        {"--target", 1, text(&Options::target)},
        {"--bake-planet", 1, text(&Options::bake_body)},
        {"--gen-world", 1, text(&Options::gen_world)},
        {"--gen-width", 1, whole(&Options::gen_width)},
        {"--bake-width", 1, whole(&Options::bake_width)},
        {"--bake-lazy", 0, on(&Options::bake_lazy)},
        {"--bake-estimate", 0, on(&Options::bake_estimate)},
        {"--list-scenarios", 0, on(&Options::list_scenarios)},
        {"--mfd", 1, whole(&Options::mfd_display)},
        {"--mfd-left", 1, text(&Options::mfd_left)},
        {"--mfd-right", 1, text(&Options::mfd_right)},
        {"--map-scale", 1, real(&Options::map_scale)},
        {"--map-focus", 1, text(&Options::map_focus)},
        {"--map", 0, on(&Options::map)},
        {"--orbit", 0, on(&Options::orbit_start)},
        {"--orbit-body", 1, text(&Options::orbit_body)},
        {"--cam-pitch", 1, real(&Options::cam_pitch)},
        {"--cam-yaw", 1, real(&Options::cam_yaw)},
        {"--cam-dolly", 1, real(&Options::cam_dolly)},
        {"--cam-spin", 1, real(&Options::cam_spin)},
        {"--launch-at", 1, real(&Options::launch_at)},
        {"--alone", 0, on(&Options::alone)},
        {"--cam-tilt", 1, real(&Options::cam_tilt)},
        {"--cam-fov", 1, real(&Options::cam_fov)},
        {"--cam-antisun", 0, on(&Options::cam_antisun)},
        {"--cam-sunward", 0, on(&Options::cam_sunward)},
        {"--cam-planet", 1, real(&Options::cam_planet)},
        {"--cam-phase", 1, real(&Options::cam_phase)},
        {"--cam-distance", 1, real(&Options::cam_distance)},
        {"--orbit-phase", 1, real(&Options::orbit_phase)},
        {"--sunlit", 0, on(&Options::sunlit)},
        {"--sun-angle", 1, real(&Options::sun_angle)},
        {"--orbit-half", 0, on(&Options::orbit_half)},
        {"--orbit-alt", 1, real(&Options::orbit_altitude)},
        {"--warp-to", 1, real(&Options::shot_time)},
        {"--root", 1, text(&Options::root)},
        {"--world-texture", 1, [](Options &o, char **a) { o.world_texture = std::max(0, std::atoi(a[0])); return true; }},
        {"--render-style", 1, [](Options &o, char **a) { const std::string v = a[0]; o.render_style = v == "flat" ? 1 : v == "wireframe" ? 2 : 0; return true; }},
        {"--eva-walk", 0, [](Options &o, char **) { o.eva = o.eva_walk = true; return true; }},
        {"--no-detail", 0, [](Options &, char **) { detail_disabled = true; return true; }},
        {"--clean", 0, [](Options &o, char **) { o.clean = o.hide_info = true; return true; }},
        {"--console", 0, [](Options &, char **) { return true; }}, // handled by main() before the window opens (Windows only)
        {"--cockpit3d", 0, [](Options &o, char **) { o.cockpit = o.cockpit3d = true; return true; }},
        {"--set", 1, [](Options &o, char **a) { o.sets.push_back(a[0]); return true; }},
        {"--link-port", 1, [](Options &o, char **a) { o.link_port = std::clamp(std::atoi(a[0]), 0, 65535); return true; }},
        {"--gen-threads", 1, [](Options &o, char **a) { o.gen_threads = std::clamp(std::atoi(a[0]), 1, 4); return true; }},
        {"--gen-memory", 1, [](Options &o, char **a) { o.gen_memory_mb = std::clamp(std::atoi(a[0]), 512, 4096); return true; }},
        {"--scenario-file", 1, text(&Options::scenario_file)},
        {"--ambient", 1, [](Options &o, char **a) { o.ambient = static_cast<float>(std::atof(a[0])); return true; }},
        {"--mjd", 1, [](Options &o, char **a) { o.start_jd = std::atof(a[0]) + kosmos::mjd_offset; return true; }},
        {"--earth-turn", 1, [](Options &o, char **a) { o.earth_turn = std::atof(a[0]) * 3.14159265358979 / 180.0; return true; }},
        {"--export-base", 2, [](Options &o, char **a) { o.export_base = a[0]; o.export_file = a[1]; return true; }},
        {"--drop", 3, [](Options &o, char **a) { o.drop_at = std::atof(a[0]); o.drop_count = std::atoi(a[1]); o.drop_interval = std::atof(a[2]); return true; }},
        {"--interior-at", 4, [](Options &o, char **a) { o.interior_at = true; for (int k = 0; k < 4; ++k) o.interior_pos[k] = std::atof(a[k]); return true; }},
        {"--set-at", 2, set_at},
        {"--ship", 1, pick_ship},
        {"--date", 1, read_date},
            };
            std::unordered_map<std::string, Flag> map;
            for (const Flag &f : list)
                map.emplace(f.name, f);
            return map;
        }();
        return table;
    }

    // Developer preview: --gen-planet TYPE SEED OUT.png [WIDTH] writes an invented world's far-view image and exits.
    int generate_planet_preview(int argc, char **argv, int i)
    {
        kosmos::PlanetStyle style;
        style.type = kosmos::archetype_from_name(argv[i + 1]);
        style.seed = std::atoi(argv[i + 2]);
        const std::string out = argv[i + 3];
        const int width = i + 4 < argc && std::atoi(argv[i + 4]) > 0 ? std::atoi(argv[i + 4]) : 1024;
        if (!style.valid())
        {
            std::fprintf(stderr, "types: mercury venus earth mars gas\n");
            return 1;
        }
        static const float tints[][3] = {{0.6f, 0.6f, 0.6f}, {0.6f, 0.6f, 0.6f}, {0.88f, 0.75f, 0.47f}, {0.24f, 0.43f, 0.67f}, {0.75f, 0.4f, 0.25f}, {0.85f, 0.7f, 0.5f}};
        std::copy(tints[static_cast<int>(style.type)], tints[static_cast<int>(style.type)] + 3, style.tint);
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

    // --host [PORT]: the port is optional, so this one does not fit the table.
    void parse_host(int argc, char **argv, int &i, Options &options)
    {
        options.net_host = true;
        if (i + 1 < argc && std::isdigit(static_cast<unsigned char>(argv[i + 1][0])))
            options.net_port = std::clamp(std::atoi(argv[++i]), 1, 65535);
    }

    // Applies the flag at argv[i] from the table and moves i past its arguments. Returns 0 if it was applied, 1 if it failed, -1 if it is not a known flag.
    int apply_flag(int argc, char **argv, int &i, Options &options)
    {
        const auto it = flag_table().find(argv[i]);
        if (it == flag_table().end() || i + it->second.arity >= argc)
            return -1;
        char **args = argv + i + 1;
        i += it->second.arity;
        return it->second.apply(options, args) ? 0 : 1;
    }
    } // namespace

    int parse_command_line(int argc, char **argv, Options &options, std::vector<char *> &backend_args)
    {
        for (int i = 1; i < argc; ++i)
        {
            if (!std::strcmp(argv[i], "--version"))
            {
                std::printf("Kosmos %s\n", KOSMOS_VERSION);
                return 0;
            }
            if (!std::strcmp(argv[i], "--gen-planet") && i + 3 < argc)
                return generate_planet_preview(argc, argv, i);
            if (!std::strcmp(argv[i], "--host"))
            {
                parse_host(argc, argv, i, options);
                continue;
            }
            const int applied = apply_flag(argc, argv, i, options);
            if (applied > 0)
                return 1;
            if (applied < 0)
                backend_args.push_back(argv[i]);
        }
        return keep_going;
    }
} // namespace kosmos::app
