#include "app_state.h"

#include <cstring>

namespace kosmos::app
{
    // Orbiter's stepped field of view: the next preset wider or narrower than the current one.
    void App::step_fov(int direction)
    {
        static const double presets[] = {10.0, 20.0, 30.0, 40.0, 50.0, 60.0, 70.0, 80.0, 90.0};
        double next = view_fov;
        if (direction > 0)
        {
            next = presets[8];
            for (int i = 8; i >= 0; --i)
                if (presets[i] > view_fov + 0.5)
                    next = presets[i];
        }
        else
        {
            next = presets[0];
            for (double p : presets)
                if (p < view_fov - 0.5)
                    next = p;
        }
        rig.fov_user = next;
        sim.log("Field of view " + std::to_string(static_cast<int>(next + 0.5)) + " deg");
    }

    void App::restart_scenario()
    {
        if (flight_replay.capturing)
            stop_flight_recording();
        if (flight_replay.playing)
        {
            stop_flight_playback();
            if (flight_replay.original_valid)
                return;
        }
        vis.clear_parts();
        Ship *restarted = start_scenario(sim, options, terrain_set);
        if (!restarted)
        {
            std::fprintf(stderr, "kosmos: unable to restart flight\n");
            return;
        }
        ship = restarted;
        eva.active = false;
        arsenal.reset();
        rig.cam_dyn.reset();
        rig.dyn_valid = false;
        rig.cam_distance = ship->spec().camera_distance;
    }

    void App::drop_bomb(bool special)
    {
        Ship *flown = sim.active_ship();
        int index = -1;
        // Shift releases from the last loaded hardpoint (the special store) instead of the first.
        if (flown && special)
            for (int i = static_cast<int>(flown->spec().hardpoints.size()) - 1; i >= 0 && index < 0; --i)
                if (arsenal.rack_count(*flown, static_cast<std::size_t>(i)) > 0)
                    index = i;
        if (flown && !arsenal.release(arsenal_ctx, *flown, index))
            sim.log("Nothing to release");
    }

    void App::next_scenario()
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

    void App::cycle_mfd_display()
    {
        const bool on_dashboard = cockpit_view && !cockpit3d && ship->spec().cockpit.present && ship->spec().cockpit.mfds &&
                                  !map_mode && rig.interior_cam < 0 && rig.base_view < 0;
        // The see-through HUD steps through both displays, the left one and the right one before moving on.
        const bool in_hud = on_dashboard ? cockpit_mfd == 1 : mfd_display == 2;
        if (hud_sides == 0 && (on_dashboard ? cockpit_mfd > 0 : mfd_display > 0))
        {
            hud_sides = 3;
            return;
        }
        if (in_hud && hud_sides == 3)
        {
            hud_sides = 1;
            return;
        }
        if (in_hud && hud_sides == 1)
        {
            hud_sides = 2;
            return;
        }
        hud_sides = 3;
        if (on_dashboard)
            cockpit_mfd = (cockpit_mfd + 1) % 3;
        else
            mfd_display = (mfd_display + 1) % 3;
    }

    void App::cycle_camera()
    {
        rig.free_yaw = rig.free_pitch = 0.0f;
        // C cycles chase view, 2D cockpit, (for ships with MFDs) the 3D cockpit, then each camera from the Camera editor.
        rig.interior_cam = rig.base_view = -1;
        if (rig.user_cam >= 0)
        {
            if (++rig.user_cam >= static_cast<int>(rig.user_cams.size()))
                rig.user_cam = -1;
        }
        else if (!cockpit_view)
            cockpit_view = true, cockpit3d = false;
        else if (!cockpit3d && ship->spec().cockpit.mfds)
            cockpit3d = true;
        else
        {
            cockpit_view = cockpit3d = false;
            if (!rig.user_cams.empty())
                rig.user_cam = 0;
        }
        rig.look_yaw = rig.look_pitch = 0.0f;
    }

    void App::switch_to_next_ship()
    {
    sl::PropertyTree &props = *props_ref;
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
        rig.cam_distance = ship->spec().camera_distance;
        rig.look_yaw = rig.look_pitch = 0.0f;
        rig.interior_cam = -1;
    }

