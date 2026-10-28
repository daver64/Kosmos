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

    struct App
    {
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
        std::function<void()> leave_system;
        std::function<void()> place_worlds;
        std::function<bool(double, std::string &)> change_date;
        std::function<void()> reload_world_textures;
        std::function<bool()> enter_system;
        Ship * ship{};
        sl::Hud hud;
        EngineSound sound;
        kosmos::Lightning lightning;
        bool sound_on{};
        bool first_frame{};
        bool show_orbit{};
        bool show_info{};
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
        bool mouse_captured{};
        bool pointer_free{};
        int mfd_display{};
        int cockpit_mfd{};
        bool first_ship_seen{};
        int panel_index{};
        bool mfd_mouse_prev{};
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
        sl::Bitmap * panel_target{};
        int panel_target_height{};
        const Ship * mfd_ship{};
        std::string mfd_ship_id;
        float look_yaw{};
        float look_pitch{};
        double eva_look_yaw{};
        double eva_look_pitch{};
        sl::dvec3 eye_shift;
        const Ship * eye_ship{};
        int sas_index{};
        double earth_sphere_switch_m{};
        kosmos::Joystick joystick;
        kosmos::KeyBindings keys;
        bool keys_window{};
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
        std::function<const sl::Terrain *(int)> terrain_of;
        std::function<int(double &)> nearest_ship;
        std::function<sl::dvec3 (const Ship &)> door_world;
        std::function<int()> building_at_door;
        kosmos::ArsenalContext arsenal_ctx;
        std::function<void(bool)> board_ship;
        std::function<void()> toggle_eva;
        FILE * record_pipe{};
        FILE * record_pcm{};
        std::string record_file;
        std::string record_video;
        std::string record_pcm_file;
        long record_frames{};
        int record_w{};
        int record_h{};
        int record_rate{};
        int record_channels{};
        std::mutex record_audio_mutex;
        std::vector<std::int16_t> record_audio;
        std::function<void()> flush_record_audio;
        std::function<void()> stop_recording;
        ExhaustFx exhaust_fx;
        sl::Event event_discard;
        double clock{};
        double lightning_flash{};
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
        double user_extent{};
        int soi{};
        sl::dvec3 soi_pos;
        bool home{};
        sl::dvec3 earth_pos;
        sl::dvec3 eye_from_earth;
        double eye_earth_distance{};
        sl::TerrainView view;
        bool near_earth{};
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

        int init_sim(int argc, char **argv);
        int init_start_date();
        int init_window();
        int init_render();
        int init_systems();
        int init_ship();
        int init_ui();
        int init_helpers();
        int init_recording();
        int init_loop();
        bool begin_frame();
        void apply_settings();
        void poll_events();
        void follow_window();
        void keyboard();
        void advance_time();
        void place_camera();
        void prepare_scene();
        void draw_world();
        void draw_overlays();
        void update_sound();
        void draw_hud();
        bool depth_layers_work = true;
        double backdrop{};
        std::size_t limb_used{};
        bool layered{};
        double near_plane{};
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
        void scene_bodies();
        void scene_ships();
        void scene_weather();
        void scene_lightning();
        void scene_lamp_bulbs();
        void scene_exhaust_and_held();
        void draw_gui();
    void draw_systems_window();
    void draw_joystick_window();
    void draw_keys_window();
    void refresh_menu_keys();
    void draw_settings_window();
    void draw_part_window();
    void draw_quantum_window();
    void draw_camera_editor_window();
    void draw_camera_window();
        void present();
        void shutdown();

        // One pass of the main loop; false ends it.
        bool run_frame();
    };
} // namespace kosmos::app
