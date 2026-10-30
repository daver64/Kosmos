#include "app_state.h"

namespace kosmos::app
{
    // Window and input events.
    void App::poll_events()
    {
    kosmos::Universe &universe = *universe_ref;
    sl::PropertyTree &props = *props_ref;
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
                const bool shift = sl::key_down(sl::Key::left_shift) || sl::key_down(sl::Key::right_shift);
                auto hit = [&](const char *id) { return keys.pressed(id, key, shift); };
                if (keys.learning())
                {
                    bool done = false;
                    const std::string taken = keys.learn_key(key, shift, done);
                    if (done)
                    {
                        keys_status = taken.empty() ? "" : "Taken from: " + taken;
                        keys.save();
                    }
                }
                else if (keys.press_custom(props, key, shift))
                {
                }
                else if (hit("window_cameras"))
                    camera_window = !camera_window;
                else if (hit("window_settings"))
                    settings_window = !settings_window;
                else if (hit("window_properties"))
                    property_window = !property_window;
                else if (hit("window_systems"))
                    systems_window = !systems_window;
                else if (hit("free_pointer"))
                    pointer_free = !pointer_free;
                else if (hit("window_lua"))
                    lua_window = !lua_window;
                else if (hit("window_multiplayer"))
                    net_window = !net_window;
                else if (key == sl::Event::Key::escape)
                {
                    if (!menu_bar.dismiss(menu_windows))
                        running = false;
                }
                else if (hit("restart"))
                {
                    vis.clear_parts();
                    ship = start_scenario(sim, options, terrain_set);
                    eva.active = false;
                    arsenal.reset();
                    cam_distance = ship->spec().camera_distance;
                }
                else if (eva.active && !map_mode && key >= sl::Event::Key::digit_1 && key <= sl::Event::Key::digit_9)
                    arsenal.select(static_cast<int>(key) - static_cast<int>(sl::Event::Key::digit_1));
                else if (!eva.active && !map_mode && (hit("bomb") || hit("bomb_special")))
                {
                    Ship *flown = sim.active_ship();
                    int index = -1;
                    // Shift releases from the last loaded hardpoint (the special store) instead of the first.
                    if (flown && hit("bomb_special"))
                        for (int i = static_cast<int>(flown->spec().hardpoints.size()) - 1; i >= 0 && index < 0; --i)
                            if (arsenal.rack_count(*flown, static_cast<std::size_t>(i)) > 0)
                                index = i;
                    if (flown && !arsenal.release(arsenal_ctx, *flown, index))
                        sim.log("Nothing to release");
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
                    props.set_bool("/autopilot/enabled", false);
                    props.set_double("/controls/throttle", 1.0);
                }
                else if (hit("throttle_zero"))
                {
                    props.set_bool("/autopilot/enabled", false);
                    props.set_double("/controls/throttle", 0.0);
                }
                else if (hit("hover_on"))
                    props.set_double("/controls/hover", 1.0);
                else if (hit("hover_off"))
                    props.set_double("/controls/hover", 0.0);
                else if (hit("stage"))
                    props.set_bool("/controls/stage", true);
                else if (hit("autopilot"))
                {
                    const bool enable = !props.get_bool("/autopilot/enabled");
                    props.set_bool("/autopilot/enabled", enable);
                    if (!enable)
                        props.set_string("/autopilot/status", "");
                }
                else if (hit("sas"))
                {
                    props.set_bool("/autopilot/enabled", false);
                    sas_index = (sas_index + 1) % sas_mode_count;
                    props.set_string("/controls/sas", sas_modes[sas_index]);
                }
                else if (hit("next_scenario") && !sim.scenarios().empty())
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
                else if (hit("mfd_display"))
                {
                    const bool on_dashboard = cockpit_view && !cockpit3d && ship->spec().cockpit.present && ship->spec().cockpit.mfds &&
                                              !map_mode && interior_cam < 0 && base_view < 0;
                    if (on_dashboard)
                        cockpit_mfd = (cockpit_mfd + 1) % 3;
                    else
                        mfd_display = (mfd_display + 1) % 3;
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
                    props.set_bool(path, !props.get_bool(path, true));
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
                {
                    jump_to = gview.selected;
                    jump_to_star_orbit = false;
                }
                else if (hit("cycle_camera"))
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
                else if (hit("next_ship") && sim.ship_count() > 1)
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
                    if (!(cockpit_view || interior_cam >= 0))
                        sim.log("HUD is shown in cockpit view (press C)");
                }
                else if (hit("jump_player"))
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
                else if (hit("sound"))
                    props.set_bool("/sim/settings/sound", !props.get_bool("/sim/settings/sound"));
                else if (hit("warp_up"))
                    props.set_int("/sim/warp-index", std::min(props.get_int("/sim/warp-index") + 1, Sim::warp_levels - 1));
                else if (hit("warp_down"))
                    props.set_int("/sim/warp-index", std::max(props.get_int("/sim/warp-index") - 1, 0));
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
        typing = sl::gui_available() && ImGui::GetIO().WantTextInput;
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
        if (!typing && !on_foot && keys.down("throttle_up"))
            props.set_double("/controls/throttle", std::min(1.0, throttle + throttle_rate));
        if (!typing && !on_foot && keys.down("throttle_down"))
        {
            props.set_bool("/autopilot/enabled", false);
            // A wheeled vehicle's throttle goes below zero to reverse.
            props.set_double("/controls/throttle", std::max(ship->spec().ground() ? -1.0 : 0.0, throttle - throttle_rate));
        }
        const double hover = props.get_double("/controls/hover");
        if (!typing && !on_foot && keys.down("hover_up"))
            props.set_double("/controls/hover", std::min(1.0, hover + throttle_rate));
        if (!typing && !on_foot && keys.down("hover_down"))
            props.set_double("/controls/hover", std::max(0.0, hover - throttle_rate));
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
        for (; options.shot_stages > 0; --options.shot_stages)
            ship->stage_once();
        if (options.auto_stage)
        {
            // A scripted rocket ascent for videos: pitch over once half of the first stage's propellant is gone, drop each empty stage,
            // and light the next stage 5 s after the last one has fallen away.
            const double throttle = options.shot_throttle >= 0.0 ? options.shot_throttle : 1.0;
            // Engines light over 3 s rather than snapping to full thrust, with a thump as they catch.
            auto light_engines = [&] {
                ramp_clock = clock;
                sound.play("thump", 255);
            };
            if (options.launch_at >= 0.0 && ramp_clock < 0.0 && first_stage_clock < 0.0)
            {
                props.set_double("/controls/throttle", 0.0);
                if (clock >= options.launch_at)
                    light_engines();
            }
            if (ascent_fuel0 < 0.0 && ship->active_stage() == 0 && ship->telemetry().stage_fuel > 0.0 && ship->telemetry().thrust > 0.0)
                ascent_fuel0 = ship->telemetry().stage_fuel;
            if (pitchover_clock < 0.0 && ascent_fuel0 > 0.0 && ship->telemetry().stage_fuel < 0.5 * ascent_fuel0)
                pitchover_clock = clock;
            if (pitchover_clock >= 0.0)
            {
                props.set_string("/controls/sas", "attitude");
                props.set_double("/controls/target-heading", 90.0);
                props.set_double("/controls/target-pitch", std::max(40.0, 90.0 - 0.8 * (clock - pitchover_clock)));
            }
            if (ignite_clock >= 0.0)
            {
                props.set_double("/controls/throttle", 0.0);
                if (clock >= ignite_clock)
                {
                    ignite_clock = -1.0;
                    light_engines();
                }
            }
            else if (ramp_clock >= 0.0)
            {
                const double t = std::clamp((clock - ramp_clock) / 3.0, 0.0, 1.0);
                props.set_double("/controls/throttle", throttle * t * t * (3.0 - 2.0 * t));
            }
            if (ignite_clock < 0.0 && ship->active_stage() < ship->stage_count() - 1 && ship->telemetry().thrust < 1.0 && ship->telemetry().stage_fuel < 1.0 &&
                ship->telemetry().total_fuel > 1.0 && ship->stage_once())
            {
                if (first_stage_clock < 0.0)
                    first_stage_clock = clock;
                ignite_clock = clock + 5.0;
                ramp_clock = -1.0;
                props.set_double("/controls/throttle", 0.0);
                props.set_int("/sim/warp-index", 0);
            }
        }
        for (; set_at_done < options.set_at.size() && clock >= options.set_at[set_at_done].first; ++set_at_done)
        {
            {
                const std::string &assignment = options.set_at[set_at_done].second;
                const std::size_t eq = assignment.find('=');
                if (eq != std::string::npos)
                    for (const char *axis_name : {"pitch", "yaw", "roll", "rcs-x", "rcs-y", "rcs-z"})
                        if (assignment.substr(0, eq) == std::string("/controls/") + axis_name)
                            held_axes[axis_name] = std::atof(assignment.c_str() + eq + 1);
            }
            apply_sets(props, {options.set_at[set_at_done].second});
        }
        if (options.drop_at >= 0.0)
            while (drops_done < options.drop_count && clock >= options.drop_at + drops_done * options.drop_interval)
            {
                if (Ship *flown = sim.active_ship())
                    arsenal.release(arsenal_ctx, *flown, 0);
                ++drops_done;
            }
        if (options.shot_throttle >= 0.0 && !options.auto_stage)
            props.set_double("/controls/throttle", options.shot_throttle);
        auto axis = [this, on_foot](const char *plus, const char *minus) { return typing || on_foot ? 0.0 : (keys.down(plus) ? 1.0 : 0.0) - (keys.down(minus) ? 1.0 : 0.0); };
        props.set_double("/controls/pitch", axis("pitch_up", "pitch_down"));
        props.set_double("/controls/yaw", axis("yaw_left", "yaw_right"));
        props.set_double("/controls/roll", axis("roll_left", "roll_right"));
        props.set_double("/controls/brake", !typing && !on_foot && keys.down("brake") ? 1.0 : 0.0);
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
            eye_shift.z -= axis("rcs_up", "rcs_down") * step;
            eye_shift.x += axis("rcs_right", "rcs_left") * step;
            eye_shift.y += axis("head_up", "head_down") * step;
            eye_shift = glm::clamp(eye_shift, sl::dvec3(-0.8), sl::dvec3(0.8));
        }
        if (const std::string request = props.get_string("/sim/hud-request"); !request.empty())
        {
            sl::HudMode requested = sl::HudMode::off;
            if (request == "off" || sl::hud_parse_mode(request, requested))
                hud.set_mode(requested);
            props.set_string("/sim/hud-request", "");
        }
        props.set_double("/controls/rcs-x", head_keys ? 0.0 : axis("rcs_right", "rcs_left"));
        props.set_double("/controls/rcs-y", head_keys ? 0.0 : axis("rcs_up", "rcs_down"));
        for (const auto &[name, value] : held_axes)
            props.set_double("/controls/" + name, value);
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
    }
} // namespace kosmos::app
