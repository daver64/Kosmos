#pragma once

// The command-line options, filled in by parse_command_line and read at start-up.

#include "netplay.h"

#include <string>
#include <utility>
#include <vector>

#ifndef KOSMOS_ROOT
#define KOSMOS_ROOT "."
#endif

namespace kosmos::app
{
    struct Options
    {
        std::string root = KOSMOS_ROOT;
        std::string life_support;
        bool systems_window = false;
        double selftest = 0.0;
        int max_texture = -1; // --max-texture W: largest texture width to load; -1 uses the saved setting (0 there means 16384)
        int world_texture = -1; // --world-texture W: far-view image width for invented worlds (0 = automatic); -1 uses the saved setting
        int render_style = -1; // --render-style normal|flat|wireframe; -1 uses the saved setting
        double selftest_warp = 0.0;
        bool selftest_gear_up = false;
        double selftest_drop = -1.0;
        bool selftest_drop_atom = false;
        bool selftest_arsenal = false;
        bool selftest_weapons = false;
        bool selftest_timed_burn = false;
        bool selftest_orbital_sas = false;
        bool selftest_ui_windows = false;
        bool selftest_vessel_ascent = false;
        bool selftest_save = false;
        bool selftest_orbitcomp = false;
        double selftest_throttle = -1.0;
        /** Hold this throttle while taking a --shot (for a picture of the exhaust); negative leaves the controls alone. */
        double shot_throttle = -1.0;
        /** Separate this many stages before a --shot, to photograph the upper stages. */
        int shot_stages = 0;
        bool auto_stage = false;            // --auto-stage: drop each stage when its propellant runs out
        double record_after_stage = 0.0;    // --record-after-stage SECONDS: with --record, end the video this long after the first staging
        /** Which 2D panel the 2D cockpit starts on (--panel N, from 0). */
        int panel = 0;
        /** Ignore the tiled Earth imagery and use the single image (--no-tiles). */
        bool no_tiles = false;
        int orbital_relief_patches = 0;
        int orbital_relief_meshes = 0;
        int orbital_relief_textures = 0;
        bool orbital_relief_ground = true;
        std::string orbiter_microtexture_root;
        bool camera_window = false;
        bool settings_window = false;
        bool performance_window = false;
        bool keys_window = false;
        bool link_window = false;
        /** KosmosLink: --link listens for outside programs from the start, --link-port moves it off the default 9420. */
        bool link_listen = false;
        int link_port = 9420;
        bool aircraft_autopilot = false, spacecraft_autopilot = false;
        bool property_window = false;
        bool lua_window = false;
        bool menu_visible = false, scenario_editor = false, base_editor = false;
        bool camera_editor = false;
        int user_camera = -1; // index into the Camera editor list to start in
        std::string view_base; // "base:index" to start looking through a base camera
        bool interior_at = false;   // --interior-at X Z YAW PITCH: after walking into an interior, stand at this spot (metres, degrees)
        double interior_pos[4] = {};
        std::string hold_item; // --hold ID: select this pack item (for example tricorder) once on foot
        std::string enter_building; // --enter-building TEXT: start inside the first building whose name contains TEXT
        std::string shot;
        double shot_at = 10.0;
        double record_seconds = 0.0; // --record SECONDS: record a video from the start, then quit
        double shot_time = 0.0;
        double earth_turn = 0.0;
        /** Julian date (UTC) to start on, from --date or --mjd; 0 uses the scenario's date or the default. */
        double start_jd = 0.0;
        bool autopilot = false;
        bool orbit_start = false;
        bool clean = false; // --clean: no HUD text, suit panel, prompts, messages or held item (promo shots); implies --hide-info
        bool hide_info = false, show_info = false, no_clouds = false, no_clutter = false, perf = false, eva = false, eva_walk = false;
        std::string hud;
        bool no_relief = false;
        std::string blender;
        bool part_window = false;
        // Multiplayer
        bool net_host = false, net_window = false;
        int net_port = kosmos::NetPlay::default_port;
        std::string net_join, net_name, net_advertise, net_irc = "salyut.ddns.net", net_channel = "#kosmos";
        bool map = false;
        bool cockpit = false;
        bool cockpit3d = false;
        int mfd_display = 0; // 0 off, 1 panels, 2 heads-up
        std::string mfd_left, mfd_right;
        int ship_index = 0;
        std::string ship_name = "Kestrel";
        std::string scenario;
        std::string target;
        std::vector<std::string> sets; // --set path=value, applied once the scenario is loaded
        bool list_scenarios = false;
        std::string bake_body; // --bake-planet
        int bake_width = 16384;
        bool bake_estimate = false;
        bool bake_lazy = false;
        std::string gen_world; // --gen-world
        int gen_width = 16384;
        int gen_threads = 4;
        int gen_memory_mb = 4096;
        double map_scale = 0.0;
        std::string map_focus;
        std::string orbit_body;
        bool portal_arrival = false; // start in front of the system's quantum portal, facing away from it, as after a jump
        double cam_pitch = 0.25;
        double cam_yaw = 0.7;
        double cam_distance = 0.0;
        bool alone = false; // --alone: with --orbit, do not place the other addon ships alongside
        std::string scenario_file; // --scenario-file PATH: run a scenario from this file without installing it
        std::vector<std::pair<double, std::string>> set_at; // --set-at SECS PATH=VALUE: set a property this many seconds into the run
        double drop_at = -1.0, drop_interval = 0.5; // --drop SECS COUNT INTERVAL: release COUNT bombs from the first loaded hardpoint, one every INTERVAL s from SECS (for video)
        int drop_count = 0;
        double launch_at = -1.0; // --launch-at SECS: with --auto-stage, hold the engines until this time, then light them over 3 s (for video)
        double cam_tilt = 0.0; // --cam-tilt DEG: nod the chase camera up and down by this much (for video)
        double cam_dolly = 0.0; // --cam-dolly M_PER_SEC: move the chase camera away (or in, if negative) as it films (for video)
        double cam_spin = 0.0; // --cam-spin DEG_PER_SEC: slowly swing the chase camera round the ship (for video)
        bool cam_sunward = false;
        /** Put the camera on the far side of the ship from the star, turned aside, so the star is in view (for pictures). */
        bool cam_antisun = false;
        double cam_planet = 0.0; // --cam-planet R: look at the centre of the body being orbited from R body radii away, ignoring the ship
        double cam_phase = 0.0;  // --cam-phase DEG: with --cam-planet, the angle at the planet between the camera and the Sun (0 = full face, 180 = new)
        double cam_fov = 50.0;
        double orbit_altitude = 0.0;
        bool orbit_half = false;
        float ambient = 0.3f;
        bool sunlit = false;
        double sun_angle = 0.0; // --sun-angle DEG: with --sunlit, sit DEG round the planet from the sub-solar point (about 70 shows two thirds lit)
        bool fullscreen = false;
        double orbit_phase = 0.0;
        int system = 0;
        std::string system_name;
        bool list_stars = false;
        std::string list_worlds; // --list-worlds TYPE
        bool check_data = false;
        std::string export_base, export_file;
        bool galaxy_view = false;
        bool quantum_window = false;
        std::string test_scenario; // --test-scenario ID: switch to that scenario a couple of seconds into the run (for testing)
        int test_dial = -1; // --test-dial N: dial the portal to star N a second into the run (for testing)
        int test_jump = -1; // --test-jump N: quantum-jump to star N a second into the run (for testing)
    };
} // namespace kosmos::app
