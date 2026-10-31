#pragma once

// The application's state. It was once the local variables of main(); the start-up steps, the per-frame phases and the
// shutdown are now member functions spread over the app_*.cpp files.

#include "sl.h"
#include "props3d.h"
#include "flight.h"
#include "example_fx.h"
#include "sim.h"
#include "lightning.h"
#include "lua_console.h"
#include "joystick.h"
#include "keybindings.h"
#include "link/native_addons.h"
#include "menu.h"
#include "netplay.h"
#include "planet_data.h"
#include "terrain_clutter.h"
#include "planet_bake.h"
#include "blender_edit.h"
#include "usercam.h"
#include "eva.h"
#include "interior.h"
#include "interior_runtime.h"
#include "eva_solids.h"
#include "ordnance.h"
#include "ship_weapons.h"
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
#include "command_line.h"
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

#include "app.h"

namespace kosmos::app
{
    struct StarPlace
    {
        bool valid = false;
        glm::vec3 dir{0.0f}, tint{1.0f};
        double distance = 0.0, radius = 0.0, angular = 0.0;
    };

    /** Opt-in link events: what each was last told, so only changes are broadcast. */
    struct LinkWatch
    {
        bool primed = false, paused = false, landed = false, crashed = false;
        int warp = 0, ships = 0;
        std::string soi, weather;
        const Ship *tracked = nullptr;
        std::chrono::steady_clock::time_point tick{};
    };

    /** What the per-frame phases remember from one frame to the next (edge detection, timers, window fields). */
    struct FrameMemory
    {
        double eva_report_at = 0.0;
        bool trigger_was_down = false, primary_was = false, secondary_was = false;
        sl::dvec3 carried_east{0.0};
        int carried_soi = -1;
        double tilt_clock = 0.0;
        bool cues_seen = false, was_landed = false, was_crashed = false, was_docked = false, was_alarm = false;
        int was_stage = 0;
        double alarm_next = 0.0;
        std::string weather_seen;
        std::chrono::steady_clock::time_point weather_until{};
        int selected_vessel = 0, slice_y = 5, slice_gas = 0;
        char metar[256] = "";
        bool metar_loaded = false;
        LinkWatch link_watch;
    };

