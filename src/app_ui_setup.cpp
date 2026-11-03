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
        drag.mfd_mouse_prev = false;
        cockpit3d = options.cockpit3d;
        // Camera selection (F2): a ship's interior camera or a base camera, besides the chase and cockpit views.
        rig.interior_cam = -1;
        rig.base_view = -1;
        rig.base_view_cam = 0;
        // Cameras from the Camera editor (an orbiting external view to begin with); user_cam is the one looked through, or -1.
        rig.user_cam_file = kosmos::user_camera_path(kosmos::Sim::user_dir());
        rig.user_cams = kosmos::load_user_cameras(rig.user_cam_file);
        rig.user_cam = options.user_camera >= 0 && options.user_camera < static_cast<int>(rig.user_cams.size()) ? options.user_camera : -1;
        windows.camera_editor_window = options.camera_editor;
        rig.camera_editor_pick = 0;
        windows.systems_window = options.systems_window;
        windows.camera_window = options.camera_window;
        windows.settings_window = options.settings_window;
        windows.performance_window = options.performance_window;
        windows.property_window = options.property_window;
        windows.lua_window = options.lua_window;
        windows.net_window = options.net_window;
        // Part editor: meshes are edited in Blender while the simulation waits (the pause property is restored afterwards).
        windows.part_window = options.part_window;
        part_tool.edit_paused = false;
        part_tool.edit_was_paused = false;
        part_tool.model_poll = 0.0;
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
                    rig.base_view = static_cast<int>(i), rig.base_view_cam = colon == std::string::npos ? 0 : std::atoi(options.view_base.c_str() + colon + 1);
        }
        sl::gui_init();
        targets.mfd_target[0] = sl::create_render_target(mfd_target_size, mfd_target_size);
        targets.mfd_target[1] = sl::create_render_target(mfd_target_size, mfd_target_size);
        targets.hud_target = sl::create_render_target(mfd_target_size, mfd_target_size);
        if (targets.hud_target)
            targets.hud_target->smooth = true;
        // The boards are minified on screen; nearest sampling made thin lines shimmer as the ship rocked.
        for (sl::Bitmap *t : targets.mfd_target)
            if (t)
                t->smooth = true;
        targets.panel_target = nullptr;
        targets.panel_target_height = 0;
        vis.screen_quad = make_screen_quad();
        vis.cockpit_box = sl::create_cube_mesh(1.0f, {255, 255, 255, 255});
        targets.mfd_ship = nullptr;
        rig.look_yaw = 0.0f;
        rig.look_pitch = 0.0f;
        rig.eva_look_yaw = 0.0;
        rig.eva_look_pitch = 0.0;
        rig.eye_shift = sl::dvec3{0.0}; // the pilot's head moved from the seat with the arrow keys and Page Up/Down, in the ship's frame
        rig.eye_ship = nullptr;
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
            if (const std::string problem = keys.add_from_json(text.str(), "addon " + addon.id + " keys.json"); !problem.empty())
                sim.log(problem);
        }
        keys.load();
        setup_link();
        windows.joystick_window = false;
        windows.keys_window = options.keys_window;
        rig.cam_yaw = static_cast<float>(options.cam_yaw);
        rig.cam_pitch = static_cast<float>(options.cam_pitch);
        if (options.cockpit3d && (options.cam_yaw != 0.0 || options.cam_pitch != 0.0))
        {
            rig.look_yaw = static_cast<float>(glm::radians(options.cam_yaw));
            rig.look_pitch = static_cast<float>(glm::radians(options.cam_pitch));
        }
        rig.cam_distance = options.cam_distance > 0.0 ? options.cam_distance : ship->spec().camera_distance;
        // Right-drag in the chase view turns the camera on the spot; the ship moves across the screen instead of the camera swinging round it.
        rig.free_yaw = 0.0f;
        rig.free_pitch = 0.0f;
        rig.last_mouse_x = 0;
        rig.last_mouse_y = 0;
        screenshot_requested = false;
        // The menu item is clicked while its menu is open, so the shot waits a moment for the menu to close.
        screenshot_due_ms = 0;
        // Pinned open unless the menu is switched off from its View menu; --hide-info (screenshots) leaves it to slide away.
        menu_bar.keep_visible = options.menu_visible || !options.hide_info;
        menu_bar.scenario_editor_open = options.scenario_editor;
        menu_bar.aircraft_autopilot_open = options.aircraft_autopilot;
        menu_bar.spacecraft_autopilot_open = options.spacecraft_autopilot;
        menu_bar.base_editor_open = options.base_editor;
        menu_windows = {{"Part editor", "", &windows.part_window}, {"Cameras", "F2", &windows.camera_window}, {"Camera editor", "", &windows.camera_editor_window}, {"Settings", "F3", &windows.settings_window}, {"Performance settings", "", &windows.performance_window}, {"ICBM targeting", "", &windows.icbm_window}, {"Joystick", "", &windows.joystick_window}, {"Keyboard", "", &windows.keys_window}, {"Native addons", "", &windows.link_window}, {"Ship systems", "F1", &windows.systems_window}, {"Properties", "F4", &windows.property_window}, {"Lua console", "F11", &windows.lua_window}, {"Multiplayer", "F12", &windows.net_window}, {"Quantum portal", "", &quantum.quantum_window}};
        menu_actions.start_scenario = [this](const kosmos::Scenario &sc) { menu_start_scenario(sc); };
        menu_actions.restart = [this]() { menu_restart(); };
    return keep_going;
    }

    const sl::Terrain * App::terrain_of(int body)
    {
        for (const std::unique_ptr<BodyTerrain> &bt : body_terrains)
            if (bt->body == body)
                return bt->terrain.get();
        return nullptr;
    }

    // The ship nearest the astronaut's eye, by index, and how far away its origin is.
    int App::nearest_ship(double &distance)
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
    }

    // The door of a building is a point in its frame; this is where it is in the world.
    sl::dvec3 App::door_world(const Ship &building)
    {
        return building.origin() + building.body().direction_to_world(building.spec().door);
    }

    // A building whose door the astronaut stands at, or -1.
    int App::building_at_door()
    {
        const sl::dvec3 eye = eva.eye_world(sim.universe);
        for (int i = 0; i < sim.ship_count(); ++i)
            if (sim.ship(i)->spec().building() && glm::length(door_world(*sim.ship(i)) - eye) <= sim.ship(i)->spec().door_radius + 1.7)
                return i;
        return -1;
    }

    void App::board_ship(bool force)
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
        rig.cam_distance = ship->spec().camera_distance;
        rig.look_yaw = rig.look_pitch = 0.0f;
        sim.log("Back aboard " + ship->name);
    }

    void App::leave_eva()
    {
        const int door = building_at_door();
        if (eva.building >= 0 && walk.interior_panel >= 0)
        {
            // Use the panel: the building's own interface, with Enter to step back.
            walk.panel_return = eva.building;
            eva.active = false;
            sim.activate(eva.building);
            ship = sim.active_ship();
            cockpit_view = ship->spec().cockpit.present;
            cockpit3d = false;
            rig.look_yaw = rig.look_pitch = 0.0f;
            return;
        }
        if (eva.building >= 0)
        {
            Ship *inside = sim.ship(eva.building);
            if (door != eva.building)
            {
                sim.log("Go to the airlock hatch to leave");
                return;
            }
            if (!inside->spec().interior->airlock_name().empty())
            {
                if (!walk.in_airlock)
                {
                    sim.log("The way out is through the airlock");
                    return;
                }
                if (walk.room_kpa > kosmos::airlock_safe_kpa)
                {
                    inside->interior_state.airlock = kosmos::InteriorState::Airlock::venting;
                    sim.log("Airlock depressurising: wait, then press Enter again");
                    return;
                }
                inside->interior_state.airlock = kosmos::InteriorState::Airlock::vacuum;
            }
            const kosmos::Telemetry &bt = inside->telemetry();
            const sl::dvec3 spot = door_world(*inside);
            eva.building = -1;
            eva.leave(sim.universe, bt.soi, spot, inside->body().velocity, inside->origin() - spot, terrain_of(bt.soi), true);
            rig.eva_look_yaw = rig.eva_look_pitch = 0.0;
            sim.log("Outside " + inside->name);
            return;
        }
        if (door >= 0 && sim.ship(door)->spec().interior)
        {
            eva.building = door;
            walk.walker.place(*sim.ship(door)->spec().interior);
            if (sim.ship(door)->interior_state.airlock == kosmos::InteriorState::Airlock::vacuum)
            {
                sim.ship(door)->interior_state.airlock = kosmos::InteriorState::Airlock::normal;
                sim.log("Airlock repressurising");
            }
            rig.eva_look_yaw = rig.eva_look_pitch = 0.0;
            sim.log("Inside " + sim.ship(door)->name + ": Enter at the airlock hatch to go out");
            return;
        }
        if (door >= 0)
        {
            eva.active = false;
            sim.activate(door);
            ship = sim.active_ship();
            cockpit_view = ship->spec().cockpit.present;
            cockpit3d = false;
            rig.look_yaw = rig.look_pitch = 0.0f;
            sim.log("Inside " + ship->name);
            return;
        }
        board_ship(false);
        return;
    }

    void App::toggle_eva()
    {
        if (eva.active)
        {
            leave_eva();
            return;
        }
        Ship *flown = sim.active_ship();
        if (!flown)
            return;
        if (flown->spec().building() && walk.panel_return >= 0 && walk.panel_return < sim.ship_count() && sim.ship(walk.panel_return) == flown && flown->spec().interior)
        {
            eva.active = true;
            eva.building = walk.panel_return;
            walk.panel_return = -1;
            cockpit_view = cockpit3d = false;
            rig.eva_look_yaw = rig.eva_look_pitch = 0.0;
            return;
        }
        walk.panel_return = -1;
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
            rig.interior_cam = rig.base_view = rig.user_cam = -1;
            rig.eva_look_yaw = rig.eva_look_pitch = 0.0;
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
        const sl::dvec3 local_up = glm::normalize(origin - centre);
        const double reach = std::clamp(flown->spec().camera_distance * 0.25, 4.0, 25.0);
        sl::dvec3 side = fb.direction_to_world(sl::dvec3(-1.0, 0.0, 0.0));
        sl::dvec3 spot;
        if (t.landed)
        {
            side -= local_up * glm::dot(side, local_up);
            side = glm::length(side) > 1e-6 ? glm::normalize(side) : glm::normalize(glm::cross(sim.universe.pole(t.soi), local_up));
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
        rig.interior_cam = rig.base_view = rig.user_cam = -1;
        rig.eva_look_yaw = rig.eva_look_pitch = 0.0;
        sim.log("On foot: Enter near the ship to board");
    }

    // Helpers for boarding ships and walking.
    int App::init_helpers()
    {
        arsenal_ctx.sim = &sim;
        arsenal_ctx.eva = &eva;
        arsenal_ctx.terrain = [this](int body) { return terrain_of(body); };
        menu_actions.toggle_eva = [this]() { toggle_eva(); };
        menu_actions.key_label = [this](const char *id) { return keys.label_for(id); };
        refresh_menu_keys();
        menu_actions.on_foot = [&]() { return eva.active; };
        menu_actions.in_building = [&]() { return !eva.active && sim.active_ship() && sim.active_ship()->spec().building(); };
        menu_actions.show_panel = [this](int index) { menu_show_panel(index); };
        menu_actions.quit = [&]() { running = false; };
        menu_actions.screenshot = [&]() { screenshot_requested = true; screenshot_due_ms = sl::time_ms() + 300; };
    return keep_going;
    }

    // Video capture.
    int App::init_recording()
    {
        // Video capture: raw frames from the screen are piped to ffmpeg, which stamps them by wall clock and encodes 30 fps H.264.
        rec.record_pipe = nullptr;
        rec.record_pcm = nullptr;
        rec.record_frames = 0;
        rec.record_w = 0;
        rec.record_h = 0;
        rec.record_rate = 48000;
        rec.record_channels = 2;
        menu_actions.recording = [&]() { return rec.record_pipe != nullptr; };
        menu_actions.toggle_recording = [this]() { menu_toggle_recording(); };
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
    void App::menu_start_scenario(const kosmos::Scenario &sc)
    {
        vis.clear_parts();
        link_state.link_scenario = sc.id;
        const int previous_system = sim.system_index();
        bool world_rebuilt = false;
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
                    ship = nullptr;
                    running = false;
                    return;
                }
                world_rebuilt = true;
                map.focus = -1;
                quantum.quantum_selected = -1;
                gview.refresh(sim.galaxy, sim.system_index());
            }
        }
        // A scenario with its own date rebuilds the system on that date; one without keeps the clock it has.
        if (sc.epoch_jd > 0.0 && std::fabs(sc.epoch_jd - sim.universe.epoch_jd) > 1.0e-9)
        {
            std::string epoch_error;
            if (!change_date(sc.epoch_jd, epoch_error))
            {
                std::fprintf(stderr, "kosmos: cannot start on that date: %s\n", epoch_error.c_str());
                ship = nullptr;
                running = false;
                return;
            }
            world_rebuilt = true;
        }
        if (sc.fullscreen && !sl::is_fullscreen())
            sl::set_fullscreen(true);
        if (Ship *flown = apply_scenario(sim, sc, terrain_set))
        {
            ship = flown;
            rig.cam_distance = ship->spec().camera_distance;
            cockpit_view = cockpit3d = false;
            rig.interior_cam = rig.base_view = rig.user_cam = -1;
            rig.look_yaw = rig.look_pitch = 0.0f;
        }
        else if (world_rebuilt || sim.system_index() != previous_system)
        {
            ship = nullptr;
            running = false;
        }
    }

    void App::menu_restart()
    {
        vis.clear_parts();
        ship = start_scenario(sim, options, terrain_set);
        eva.active = false;
        arsenal.reset();
        ship_weapons.reset();
        rig.cam_distance = ship->spec().camera_distance;
    }

    void App::menu_show_panel(int index)
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
        rig.interior_cam = rig.base_view = -1;
        map_mode = galaxy_mode = false;
        rig.look_yaw = rig.look_pitch = 0.0f;
    }

    void App::flush_record_audio()
    {
        std::vector<std::int16_t> chunk;
        {
            std::lock_guard<std::mutex> lock(rec.record_audio_mutex);
            chunk.swap(rec.record_audio);
        }
        if (rec.record_pcm && !chunk.empty())
            std::fwrite(chunk.data(), sizeof(std::int16_t), chunk.size(), rec.record_pcm);
    }

    void App::stop_recording()
    {
        if (!rec.record_pipe)
            return;
        sl::audio_set_capture({});
#ifdef _WIN32
        _pclose(rec.record_pipe);
#else
        pclose(rec.record_pipe);
#endif
        rec.record_pipe = nullptr;
        flush_record_audio();
        bool audio = false;
        if (rec.record_pcm)
        {
            audio = std::ftell(rec.record_pcm) > 0;
            std::fclose(rec.record_pcm);
            rec.record_pcm = nullptr;
        }
        bool ok = true;
        if (audio)
        {
            const std::string mux = "ffmpeg -loglevel error -y -i \"" + rec.record_video + "\" -f s16le -ar " + std::to_string(rec.record_rate) +
                                    " -ac " + std::to_string(rec.record_channels) + " -i \"" + rec.record_pcm_file +
                                    "\" -c:v copy -c:a aac -b:a 192k -shortest -movflags +faststart \"" + rec.record_file + "\"";
            ok = std::system(mux.c_str()) == 0;
        }
        if (!audio || !ok)
            std::rename(rec.record_video.c_str(), rec.record_file.c_str());
        else
            std::remove(rec.record_video.c_str());
        std::remove(rec.record_pcm_file.c_str());
        sim.log("Saved " + rec.record_file + " (" + std::to_string(rec.record_frames) + " frames" + (audio && ok ? ", with sound)" : ")"));
    }

    void App::menu_toggle_recording()
    {
        if (rec.record_pipe)
        {
            stop_recording();
            return;
        }
        rec.record_w = sl::screen->width & ~1;
        rec.record_h = sl::screen->height & ~1;
        rec.record_file = "kosmos-" + std::to_string(sl::time_ms()) + ".mp4";
        rec.record_video = rec.record_file + ".video.mp4";
        rec.record_pcm_file = rec.record_file + ".pcm";
        const std::string command = "ffmpeg -loglevel error -y -use_wallclock_as_timestamps 1 -f rawvideo -pix_fmt rgba -s " +
                                    std::to_string(sl::screen->width) + "x" + std::to_string(sl::screen->height) +
                                    " -i - -vf \"crop=" + std::to_string(rec.record_w) + ":" + std::to_string(rec.record_h) +
                                    ",format=yuv420p\" -r 30 -c:v libx264 -preset veryfast -crf 20 -movflags +faststart \"" + rec.record_video + "\"";
#ifdef _WIN32
        rec.record_pipe = _popen(command.c_str(), "w");
#else
        rec.record_pipe = popen(command.c_str(), "w");
#endif
        rec.record_frames = 0;
        if (rec.record_pipe)
        {
            rec.record_pcm = std::fopen(rec.record_pcm_file.c_str(), "wb");
            if (rec.record_pcm && !sl::audio_set_capture(
                                  [&](const std::int16_t *samples, std::size_t frames)
                                  {
                                      std::lock_guard<std::mutex> lock(rec.record_audio_mutex);
                                      rec.record_audio.insert(rec.record_audio.end(), samples, samples + frames * static_cast<std::size_t>(rec.record_channels));
                                  },
                                  &rec.record_rate, &rec.record_channels))
            {
                std::fclose(rec.record_pcm);
                rec.record_pcm = nullptr;
            }
            sim.log("Recording " + rec.record_file);
        }
        else
            sim.log("Could not start ffmpeg");
    }

} // namespace kosmos::app