    void App::jump_to_player()
    {
    kosmos::Universe &universe = *universe_ref;
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

    void App::on_key_down()
    {
    kosmos::Universe &universe = *universe_ref;
    sl::PropertyTree &props = *props_ref;
        const auto key = event.key();
        const kosmos::KeyMods mods{sl::key_down(sl::Key::left_shift) || sl::key_down(sl::Key::right_shift),
                                   sl::key_down(sl::Key::left_control) || sl::key_down(sl::Key::right_control),
                                   sl::key_down(sl::Key::left_alt) || sl::key_down(sl::Key::right_alt)};
        auto hit = [&](const char *id) { return keys.pressed(id, key, mods); };
        if (keys.learning())
        {
            bool done = false;
            const std::string taken = keys.learn_key(key, mods, done);
            if (done)
            {
                keys_status = taken.empty() ? "" : "Taken from: " + taken;
                keys.save();
            }
        }
        else if (keys.press_custom(props, key, mods))
        {
        }
        else if (hit("window_cameras"))
            windows.camera_window = !windows.camera_window;
        else if (hit("window_settings"))
            windows.settings_window = !windows.settings_window;
        else if (hit("window_properties"))
            windows.property_window = !windows.property_window;
        else if (hit("window_systems"))
            windows.systems_window = !windows.systems_window;
        else if (hit("free_pointer"))
            pointer_free = !pointer_free;
        else if (hit("window_lua"))
            windows.lua_window = !windows.lua_window;
        else if (hit("window_multiplayer"))
            windows.net_window = !windows.net_window;
        else if (key == sl::Event::Key::escape)
        {
            if (!menu_bar.dismiss(menu_windows))
                running = false;
        }
        else if (hit("restart"))
        {
        restart_scenario();
        }
        else if (eva.active && !map_mode && key >= sl::Event::Key::digit_1 && key <= sl::Event::Key::digit_9)
            arsenal.select(static_cast<int>(key) - static_cast<int>(sl::Event::Key::digit_1));
        else if (!eva.active && !map_mode && (hit("bomb") || hit("bomb_special")))
        {
        drop_bomb(hit("bomb_special"));
        }
        else if (eva.active && (hit("throttle_full") || hit("throttle_zero") || hit("stage") || hit("autopilot") || hit("sas") || hit("gear") || hit("undock") || hit("hover_on") ||
                                hit("hover_off") || hit("cycle_camera") || hit("next_ship") || hit("hud_mode") || hit("hud_colour") || hit("jump_player")))
        {
            // On foot these ship keys do nothing; Space jumps (Ctrl crouches) and is read as a held key.
        }
        else if (eva.active && !map_mode && hit("flaps"))
        {
            if (eva.suit.jetpack)
            {
                eva.pack = !eva.pack;
                sim.log(eva.pack ? "Manoeuvring pack on" : "Manoeuvring pack off");
            }
        }
        else if (hit("throttle_full"))
        {
            sim.cancel_autoland("Autoland cancelled: manual throttle");
            sim.cancel_timed_burn("Timed burn cancelled: manual throttle");
            sim.cancel_vessel_ascent("Ascent cancelled: manual throttle");
            props.set_bool("/autopilot/enabled", false);
            props.set_double("/controls/throttle", 1.0);
        }
        else if (hit("throttle_zero"))
        {
            sim.cancel_autoland("Autoland cancelled: manual throttle");
            sim.cancel_timed_burn("Timed burn cancelled: manual throttle");
            sim.cancel_vessel_ascent("Ascent cancelled: manual throttle");
            props.set_bool("/autopilot/enabled", false);
            props.set_double("/controls/throttle", 0.0);
        }
        else if (hit("reset_look"))
        {
            rig.look_yaw = rig.look_pitch = 0.0f;
            rig.eva_look_yaw = rig.eva_look_pitch = 0.0;
            sim.log("View reset to forwards");
        }
        else if (hit("hover_on"))
        {
            sim.cancel_autoland("Autoland cancelled: manual hover");
            sim.cancel_timed_burn("Timed burn cancelled: manual hover");
            sim.cancel_vessel_ascent("Ascent cancelled: manual hover");
            props.set_double("/controls/hover", 1.0);
        }
        else if (hit("hover_off"))
        {
            sim.cancel_autoland("Autoland cancelled: manual hover");
            sim.cancel_timed_burn("Timed burn cancelled: manual hover");
            sim.cancel_vessel_ascent("Ascent cancelled: manual hover");
            props.set_double("/controls/hover", 0.0);
        }
        else if (hit("stage"))
        {
            sim.cancel_autoland("Autoland cancelled: staging");
            sim.cancel_timed_burn("Timed burn cancelled: staging");
            sim.cancel_vessel_ascent("Ascent cancelled: staging");
            props.set_bool("/controls/stage", true);
        }
        else if (hit("chute_deploy"))
        {
            sim.cancel_autoland("Autoland cancelled: parachute deployed");
            sim.cancel_vessel_ascent("Ascent cancelled: parachute deployed");
            props.set_int("/controls/parachute", 1);
        }
        else if (hit("chute_cut"))
        {
            sim.cancel_autoland("Autoland cancelled: parachute cut");
            sim.cancel_vessel_ascent("Ascent cancelled: parachute cut");
            props.set_int("/controls/parachute", -1);
        }
        else if (hit("autopilot"))
        {
            if (sim.vessel_ascent_active())
            {
                sim.cancel_vessel_ascent("Ascent cancelled");
                return;
            }
            if (props.get_bool("/autopilot/timed/armed"))
            {
                sim.cancel_timed_burn("Timed burn cancelled");
                return;
            }
            if (props.get_bool("/autopilot/land/armed"))
            {
                sim.cancel_autoland("Autoland cancelled");
                return;
            }
            const bool enable = !props.get_bool("/autopilot/enabled");
            props.set_bool("/autopilot/enabled", enable);
            if (!enable)
                props.set_string("/autopilot/status", "");
        }
        else if (hit("sas") || hit("sas_prograde") || hit("sas_retrograde") || hit("sas_normal") || hit("sas_antinormal"))
        {
            sim.cancel_autoland("Autoland cancelled: manual SAS");
            sim.cancel_timed_burn("Timed burn cancelled: manual SAS");
            sim.cancel_vessel_ascent("Ascent cancelled: manual SAS");
            const char *mode = hit("sas_prograde") ? "prograde" : hit("sas_retrograde") ? "retrograde" : hit("sas_normal") ? "normal" : hit("sas_antinormal") ? "antinormal" : "stability";
            props.set_bool("/autopilot/enabled", false);
            for (int m = 0; m < sas_mode_count; ++m)
                if (!std::strcmp(sas_modes[m], mode))
                    sas_index = m;
            props.set_string("/controls/sas", mode);
            sim.log(std::string("SAS: ") + mode);
        }
        else if (hit("rcs_mode") || hit("rcs_off"))
        {
            sim.cancel_vessel_ascent("Ascent cancelled: manual RCS mode");
            const std::string was = props.get_string("/controls/rcs-mode", "rot");
            const std::string now = hit("rcs_off") ? (was == "off" ? "rot" : "off") : (was == "lin" ? "rot" : "lin");
            props.set_string("/controls/rcs-mode", now);
            sim.log(now == "off" ? "RCS: off" : now == "lin" ? "RCS: linear" : "RCS: rotation");
        }
        else if (hit("pause"))
            props.set_bool("/sim/paused", !props.get_bool("/sim/paused"));
        else if (hit("record"))
            menu_toggle_recording();
        else if (hit("quit"))
            running = false;
        else if (hit("fov_step_up") || hit("fov_step_down"))
            step_fov(hit("fov_step_up") ? 1 : -1);
        else if (hit("next_scenario") && !sim.scenarios().empty())
        {
        next_scenario();
        }
        else if (hit("mfd_display"))
        {
        cycle_mfd_display();
        }
        else if (hit("mfd_left_mode") || hit("mfd_right_mode"))
        {
            const int side = hit("mfd_left_mode") ? 0 : 1;
            const int count = static_cast<int>(sim.mfd.modes().size());
            if (count > 0)
                sim.mfd.set_mode(side, (sim.mfd.unit(side).mode + 1) % count);
        }
        else if (hit("mfd_left_button") || hit("mfd_right_button"))
            sim.mfd.press(hit("mfd_left_button") ? 0 : 1, 12);
        else if (hit("gear") && ship->spec().retractable_gear)
        {
            const bool down = props.get_double("/controls/gear") > 0.5;
            props.set_double("/controls/gear", down ? 0.0 : 1.0);
            sound.play(down ? "gear_up" : "gear_down", 200);
        }
        else if (hit("nav_lights") || hit("lamps"))
        {
            const char *path = hit("lamps") ? "/controls/lighting/lamps" : "/controls/lighting/nav";
            props.set_bool(path, !props.get_bool(path, hit("lamps")));
        }
        else if (hit("undock"))
            props.set_bool("/dock/undock", true);
        else if (hit("orbit_line"))
            props.set_bool("/sim/settings/orbit-line", !props.get_bool("/sim/settings/orbit-line"));
        else if (hit("map"))
            map_mode = !map_mode;
        else if (hit("galaxy_map"))
            galaxy_mode = !galaxy_mode;
        else if ((key == sl::Event::Key::return_key || key == sl::Event::Key::keypad_enter) && (sl::key_down(sl::Key::left_alt) || sl::key_down(sl::Key::right_alt)))
            sl::toggle_fullscreen();
        else if (!galaxy_mode && !map_mode && hit("enter_exit"))
            toggle_eva();
        else if (galaxy_mode && key == sl::Event::Key::arrow_right)
            gview.step_selection(1);
        else if (galaxy_mode && key == sl::Event::Key::arrow_left)
            gview.step_selection(-1);
        else if (galaxy_mode && hit("enter_exit") && gview.selected >= 0 && gview.selected != sim.system_index())
            dial_portal(gview.selected);
        else if (hit("cycle_camera"))
        {
        cycle_camera();
        }
        else if (hit("next_ship") && sim.ship_count() > 1)
        {
        switch_to_next_ship();
        }
        else if (hit("flaps") && map_mode)
        {
            do
                map.focus = map.focus + 1 >= static_cast<int>(universe.bodies.size()) ? -1 : map.focus + 1;
            while (map.focus >= 0 && universe.bodies[static_cast<std::size_t>(map.focus)].minor);
            map.metres_per_pixel = map.focus < 0 ? std::min(map.metres_per_pixel, 1.0e5) : std::max(universe.bodies[static_cast<std::size_t>(map.focus)].radius * 0.02, 1.0);
        }
        else if (hit("flaps") && !map_mode)
            props.set_double("/controls/flaps", std::fmod(props.get_double("/controls/flaps") + 0.34, 1.02));
        else if (hit("info"))
            show_info = !show_info;
        else if (hit("hud_mode") || hit("hud_colour"))
        {
            if (hit("hud_colour"))
                hud.next_colour();
            else
                hud.set_mode(next_hud_mode(hud.mode(), ship->spec().hud_modes()));
            if (hit("hud_mode"))
                sim.log(std::string("HUD mode: ") + (hud.mode() == sl::HudMode::off ? "off" : sl::hud_mode_name(hud.mode())));
            if (!(cockpit_view || rig.interior_cam >= 0))
                sim.log("HUD is shown in cockpit view (press C)");
        }
        else if (hit("jump_player"))
        {
        jump_to_player();
        }
        else if (hit("sound"))
            props.set_bool("/sim/settings/sound", !props.get_bool("/sim/settings/sound"));
        else if (hit("warp_up"))
            props.set_int("/sim/warp-index", std::min(props.get_int("/sim/warp-index") + 1, Sim::warp_levels - 1));
        else if (hit("warp_down"))
            props.set_int("/sim/warp-index", std::max(props.get_int("/sim/warp-index") - 1, 0));
    }

    // Window and input events.
    void App::poll_events()
    {
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
        on_key_down();
            }
            sl::display_handle_event(event);
        }
    }

    // Follows the window size and the toggles that keep their own flags.
    void App::follow_window()
    {
        // Follow the window: resizing or going fullscreen resizes the 3D surface to match.
        if (sl::screen_width() > 0 && sl::screen_height() > 0 && (sl::screen_width() != screen_w || sl::screen_height() != screen_h))
        {
            screen_w = sl::screen_width();
            screen_h = sl::screen_height();
        }
        const int want_msaa = std::clamp(sim.props.get_int("/sim/settings/gfx-msaa", 4), 1, 8);
        if (screen_w != surface_w || screen_h != surface_h || want_msaa != surface_msaa)
        {
            surface_msaa = want_msaa;
            if (sl::Surface3D *resized = sl::create_surface_3d(screen_w, screen_h, want_msaa))
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
        typing = sl::gui_available() && ImGui::GetIO().WantTextInput;
    }

    void App::pod_keys(bool on_foot)
    {
    sl::PropertyTree &props = *props_ref;
        const double throttle_rate = 0.5 * frame;
        if (!typing && !on_foot && ship->spec().has_pods())
        {
            // Engine pods: keypad 9 and 3 set their thrust, 8 and 2 swing them (forward and back), 4, 5 and 6 jump to retro, hover and forward.
            auto bump = [&](const char *path, double d) { props.set_double(path, std::clamp(props.get_double(path) + d, 0.0, 1.0)); };
            const double pod_step = throttle_rate * (keys.down("pod_thrust_up") ? 1.0 : keys.down("pod_thrust_down") ? -1.0 : 0.0);
            const double tilt_step = frame * 0.4 * (keys.down("pod_tilt_up") ? 1.0 : keys.down("pod_tilt_down") ? -1.0 : 0.0);
            for (const char *side : {"left", "right"})
            {
                bump((std::string("/controls/pod-") + side).c_str(), pod_step);
                bump((std::string("/controls/pod-tilt-") + side).c_str(), tilt_step);
            }
            const int preset = keys.down("pod_retro") ? 0 : keys.down("pod_hover") ? 1 : keys.down("pod_forward") ? 2 : -1;
            if (preset >= 0)
            {
                props.set_double("/controls/pod-tilt-left", preset * 0.5);
                props.set_double("/controls/pod-tilt-right", preset * 0.5);
            }
        }
    }

    void App::run_scripted_options()
    {
    sl::PropertyTree &props = *props_ref;
        for (; options.shot_stages > 0; --options.shot_stages)
            ship->stage_once();
        if (options.auto_stage)
        {
            // A scripted rocket ascent for videos: pitch over once half of the first stage's propellant is gone, drop each empty stage,
            // and light the next stage 5 s after the last one has fallen away.
            const double throttle = options.shot_throttle >= 0.0 ? options.shot_throttle : 1.0;
            // Engines light over 3 s rather than snapping to full thrust, with a thump as they catch.
            auto light_engines = [&] {
                script.ramp_clock = clock;
                sound.play("thump", 255);
            };
            if (options.launch_at >= 0.0 && script.ramp_clock < 0.0 && script.first_stage_clock < 0.0)
            {
                props.set_double("/controls/throttle", 0.0);
                if (clock >= options.launch_at)
                    light_engines();
            }
            if (script.ascent_fuel0 < 0.0 && ship->active_stage() == 0 && ship->telemetry().stage_fuel > 0.0 && ship->telemetry().thrust > 0.0)
                script.ascent_fuel0 = ship->telemetry().stage_fuel;
            if (script.pitchover_clock < 0.0 && script.ascent_fuel0 > 0.0 && ship->telemetry().stage_fuel < 0.5 * script.ascent_fuel0)
                script.pitchover_clock = clock;
            if (script.pitchover_clock >= 0.0)
            {
                props.set_string("/controls/sas", "attitude");
                props.set_double("/controls/target-heading", 90.0);
                props.set_double("/controls/target-pitch", std::max(40.0, 90.0 - 0.8 * (clock - script.pitchover_clock)));
            }
            if (script.ignite_clock >= 0.0)
            {
                props.set_double("/controls/throttle", 0.0);
                if (clock >= script.ignite_clock)
                {
                    script.ignite_clock = -1.0;
                    light_engines();
                }
            }
            else if (script.ramp_clock >= 0.0)
            {
                const double t = std::clamp((clock - script.ramp_clock) / 3.0, 0.0, 1.0);
                props.set_double("/controls/throttle", throttle * t * t * (3.0 - 2.0 * t));
            }
            if (script.ignite_clock < 0.0 && ship->active_stage() < ship->stage_count() - 1 && ship->telemetry().thrust < 1.0 && ship->telemetry().stage_fuel < 1.0 &&
                ship->telemetry().total_fuel > 1.0 && ship->stage_once())
            {
                if (script.first_stage_clock < 0.0)
                    script.first_stage_clock = clock;
                script.ignite_clock = clock + 5.0;
                script.ramp_clock = -1.0;
                props.set_double("/controls/throttle", 0.0);
                props.set_int("/sim/warp-index", 0);
            }
        }
        for (; script.set_at_done < options.set_at.size() && clock >= options.set_at[script.set_at_done].first; ++script.set_at_done)
        {
            {
                const std::string &assignment = options.set_at[script.set_at_done].second;
                const std::size_t eq = assignment.find('=');
                if (eq != std::string::npos)
                    for (const char *axis_name : {"pitch", "yaw", "roll", "rcs-x", "rcs-y", "rcs-z"})
                        if (assignment.substr(0, eq) == std::string("/controls/") + axis_name)
                            script.held_axes[axis_name] = std::atof(assignment.c_str() + eq + 1);
            }
            apply_sets(props, {options.set_at[script.set_at_done].second});
        }
        if (options.drop_at >= 0.0)
            while (script.drops_done < options.drop_count && clock >= options.drop_at + script.drops_done * options.drop_interval)
            {
                if (Ship *flown = sim.active_ship())
                    arsenal.release(arsenal_ctx, *flown, 0);
                ++script.drops_done;
            }
        if (options.shot_throttle >= 0.0 && !options.auto_stage)
            props.set_double("/controls/throttle", options.shot_throttle);
    }

    void App::read_joystick(bool on_foot)
    {
    sl::PropertyTree &props = *props_ref;
        if (joystick.learning())
            joystick.poll_learn();
        else if (!typing && !on_foot)
        {
            const kosmos::JoyResult joy = joystick.update(ship->flies_as_aircraft());
            for (const auto &[action, value] : joy.axes)
            {
                const std::string path = "/controls/" + action;
                props.set_double(path, std::clamp(props.get_double(path) + static_cast<double>(value), -1.0, 1.0));
            }
            for (const auto &[action, value] : joy.levels)
            {
                if (action == "throttle" || action == "hover")
                {
                    sim.cancel_autoland("Autoland cancelled: manual throttle");
                    sim.cancel_timed_burn("Timed burn cancelled: manual throttle");
                    sim.cancel_vessel_ascent("Ascent cancelled: manual throttle");
                    if (action == "throttle")
                        props.set_bool("/autopilot/enabled", false);
                }
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
                {
                    sim.cancel_autoland("Autoland cancelled: staging");
                    sim.cancel_timed_burn("Timed burn cancelled: staging");
                    sim.cancel_vessel_ascent("Ascent cancelled: staging");
                    props.set_bool("/controls/stage", true);
                }
                else if (action == "autopilot")
                {
                    if (sim.vessel_ascent_active())
                    {
                        sim.cancel_vessel_ascent("Ascent cancelled");
                        continue;
                    }
                    if (props.get_bool("/autopilot/timed/armed"))
                    {
                        sim.cancel_timed_burn("Timed burn cancelled");
                        continue;
                    }
                    if (props.get_bool("/autopilot/land/armed"))
                    {
                        sim.cancel_autoland("Autoland cancelled");
                        continue;
                    }
                    const bool enable = !props.get_bool("/autopilot/enabled");
                    props.set_bool("/autopilot/enabled", enable);
                    if (!enable)
                        props.set_string("/autopilot/status", "");
                }
                else if (action == "sas")
                {
                    sim.cancel_autoland("Autoland cancelled: manual SAS");
                    sim.cancel_timed_burn("Timed burn cancelled: manual SAS");
                    sim.cancel_vessel_ascent("Ascent cancelled: manual SAS");
                    props.set_bool("/autopilot/enabled", false);
                    sas_index = (sas_index + 1) % sas_mode_count;
                    props.set_string("/controls/sas", sas_modes[sas_index]);
                }
                else if (action == "throttle-zero" || action == "throttle-full")
                {
                    sim.cancel_autoland("Autoland cancelled: manual throttle");
                    sim.cancel_timed_burn("Timed burn cancelled: manual throttle");
                    sim.cancel_vessel_ascent("Ascent cancelled: manual throttle");
                    props.set_bool("/autopilot/enabled", false);
                    props.set_double("/controls/throttle", action == "throttle-full" ? 1.0 : 0.0);
                }
                else if (action == "hover-on" || action == "hover-off")
                {
                    sim.cancel_autoland("Autoland cancelled: manual hover");
                    sim.cancel_timed_burn("Timed burn cancelled: manual hover");
                    sim.cancel_vessel_ascent("Ascent cancelled: manual hover");
                    props.set_double("/controls/hover", action == "hover-on" ? 1.0 : 0.0);
                }
            }
        }
    }

    // Keyboard and joystick controls.
    void App::keyboard()
    {
    sl::PropertyTree &props = *props_ref;
        // Keyboard controls write the property tree; scripts and the cockpit use the same paths.
        const double throttle_rate = 0.5 * frame;
        const double throttle = props.get_double("/controls/throttle");
        const bool on_foot = eva.active;
        eva_moving = on_foot && !typing &&
                     (options.eva_walk || keys.down("walk_forward") || keys.down("walk_left") || keys.down("walk_back") || keys.down("walk_right") || keys.down("crouch") || keys.down("jump"));
        keys.update_custom(props, typing);
        if (!typing && !on_foot && (keys.down("throttle_up") || keys.down("throttle_down") || keys.down("hover_up") || keys.down("hover_down")))
        {
            sim.cancel_autoland("Autoland cancelled: manual throttle");
            sim.cancel_timed_burn("Timed burn cancelled: manual throttle");
            sim.cancel_vessel_ascent("Ascent cancelled: manual throttle");
        }
        if (!typing && !on_foot && keys.down("throttle_up"))
            props.set_double("/controls/throttle", std::min(1.0, throttle + throttle_rate));
        if (!typing && !on_foot && keys.down("throttle_down"))
        {
            props.set_bool("/autopilot/enabled", false);
            // A wheeled vehicle's throttle goes below zero to reverse, and so does a ship with reverse thrust.
            props.set_double("/controls/throttle", std::max(ship->spec().ground() || ship->spec().reverse_throttle ? -1.0 : 0.0, throttle - throttle_rate));
        }
        const double hover = props.get_double("/controls/hover");
        if (!typing && !on_foot && keys.down("hover_up"))
            props.set_double("/controls/hover", std::min(1.0, hover + throttle_rate));
        if (!typing && !on_foot && keys.down("hover_down"))
            props.set_double("/controls/hover", std::max(0.0, hover - throttle_rate));
        pod_keys(on_foot);
        run_scripted_options();
        auto axis = [this, on_foot](const char *plus, const char *minus) { return typing || on_foot ? 0.0 : (keys.down(plus) ? 1.0 : 0.0) - (keys.down(minus) ? 1.0 : 0.0); };
        // Keypad RCS as in Orbiter: rotation, linear (2/8 up/down, 1/3 left/right, 6/9 forward/back) or off; Ctrl gives a tenth of the power.
        const std::string rcs_mode = props.get_string("/controls/rcs-mode", "rot");
        const bool rcs_off = rcs_mode == "off", rcs_lin = rcs_mode == "lin";
        const double fine = sl::key_down(sl::Key::left_control) || sl::key_down(sl::Key::right_control) ? 0.1 : 1.0;
        // In the air the same keys drive the control surfaces, so the RCS switch only silences them in space.
        const bool aero = ship->flies_as_aircraft();
        const double rot_scale = (rcs_off || rcs_lin) && !aero ? 0.0 : fine;
        props.set_double("/controls/pitch", axis("pitch_up", "pitch_down") * rot_scale);
        if (const double trim_move = axis("trim_up", "trim_down"); trim_move != 0.0)
            props.set_double("/controls/trim", std::clamp(props.get_double("/controls/trim") + trim_move * 0.1 * frame, -1.0, 1.0));
        props.set_double("/controls/yaw", axis("yaw_left", "yaw_right") * rot_scale);
        props.set_double("/controls/roll", axis("roll_left", "roll_right") * rot_scale);
        const double lin_scale = rcs_lin && !aero ? fine : 0.0;
        const double lin_x = axis("yaw_right", "yaw_left") * lin_scale;
        const double lin_y = axis("pitch_up", "pitch_down") * lin_scale;
        const double lin_z = axis("roll_right", "rcs_back") * lin_scale;
        props.set_double("/controls/rcs-z", lin_z);
        props.set_double("/controls/brake", !typing && !on_foot && (keys.down("brake") || props.get_double("/controls/brake-latch") > 0.5) ? 1.0 : 0.0);
        // In the cockpit the arrow keys move the head instead of firing RCS translation.
        const bool head_keys = !typing && !on_foot && cockpit_view && rig.interior_cam < 0 && rig.base_view < 0 && !galaxy_mode && !map_mode;
        if (rig.eye_ship != ship || !cockpit_view)
        {
            rig.eye_ship = ship;
            rig.eye_shift = sl::dvec3(0.0);
        }
        if (head_keys)
        {
            const double step = 0.6 * frame;
            rig.eye_shift.z -= axis("rcs_up", "rcs_down") * step;
            rig.eye_shift.x += axis("rcs_right", "rcs_left") * step;
            rig.eye_shift.y += axis("head_up", "head_down") * step;
            rig.eye_shift = glm::clamp(rig.eye_shift, sl::dvec3(-0.8), sl::dvec3(0.8));
        }
        if (const std::string request = props.get_string("/sim/hud-request"); !request.empty())
        {
            sl::HudMode requested = sl::HudMode::off;
            if (request == "off" || sl::hud_parse_mode(request, requested))
                hud.set_mode(requested);
            props.set_string("/sim/hud-request", "");
        }
        const double arrows_x = head_keys || rcs_off ? 0.0 : axis("rcs_right", "rcs_left");
        const double arrows_y = head_keys || rcs_off ? 0.0 : axis("rcs_up", "rcs_down");
        props.set_double("/controls/rcs-x", std::clamp(arrows_x + lin_x, -1.0, 1.0));
        props.set_double("/controls/rcs-y", std::clamp(arrows_y + lin_y, -1.0, 1.0));
        for (const auto &[name, value] : script.held_axes)
            props.set_double("/controls/" + name, value);
        read_joystick(on_foot);
        const bool manual_rotation = std::fabs(props.get_double("/controls/pitch")) + std::fabs(props.get_double("/controls/yaw")) + std::fabs(props.get_double("/controls/roll")) > 0.02;
        if (manual_rotation)
        {
            const double pitch = props.get_double("/controls/pitch");
            const double yaw = props.get_double("/controls/yaw");
            const double roll = props.get_double("/controls/roll");
            sim.cancel_autoland("Autoland cancelled: manual attitude");
            sim.cancel_timed_burn("Timed burn cancelled: manual attitude");
            sim.cancel_vessel_ascent("Ascent cancelled: manual attitude");
            props.set_double("/controls/pitch", pitch);
            props.set_double("/controls/yaw", yaw);
            props.set_double("/controls/roll", roll);
        }
        if (props.get_bool("/autopilot/enabled") && manual_rotation)
            props.set_bool("/autopilot/enabled", false);
    }
} // namespace kosmos::app