    struct App
    {
        FrameMemory memory;
        kosmos::Universe *universe_ref{};
        sl::PropertyTree *props_ref{};
        const kosmos::Telemetry *tm_ref{};
        sl::FlightBody *fb_ref{};
        const kosmos::Body *soi_body_ref{};
        Options options;
        std::vector<char *> backend_args;
        Sim sim;
        std::string error;
        sl::Surface3D * surface{};
        int surface_w{};
        int surface_h{};
        int surface_msaa{1};
        Visuals vis;
        int earth_id{};
        const kosmos::Body * earth_body{};
        std::vector<std::unique_ptr<BodyTerrain>> body_terrains;
        sl::Terrain * terrain{};
        TerrainSet terrain_set;
        sl::Mesh * cloud_shell{};
        sl::Bitmap * home_cloud_texture{};
        double home_cloud_height{};
        kosmos::BaseRenderer base_renderer;
        std::size_t base_beacon_mark{};
        void leave_system();
        void place_worlds();
        bool change_date(double epoch_jd, std::string &error);
        void reload_world_textures();
        void load_planet_images();
        void load_earth_tiles();
        bool build_terrains();
        void build_cloud_layers();
        bool enter_system();
        Ship * ship{};
        sl::Hud hud;
        EngineSound sound;
        kosmos::Lightning lightning;
        bool sound_on{};
        bool first_frame{};
        bool show_orbit{};
        bool show_info{};
        std::uint32_t last_frame_stamp{};
        bool running{};
        bool shot_saved{};
        bool map_mode{};
        MapView map;
        GalaxyView gview;
        bool galaxy_mode{};
        int jump_to{};
        bool jump_to_star_orbit{};
        std::string jump_to_body;
        std::string quantum_body;
        int quantum_bodies_for{};
        std::vector<std::pair<std::string, std::string>> quantum_bodies;
        bool quantum_window{};
        int quantum_selected{};
        bool cockpit_view{};
        kosmos::Astronaut eva;
        kosmos::Arsenal arsenal;
        kosmos::ShipWeapons ship_weapons;
        bool mouse_captured{};
        bool pointer_free{};
        int mfd_display{};
        int cockpit_mfd{};
        int hud_sides = 3; // bit 0 left, bit 1 right: which see-through MFDs are shown
        bool first_ship_seen{};
        int panel_index{};
        bool mfd_mouse_prev{};
        const kosmos::Cockpit::VcHotspot *vc_drag_spot{};
        float vc_drag_v{};
        // Panels and MFDs can be dragged with the left button; ids: 0 the 2D panel, 1 and 2 the left and right MFD.
        int ui_drag_id = -1;
        float ui_drag_last_x{}, ui_drag_last_y{};
        float panel_offset[2]{};
        float mfd_offset[2][2]{};
        float mfd_base[2][2]{};
        bool cockpit3d{};
        int interior_cam{};
        int base_view{};
        int base_view_cam{};
        std::string user_cam_file;
        std::vector<kosmos::UserCamera> user_cams;
        int user_cam{};
        bool camera_editor_window{};
        int camera_editor_pick{};
        bool systems_window{};
        bool camera_window{};
        bool settings_window{};
        bool performance_window{};
        bool icbm_window{};
        bool property_window{};
        bool lua_window{};
        bool net_window{};
        bool part_window{};
        bool edit_paused{};
        bool edit_was_paused{};
        kosmos::BlenderEdit blender_edit;
        PartEdit part_edit;
        std::string part_status;
        char blender_buffer[260]{};
        double model_poll{};
        kosmos::NetPlay netplay;
        NetUi net_ui;
        kosmos::LuaConsole lua_console;
        PropertyBrowser property_browser;
        int applied_volume{};
        sl::Bitmap * mfd_target[2]{};
        std::vector<sl::Bitmap *> gauge_target, gauge_scratch; // `vc_gauges` textures and the top-down scratch they are drawn in
        sl::Bitmap * instr_target[8]{}; // panel instruments shown on VC parts (`vc_instruments`)
        sl::Bitmap * hud_target = nullptr;
        // Scratch targets for supersampling the MFD and HUD textures (see gfx-screen-ss).
        sl::Bitmap * ss_big = nullptr;
        sl::Bitmap * ss_mid = nullptr;
        int ss_factor = 0;
        int screen_supersample() const;
        sl::Bitmap * supersample_target(int factor);
        void downsample_target(sl::Bitmap *source, sl::Bitmap *destination, sl::Colour clear);
        sl::Bitmap * panel_target{};
        int panel_target_height{};
        const Ship * mfd_ship{};
        std::string mfd_ship_id;
        float look_yaw{};
        float look_pitch{};
        kosmos::InteriorWalker walker;
        int panel_return{-1}; // index of the building whose panel is open from inside; Enter returns to walking there
        bool in_airlock{};     // the walker stands in the airlock room
        double room_kpa{-1.0}; // pressure of the room the walker is in, or negative
        std::string room_label;
        int interior_panel{-1}; // the wall panel the walker is facing, or -1
        double eva_look_yaw{};
        double eva_look_pitch{};
        sl::dvec3 eye_shift;
        kosmos::CameraDynamics cam_dyn; // the Dynamic Camera's head on its springs
        sl::dvec3 dyn_velocity{0.0};
        const kosmos::Ship *dyn_ship = nullptr;
        bool dyn_valid = false;
        const Ship * eye_ship{};
        int sas_index{};
        double earth_sphere_switch_m{};
        kosmos::Joystick joystick;
        kosmos::KeyBindings keys;
        bool keys_window{};
        bool link_window{};
        kosmos::link::NativeAddons native_addons;
        std::unique_ptr<kosmos::link::LinkHost> link_host;
        std::unique_ptr<kosmos::link::LinkServer> link_server;
        std::chrono::steady_clock::time_point link_tick{};
        const Ship *link_ship{};
        std::string link_scenario;
        bool link_failed{};
        std::string keys_status;
        bool joystick_window{};
        float cam_yaw{};
        float cam_pitch{};
        double cam_distance{};
        float free_yaw{};
        float free_pitch{};
        int last_mouse_x{};
        int last_mouse_y{};
        kosmos::MenuBar menu_bar;
        bool screenshot_requested{};
        decltype(sl::time_ms()) screenshot_due_ms{};
        std::vector<kosmos::MenuWindow> menu_windows;
        kosmos::MenuActions menu_actions;
        const sl::Terrain *terrain_of(int body);
        int nearest_ship(double &distance);
        sl::dvec3 door_world(const Ship &building);
        int building_at_door();
        kosmos::ArsenalContext arsenal_ctx;
        void board_ship(bool force);
        void leave_eva();
        void toggle_eva();
        FILE * record_pipe{};
        FILE * record_pcm{};
        std::string record_file;
        std::string record_video;
        std::string record_pcm_file;
        long record_frames{};
        double first_stage_clock = -1.0;
        double ascent_fuel0 = -1.0, pitchover_clock = -1.0, ignite_clock = -1.0, ramp_clock = -1.0;
        std::size_t set_at_done = 0;
        int drops_done = 0;
        std::map<std::string, double> held_axes; // scripted control axes (--set-at) that the keyboard polling must not zero
        int record_w{};
        int record_h{};
        int record_rate{};
        int record_channels{};
        std::mutex record_audio_mutex;
        std::vector<std::int16_t> record_audio;
        ExhaustFx exhaust_fx;
        sl::Event event_discard;
        double clock{};
        double lightning_flash{};
        double cloud_veil{};
        std::uint32_t last_time{};
        int applied_fps{};
        int gfx_style{};
        bool gfx_clouds{};
        double frame{};
        sl::Event event;
        bool typing{};
        bool eva_moving{};
        bool in_eva{};
        bool in_user{};
        bool in_base_cam{};
        bool in_interior{};
        bool in_cockpit{};
        bool in_cockpit3d{};
        sl::dvec3 up;
        sl::dvec3 eye_world;
        glm::vec3 ship_rel;
        double view_fov{};
        double fov_user{}; // field of view set with X / Z; 0 keeps each view's own
        double user_extent{};
        int soi{};
        sl::dvec3 soi_pos;
        bool home{};
        sl::dvec3 earth_pos;
        sl::dvec3 eye_from_earth;
        double eye_earth_distance{};
        sl::TerrainView view;
        bool near_earth{};
        /** Low over another world that has air (Mars, Venus, Titan): its own tinted sky and haze take the place of the home world's. */
        bool local_sky{};
        bool earth_as_sphere{};
        glm::vec3 to_sun;
        sl::Camera camera;
        double eye_soi_altitude{};
        double view_distance{};
        bool in_user_mounted{};
        double ship_near{};
        glm::mat4 view_proj;
        sl::dvec3 east;
        sl::dvec3 north;

