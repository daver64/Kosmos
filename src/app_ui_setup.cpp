#include "app_state.h"

namespace kosmos::app
{
    // Camera, window and menu state.
    int App::init_ui()
    {
        // In the 2D cockpit the MFDs are the dashboard by default; F5 steps dashboard, heads-up, off.
        cockpit_mfd = 0;
        first_ship_seen = true;
        panel_index = options.panel; // which of the ship's 2D panels the 2D cockpit shows
        mfd_mouse_prev = false;
        cockpit3d = options.cockpit3d;
        // Camera selection (F2): a ship's interior camera or a base camera, besides the chase and cockpit views.
        interior_cam = -1;
        base_view = -1;
        base_view_cam = 0;
        // Cameras from the Camera editor (an orbiting external view to begin with); user_cam is the one looked through, or -1.
        user_cam_file = kosmos::user_camera_path(kosmos::Sim::user_dir());
        user_cams = kosmos::load_user_cameras(user_cam_file);
        user_cam = options.user_camera >= 0 && options.user_camera < static_cast<int>(user_cams.size()) ? options.user_camera : -1;
        camera_editor_window = options.camera_editor;
        camera_editor_pick = 0;
        systems_window = options.systems_window;
        camera_window = options.camera_window;
        settings_window = options.settings_window;
        property_window = options.property_window;
        lua_window = options.lua_window;
        net_window = options.net_window;
        // Part editor: meshes are edited in Blender while the simulation waits (the pause property is restored afterwards).
        part_window = options.part_window;
        edit_paused = false;
        edit_was_paused = false;
        model_poll = 0.0;
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
        {
            const std::string settings_file = settings_path();
            lua_console.init(sim, sim.root() + "/data/scripts", settings_file.empty() ? std::string() : (std::filesystem::path(settings_file).parent_path() / "scripts").string());
        }
        kosmos::set_stock_model_folder(sim.root() + "/data");
        if (!kosmos::Sim::user_dir().empty())
            kosmos::set_base_model_folder((std::filesystem::path(kosmos::Sim::user_dir()) / "bases").string());
        if (!options.blender.empty())
            sim.props.set_string("/sim/settings/blender", options.blender);
        applied_volume = -1;
        if (!options.view_base.empty())
        {
            const std::size_t colon = options.view_base.find(':');
            const std::string id = options.view_base.substr(0, colon);
            for (std::size_t i = 0; i < sim.bases.size(); ++i)
                if (sim.bases[i].id == id)
                    base_view = static_cast<int>(i), base_view_cam = colon == std::string::npos ? 0 : std::atoi(options.view_base.c_str() + colon + 1);
        }
        sl::gui_init();
        mfd_target[0] = sl::create_render_target(mfd_target_size, mfd_target_size);
        mfd_target[1] = sl::create_render_target(mfd_target_size, mfd_target_size);
        // The boards are minified on screen; nearest sampling made thin lines shimmer as the ship rocked.
        for (sl::Bitmap *t : mfd_target)
            if (t)
                t->smooth = true;
        panel_target = nullptr;
        panel_target_height = 0;
        vis.screen_quad = make_screen_quad();
        vis.cockpit_box = sl::create_cube_mesh(1.0f, {255, 255, 255, 255});
        mfd_ship = nullptr;
        look_yaw = 0.0f;
        look_pitch = 0.0f;
        eva_look_yaw = 0.0;
        eva_look_pitch = 0.0;
        eye_shift = sl::dvec3{0.0}; // the pilot's head moved from the seat with the arrow keys and Page Up/Down, in the ship's frame
        eye_ship = nullptr;
        if (options.map_scale > 0.0)
            map.metres_per_pixel = options.map_scale;
        if (!options.map_focus.empty())
            map.focus = sim.universe.find(options.map_focus);
        sas_index = 0;
        // With the Earth relief option the terrain mesh (with sea glint) takes over at 300 km instead.
        // Altitude below which the terrain mesh replaces the tiled Earth sphere. Lower keeps the 64k imagery and sea glint longer, but the sphere has no relief, so peaks pop up at the swap.
        earth_sphere_switch_m = 20.0e3;
        sl::gamepad_init();
        joystick.load();
        for (const kosmos::Addon &addon : sim.addons())
        {
            std::ifstream in(std::filesystem::path(addon.directory) / "keys.json");
            if (!in)
                continue;
            std::stringstream text;
            text << in.rdbuf();
            if (const std::string error = keys.add_from_json(text.str(), "addon " + addon.id + " keys.json"); !error.empty())
                sim.log(error);
        }
        keys.load();
        setup_link();
        joystick_window = false;
        keys_window = options.keys_window;
        cam_yaw = static_cast<float>(options.cam_yaw);
        cam_pitch = static_cast<float>(options.cam_pitch);
        cam_distance = options.cam_distance > 0.0 ? options.cam_distance : ship->spec().camera_distance;
        // Right-drag in the chase view turns the camera on the spot; the ship moves across the screen instead of the camera swinging round it.
        free_yaw = 0.0f;
        free_pitch = 0.0f;
        last_mouse_x = 0;
        last_mouse_y = 0;
        screenshot_requested = false;
        // The menu item is clicked while its menu is open, so the shot waits a moment for the menu to close.
        screenshot_due_ms = 0;
        // Pinned open unless the menu is switched off from its View menu; --hide-info (screenshots) leaves it to slide away.
        menu_bar.keep_visible = options.menu_visible || !options.hide_info;
        menu_bar.scenario_editor_open = options.scenario_editor;
        menu_bar.aircraft_autopilot_open = options.aircraft_autopilot;
        menu_bar.spacecraft_autopilot_open = options.spacecraft_autopilot;
        menu_bar.base_editor_open = options.base_editor;
        menu_windows = {{"Part editor", "", &part_window}, {"Cameras", "F2", &camera_window}, {"Camera editor", "", &camera_editor_window}, {"Settings", "F3", &settings_window}, {"Joystick", "", &joystick_window}, {"Keyboard", "", &keys_window}, {"Native addons", "", &link_window}, {"Ship systems", "F1", &systems_window}, {"Properties", "F4", &property_window}, {"Lua console", "F11", &lua_window}, {"Multiplayer", "F12", &net_window}, {"Quantum navigation", "", &quantum_window}};
        menu_actions.start_scenario = [&](const kosmos::Scenario &sc)
        {
            vis.clear_parts();
            link_scenario = sc.id;
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
    return keep_going;
    }

    // Helpers for boarding ships and walking.
    int App::init_helpers()
    {
        terrain_of = [&](int body) -> const sl::Terrain *
        {
            for (const std::unique_ptr<BodyTerrain> &bt : body_terrains)
                if (bt->body == body)
                    return bt->terrain.get();
            return nullptr;
        };
        // The ship nearest the astronaut's eye, by index, and how far away its origin is.
        nearest_ship = [&](double &distance) -> int
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
        door_world = [&](const Ship &building) { return building.origin() + building.body().direction_to_world(building.spec().door); };
        // A building whose door the astronaut stands at, or -1.
        building_at_door = [&]() -> int
        {
            const sl::dvec3 eye = eva.eye_world(sim.universe);
            for (int i = 0; i < sim.ship_count(); ++i)
                if (sim.ship(i)->spec().building() && glm::length(door_world(*sim.ship(i)) - eye) <= sim.ship(i)->spec().door_radius + 1.7)
                    return i;
            return -1;
        };
        arsenal_ctx.sim = &sim;
        arsenal_ctx.eva = &eva;
        arsenal_ctx.terrain = terrain_of;
        board_ship = [&](bool force)
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
        toggle_eva = [&]()
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
        menu_actions.key_label = [this](const char *id) { return keys.label_for(id); };
        refresh_menu_keys();
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
    return keep_going;
    }

    // Video capture.
    int App::init_recording()
    {
        // Video capture: raw frames from the screen are piped to ffmpeg, which stamps them by wall clock and encodes 30 fps H.264.
        record_pipe = nullptr;
        record_pcm = nullptr;
        record_frames = 0;
        record_w = 0;
        record_h = 0;
        record_rate = 48000;
        record_channels = 2;
        flush_record_audio = [&]()
        {
            std::vector<std::int16_t> chunk;
            {
                std::lock_guard<std::mutex> lock(record_audio_mutex);
                chunk.swap(record_audio);
            }
            if (record_pcm && !chunk.empty())
                std::fwrite(chunk.data(), sizeof(std::int16_t), chunk.size(), record_pcm);
        };
        stop_recording = [&]()
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
    return keep_going;
    }

    // Last set-up before the loop.
    int App::init_loop()
    {
        loading.note("Settling the terrain");
        clock = 0.0;
        lightning_flash = 0.0;
        last_time = sl::time_ms();
        // The engine's own limiter only sleeps when vsync is off, so turn it off and let simlib pace the frames.
        sl::set_vsync(false);
        applied_fps = 0;
    return keep_going;
    }
} // namespace kosmos::app
