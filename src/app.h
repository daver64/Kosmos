#pragma once

// Shared declarations for the Kosmos application (kosmos.cpp and the files split out of it).

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
#include "engine_sound.h"
#include "procedural.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#ifndef KOSMOS_ROOT
#define KOSMOS_ROOT "."
#endif

namespace kosmos::app
{
    // Returned by the start-up steps when the run carries on; any other value is the process exit code.
    constexpr int keep_going = std::numeric_limits<int>::min();

    constexpr double km = 1000.0;

    constexpr double pi = 3.14159265358979323846;

extern int screen_w;

extern int screen_h;

    constexpr double cloud_height = 60.0e3;

    const char *const sas_modes[] = {"stability", "prograde", "retrograde", "radial-out", "radial-in", "normal", "antinormal", "off"};

    constexpr int sas_mode_count = sizeof(sas_modes) / sizeof(sas_modes[0]);

    // The part being edited in Blender (the part editor window).
    struct PartEdit
    {
        kosmos::PartSpec *part = nullptr;
        kosmos::PartSpec original;
        bool converted = false;
        std::filesystem::path file;
        int stage = 0, index = 0;
        std::string model;
    };

    struct Options
    {
        std::string root = KOSMOS_ROOT;
        std::string life_support;
        bool systems_window = false;
        double selftest = 0.0;
        int max_texture = 16384;
        int world_texture = -1; // --world-texture W: far-view image width for invented worlds (0 = automatic); -1 uses the saved setting
        int render_style = -1; // --render-style normal|flat|wireframe; -1 uses the saved setting
        double selftest_warp = 0.0;
        bool selftest_gear_up = false;
        double selftest_drop = -1.0;
        bool selftest_drop_atom = false;
        bool selftest_arsenal = false;
        double selftest_throttle = -1.0;
        /** Hold this throttle while taking a --shot (for a picture of the exhaust); negative leaves the controls alone. */
        double shot_throttle = -1.0;
        /** Separate this many stages before a --shot, to photograph the upper stages. */
        int shot_stages = 0;
        /** Which 2D panel the 2D cockpit starts on (--panel N, from 0). */
        int panel = 0;
        /** Ignore the tiled Earth imagery and use the single image (--no-tiles). */
        bool no_tiles = false;
        bool camera_window = false;
        bool settings_window = false;
        bool keys_window = false;
        bool aircraft_autopilot = false, spacecraft_autopilot = false;
        bool property_window = false;
        bool lua_window = false;
        bool menu_visible = false, scenario_editor = false, base_editor = false;
        bool camera_editor = false;
        int user_camera = -1; // index into the Camera editor list to start in
        std::string view_base; // "base:index" to start looking through a base camera
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
        double map_scale = 0.0;
        std::string map_focus;
        std::string orbit_body;
        double cam_pitch = 0.25;
        double cam_yaw = 0.7;
        double cam_distance = 0.0;
        bool cam_sunward = false;
        /** Put the camera on the far side of the ship from the star, turned aside, so the star is in view (for pictures). */
        bool cam_antisun = false;
        double cam_fov = 50.0;
        double orbit_altitude = 0.0;
        bool orbit_half = false;
        float ambient = 0.3f;
        bool sunlit = false;
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
        std::string test_jump_body; // --test-jump-body NAME: with --test-jump, arrive at that body
        std::string test_scenario; // --test-scenario ID: switch to that scenario a couple of seconds into the run (for testing)
        int test_jump = -1; // --test-jump N: quantum-jump to star N a second into the run (for testing)
    };

    std::string format_time(double seconds);

    std::string format_distance(double metres);
} // namespace kosmos::app