        // Runs the program: returns the process exit code.
        int run(int argc, char **argv);

        int list_worlds();
        int generate_world();
        int bake_world();
        int export_base();
        int check_data();
        int init_sim(int argc, char **argv);
        int init_start_date();
        int init_window();
        int init_render();
        int init_systems();
        int init_ship();
        void menu_start_scenario(const kosmos::Scenario &sc);
        void menu_restart();
        void menu_show_panel(int index);
        void flush_record_audio();
        void stop_recording();
        void menu_toggle_recording();
        int init_ui();
        int init_helpers();
        int init_recording();
        int init_loop();
        bool begin_frame();
        void apply_settings();
        void restart_scenario();
        void drop_bomb(bool special);
        void next_scenario();
        void cycle_mfd_display();
        void update_camera_dynamics(double sim_dt);
        void draw_reentry_plasma();
        kosmos::HeadMotion head_motion() const;
        void step_fov(int direction);
        void cycle_camera();
        void switch_to_next_ship();
        void jump_to_player();
        void on_key_down();
        void poll_events();
        void follow_window();
        void pod_keys(bool on_foot);
        void run_scripted_options();
        void read_joystick(bool on_foot);
        void keyboard();
        void govern_time_warp();
        void poll_blender_edit();
        double step_simulation();
        void collide_ground_vehicles();
        void update_eva();
        void update_weapons(double time_before_step);
        void select_default_instruments();
        void publish_netplay_state();
        void update_view_flags();
        void drive_camera_controls();
        void advance_time();
        void level_camera_frame();
        void orbit_camera_eye();
        void place_cockpit_camera();
        void place_user_camera();
        void keep_eye_above_ground();
        void set_terrain_view();
        void place_camera();
        void prepare_scene();
        void update_terrains();
        void update_clutter();
        void report_clutter_perf();
        void draw_world();
        void setup_depth_layers();
        void setup_environment();
        void setup_local_sky();
        void setup_cloud_layers();
        void draw_building_panel_target();
        void render_panel_target(const kosmos::Cockpit &pcp, const std::string &directory);
        void draw_cockpit_targets();
        void draw_orbit_line();
        void draw_head_up();
        void render_vc_hud_target();
        void fill_hud_state(sl::HudState &hs);
        void draw_eva_overlay();
        void draw_body_labels();
        void draw_overlays();
        void update_sound();
        void draw_flight_readout(const std::string &soi_name);
        void draw_cockpit3d_overlay();
        void draw_panel2d();
        void draw_cockpit_hud_mfds(bool dashboard);
        void draw_mfd_overlay(bool dashboard, bool cockpit_mfd_overlay);
        void draw_hud();
        bool depth_layers_work = true;
        double backdrop{};
        std::size_t limb_used{};
        bool layered{};
        double near_plane{};
        double shell_clearance{}; // how far below the eye a lifted cloud shell must stay so the near plane cannot slice it
        sl::Camera ship_camera;
        sl::Environment3D environment;
        sl::Light3D light;
        std::vector<kosmos::PlacedLight> lamps;
        std::function<StarPlace(std::size_t)> place_star;
        std::function<glm::mat4(const StarPlace &, double, double, float)> star_basis_base;
        std::function<sl::Colour(const glm::vec3 &, float)> tint_of;
        std::function<void(const StarPlace &)> draw_photosphere;
        std::function<void(const sl::dvec3 &, const kosmos::Body &)> draw_atmosphere_glow;
        void scene_lighting();
        void scene_sky();
        void scene_star_glare();
        void draw_comet(std::size_t i, const kosmos::Body &b, const sl::dvec3 &rel, const sl::Light3D &body_light);
        void draw_body(std::size_t i, const sl::Light3D &body_light);
        void draw_terrain_body_weather(std::size_t i, const kosmos::Body &b);
        void draw_body_surface(std::size_t i, const kosmos::Body &b, const sl::dvec3 &rel, const glm::mat4 &model, sl::Material &rock);
        void draw_body_layers(std::size_t i, const kosmos::Body &b, const sl::dvec3 &rel, double distance, const glm::dquat &sphere_flip, const sl::Light3D &body_light);
        void draw_body_rings(std::size_t i, const kosmos::Body &b, const glm::mat4 &body_frame);
        void scene_bodies();
        void draw_ship_interior(int s, Ship &other, const sl::FlightBody &ob, const glm::mat4 &model);
        void draw_ship_cockpit3d(Ship &other, const sl::FlightBody &ob, const glm::mat4 &model);
        glm::mat4 vc_animation_matrix(Ship &other, const kosmos::VcAnimation &va);
        void scene_ships();
        void scene_weather();
        void scene_lightning();
        void scene_lamp_bulbs();
        void scene_exhaust_and_held();
        void draw_gui();
    void draw_systems_window();
    void draw_joystick_window();
    void draw_keys_window();
    void setup_link();
    void update_link();
    void draw_link_window();
        void draw_link_ui();
        void draw_link_3d();
    void shutdown_link();
    void refresh_menu_keys();
    void settings_graphics();
    void settings_weather();
    void settings_clutter();
    void settings_worlds();
    void draw_performance_window();
    void draw_icbm_window();
    bool ui_mouse_down() const;
        void ui_drag(int id, bool inside, bool down, bool pressed, bool consumed, float &offset_x, float &offset_y);
    void place_mfd(int side, kosmos::MfdLayout &layout);
    void drag_mfd(int side, kosmos::MfdLayout &layout, bool down, bool pressed, bool consumed);
    void draw_settings_window();
    void part_converted_mesh(kosmos::ShipSpec &fspec);
    void part_paused_controls();
    void part_live_controls(kosmos::Ship &flown, kosmos::ShipSpec &fspec);
    void draw_part_window();
    void draw_quantum_window();
    void camera_editor_buttons(bool &changed, int count);
    void camera_editor_list();
    void camera_editor_fields(bool &changed);
    void draw_camera_editor_window();
    void draw_camera_window();
        void present();
        void shutdown();

        // One pass of the main loop; false ends it.
        bool run_frame();
    };
} // namespace kosmos::app
