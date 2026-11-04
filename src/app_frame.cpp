#include "app_state.h"
#include "watchdog.h" // WATCHDOG

namespace kosmos::app
{
    // The start of a frame, and the quantum jump between star systems.
    bool App::begin_frame()
    {
        // The command-line style wins for this run without being saved.
        gfx_style = options.render_style >= 0 ? options.render_style : sim.props.get_int("/sim/settings/gfx-style");
        gfx_clouds = sim.props.get_bool("/sim/settings/gfx-clouds") && gfx_style == 0;
        sl::set_render_style_3d(static_cast<sl::RenderStyle3D>(gfx_style));
        if (const int fps = sim.props.get_int("/sim/settings/gfx-max-fps"); fps != applied_fps)
        {
            sl::set_fps(fps);
            applied_fps = fps;
        }
        if (quantum.jump_to >= 0)
        {
            // The world stops while the next system loads; show a short hyperspace effect so the window never closes.
            const int target_system = quantum.jump_to;
            quantum.jump_to = -1;
            const std::string target_name = sim.galaxy.star(target_system).name;
            // change_system deletes every ship, so the flown ship's name is taken now.
            const std::string flown_name = ship ? ship->spec().name : std::string();
            const ShipPropellantState propellant_state = ship ? ship->propellant_state() : ShipPropellantState{};
            auto warp_frame = [&](float t)
            {
                while (sl::poll_event(&event_discard))
                {
                }
                sl::clear_to_colour(sl::screen, {0, 0, 0});
                const float cx = screen_w * 0.5f, cy = screen_h * 0.5f;
                std::uint32_t seed = 12345u;
                for (int i = 0; i < 260; ++i)
                {
                    seed = seed * 1664525u + 1013904223u;
                    const float a = static_cast<float>(seed >> 8) * (6.2831853f / 16777216.0f);
                    seed = seed * 1664525u + 1013904223u;
                    const float r0 = 0.05f + 0.95f * static_cast<float>(seed >> 8) / 16777216.0f;
                    const float r1 = r0 + t * (0.15f + r0) * 1.6f, r2 = r0 + t * 0.3f * r0;
                    const int shade = static_cast<int>(std::min(1.0f, 0.3f + t) * 255.0f * (0.4f + 0.6f * r0));
                    sl::line(sl::screen, static_cast<int>(cx + std::cos(a) * r2 * screen_w * 0.5f), static_cast<int>(cy + std::sin(a) * r2 * screen_h * 0.5f),
                                  static_cast<int>(cx + std::cos(a) * r1 * screen_w * 0.5f), static_cast<int>(cy + std::sin(a) * r1 * screen_h * 0.5f), {static_cast<std::uint8_t>(shade * 0.7f), static_cast<std::uint8_t>(shade * 0.85f), static_cast<std::uint8_t>(shade)});
                }
                const int fade = static_cast<int>(255.0f * std::clamp((t - 0.7f) / 0.3f, 0.0f, 1.0f));
                if (fade > 0)
                    sl::rectfill(sl::screen, 0, 0, screen_w, screen_h, {static_cast<std::uint8_t>(fade), static_cast<std::uint8_t>(fade), static_cast<std::uint8_t>(fade)});
                sl::gprintf_center(screen_h / 2 - 8, {200, 220, 255}, "Quantum jump to %s", target_name.c_str());
                sl::show_video_bitmap();
                sl::end_frame();
            };
            const std::uint32_t begin = sl::time_ms();
            warp_frame(0.0f);
            if (netplay.joined())
                netplay.leave();
            vis.clear_parts();
            leave_system();
            std::string jump_error;
            bool loaded = sim.change_system(target_system, jump_error) && enter_system();
            if (!loaded)
            {
                std::fprintf(stderr, "kosmos: quantum jump failed: %s\n", jump_error.c_str());
                return false;
            }
            options.scenario.clear();
            options.orbit_altitude = 0.0;
            options.orbit_body.clear();
            options.orbit_start = false;
            options.portal_arrival = true;
            if (!flown_name.empty())
                options.ship_name = flown_name;
            ship = start_scenario(sim, options, terrain_set, flown_name.empty() ? nullptr : &propellant_state);
            options.portal_arrival = false;
            quantum.dialled = -1;
            quantum.portal_open = 0.0f;
            quantum.prev_valid = false;
            eva.active = false;
            arsenal.reset();
            ship_weapons.reset();
            if (!ship)
                return false;
            rig.cam_distance = ship->spec().camera_distance;
            cockpit_view = cockpit3d = false;
            rig.interior_cam = rig.base_view = -1;
            rig.look_yaw = rig.look_pitch = 0.0f;
            map.focus = -1;
            quantum.quantum_selected = -1;
            galaxy_mode = false;
            exhaust_fx = ExhaustFx();
            gview.refresh(sim.galaxy, sim.system_index());
            // Hold the effect for at least a couple of seconds, fading to black before the new sky appears.
            for (std::uint32_t t = sl::time_ms() - begin; t < 2500; t = sl::time_ms() - begin)
                warp_frame(static_cast<float>(t) / 2500.0f);
            last_time = sl::time_ms();
        }
    return true;
    }

    // Reads the settings that change while running.
    void App::apply_settings()
    {
        update_world_textures();
        // The setting is a property, so the settings window, the F4 browser and scripts all change it the same way.
        if (sim.props.get_int("/sim/settings/world-texture") != generated_texture_width)
        {
            generated_texture_width = sim.props.get_int("/sim/settings/world-texture");
            reload_world_textures();
            last_time = sl::time_ms();
        }
        if (!options.test_scenario.empty() && clock > 2.0)
        {
            if (const kosmos::Scenario *next = sim.find_scenario(options.test_scenario))
            {
                const kosmos::Scenario chosen = *next;
                options.scenario = chosen.id;
                std::fprintf(stderr, "[test] switching to %s\n", chosen.id.c_str());
                menu_actions.start_scenario(chosen);
            }
            options.test_scenario.clear();
        }
        if (options.enter_building.empty() && clock > 1.0)
            options.enter_building = sim.props.get_string("/sim/enter-building");
        if (!options.enter_building.empty() && clock > 1.0)
        {
            sim.props.set_string("/sim/enter-building", "");
            for (int i = 0; i < sim.ship_count(); ++i)
                if (sim.ship(i)->spec().building() && sim.ship(i)->name.find(options.enter_building) != std::string::npos)
                {
                    sim.activate(i);
                    ship = sim.active_ship();
                    cockpit_view = true;
                    cockpit3d = options.cockpit3d;
                    if (ship->spec().interior)
                    {
                        // A building with a walkable interior starts the player on foot inside it (step out and straight back in).
                        toggle_eva();
                        toggle_eva();
                        options.eva = false;
                        cockpit_view = false;
                        if (options.interior_at && eva.building >= 0)
                        {
                            walk.walker.position = sl::dvec3(options.interior_pos[0], 0.0, options.interior_pos[1]);
                            walk.walker.yaw = glm::radians(options.interior_pos[2]);
                            walk.walker.pitch = glm::radians(options.interior_pos[3]);
                        }
                    }
                    break;
                }
            options.enter_building.clear();
        }
        if (!options.hold_item.empty() && eva.active)
        {
            for (std::size_t i = 0; i < arsenal.pack().size(); ++i)
                if (arsenal.pack()[i].id == options.hold_item)
                    arsenal.select(static_cast<int>(i));
            options.hold_item.clear();
        }
        if (options.eva && clock > 1.0)
        {
            options.eva = false;
            if (!eva.active)
                toggle_eva();
        }
        if (options.test_dial >= 0 && clock > 1.0)
        {
            dial_portal(options.test_dial);
            options.test_dial = -1;
        }
        if (options.test_jump >= 0 && clock > 1.0)
        {
            quantum.jump_to = options.test_jump;
            options.test_jump = -1;
        }
        const std::uint32_t now = sl::time_ms();
        frame = std::clamp(static_cast<double>(now - last_time) * 0.001, 0.001, 0.1);
        last_time = now;
        clock += frame;
        smooth_frame += (frame - smooth_frame) * 0.1;
        kosmos::Universe &universe = sim.universe; universe_ref = &universe;
        sl::PropertyTree &props = sim.props; props_ref = &props;
        {
            const double raw = last_frame_stamp ? std::clamp(static_cast<double>(now - last_frame_stamp) * 0.001, 0.0005, 1.0) : 1.0 / 60.0;
            last_frame_stamp = now;
            const double smooth = props.get_double("/sim/frame-time", raw) * 0.9 + raw * 0.1;
            props.set_double("/sim/frame-time", smooth);
            props.set_double("/sim/frame-rate", 1.0 / smooth);
        }

        event = sl::Event();
    }

    // Screenshots, video capture and the buffer swap.
    void App::present()
    {
        if (sl::gui_available())
            menu_bar.draw(sim, menu_windows, menu_actions);
        sl::gui_render();

        if (!options.shot.empty() && !shot_saved && clock >= options.shot_at)
        {
            sl::save_bitmap(sl::screen, options.shot);
            shot_saved = true;
            running = false;
        }
        if (screenshot_requested && sl::time_ms() >= screenshot_due_ms)
        {
            screenshot_requested = false;
            const std::string file = "kosmos-" + std::to_string(sl::time_ms()) + ".png";
            sl::save_bitmap(sl::screen, file);
            sim.log("Saved " + file);
        }
        if (options.record_seconds > 0.0)
        {
            if (!rec.record_pipe && rec.record_frames == 0)
                menu_actions.toggle_recording();
            else if (options.record_after_stage > 0.0 ? (script.first_stage_clock >= 0.0 && clock >= script.first_stage_clock + options.record_after_stage) : clock >= options.record_seconds)
                running = false;
        }
        if (rec.record_pipe && sl::download_bitmap(sl::screen))
        {
            std::fwrite(sl::screen->pixels.data(), 1, sl::screen->pixels.size(), rec.record_pipe);
            ++rec.record_frames;
            flush_record_audio();
        }
        sl::show_video_bitmap();
        sl::end_frame();
    }

    // Tidy-up after the loop.
    void App::shutdown()
    {

        stop_recording();
        shutdown_link();
        netplay.leave();
        netplay.lobby_disconnect();
        // The launcher's "Resume" starts this scenario: the ships where they were left, with the one you were flying active.
        if (!kosmos::Sim::user_dir().empty() && sim.ship_count() > 0 && options.selftest <= 0.0 && options.shot.empty())
        {
            kosmos::Scenario last = kosmos::capture_scenario(sim, "Last session");
            last.epoch_jd = sim.universe.julian_date();
            std::string problem;
            if (!kosmos::save_scenario((std::filesystem::path(kosmos::Sim::user_dir()) / "scenarios" / "last_session.json").string(), last, problem))
                std::fprintf(stderr, "kosmos: %s\n", problem.c_str());
        }
        // A recorded run may force sound on (--set), which must not overwrite the player's own settings.
        if (options.record_seconds <= 0.0)
            settings_save(sim.props);
        sl::gui_shutdown();
        lightning.release();
        sound.stop();
        vis.clear_parts();
        leave_system();
        if (vis.local_cloud)
            sl::destroy_mesh(vis.local_cloud);
        if (vis.rain_mesh)
            sl::destroy_mesh(vis.rain_mesh);
        for (sl::Mesh *&m : vis.bolt_meshes)
            if (m)
                sl::destroy_mesh(m);
        for (sl::Bitmap *&b : vis.bolt_bitmaps)
            if (b)
                sl::destroy_bitmap(b);
        for (sl::Mesh *m : vis.limb_pool)
            sl::destroy_mesh(m);
        for (sl::Mesh *m : {vis.sphere, vis.glow, vis.quad, vis.starfield, vis.tail})
            sl::destroy_mesh(m);
        ship_weapons.release_graphics();
        sl::destroy_bitmap(vis.lights_texture);
        sl::destroy_bitmap(vis.glow_texture);
        sl::destroy_bitmap(vis.plasma_texture);
        for (sl::Bitmap *b : {vis.star_surface, vis.star_filaments, vis.star_corona, vis.star_corona_far, vis.star_rays, vis.star_limb, vis.sky})
            sl::destroy_bitmap(b);
        sl::destroy_bitmap(vis.puff_texture);
        sl::destroy_bitmap(vis.cloud_texture);
        sl::destroy_bitmap(vis.nebula_atlas);
        sl::destroy_surface_3d(surface);
        sl::gamepad_shutdown();
        sl::shutdown();
    }
    int App::run(int argc, char **argv)
    {
        if (const int code = init_sim(argc, argv); code != keep_going)
            return code;
        if (const int code = init_start_date(); code != keep_going)
            return code;
        if (const int code = init_window(); code != keep_going)
            return code;
        if (const int code = init_render(); code != keep_going)
            return code;
        if (const int code = init_systems(); code != keep_going)
            return code;
        if (const int code = init_ship(); code != keep_going)
            return code;
        if (const int code = init_ui(); code != keep_going)
            return code;
        if (const int code = init_helpers(); code != keep_going)
            return code;
        if (const int code = init_recording(); code != keep_going)
            return code;
        if (const int code = init_loop(); code != keep_going)
            return code;
        Watchdog::start(); // WATCHDOG
        while (running)
            if (!run_frame())
                break;
        shutdown();
        return 0;
    }

    bool App::run_frame()
    {
        Watchdog::stage("begin_frame"); // WATCHDOG
        if (!begin_frame())
            return false;
        Watchdog::stage("apply_settings"); // WATCHDOG
        apply_settings();
        Watchdog::stage("poll_events"); // WATCHDOG
        poll_events();
        Watchdog::stage("follow_window"); // WATCHDOG
        follow_window();
        Watchdog::stage("keyboard"); // WATCHDOG
        keyboard();
        Watchdog::stage("update_link"); // WATCHDOG
        update_link();
        Watchdog::stage("advance_time"); // WATCHDOG
        advance_time();
        Watchdog::stage("place_camera"); // WATCHDOG
        place_camera();
        Watchdog::stage("prepare_scene"); // WATCHDOG
        prepare_scene();
        Watchdog::stage("draw_world"); // WATCHDOG
        draw_world();
        Watchdog::stage("draw_overlays"); // WATCHDOG
        draw_overlays();
        Watchdog::stage("draw_link_3d"); // WATCHDOG
        draw_link_3d();
        Watchdog::stage("update_sound"); // WATCHDOG
        update_sound();
        Watchdog::stage("draw_hud"); // WATCHDOG
        draw_hud();
        Watchdog::stage("draw_gui"); // WATCHDOG
        draw_gui();
        Watchdog::stage("present"); // WATCHDOG
        present();
        Watchdog::frame_done(universe_ref->time(), props_ref->get_int("/sim/warp-index")); // WATCHDOG
        return true;
    }

    bool App::dial_portal(int star)
    {
        kosmos::Ship *portal = portal_ship(sim);
        if (!portal || !ship)
            quantum.status = "No portal in this system.";
        else if (star < 0 || star == sim.system_index())
            quantum.status = "Choose another star.";
        else if (sim.galaxy.distance_ly(sim.system_index(), star) > 50.0)
            quantum.status = "That star is out of the portal's 50 light year range.";
        else if (glm::length(portal->body().position - ship->body().position) > 100000.0)
            quantum.status = "Too far from the portal: dial from within 100 km.";
        else
        {
            quantum.dialled = star;
            quantum.portal_age = 0.0;
            quantum.portal_open = 0.0f;
            quantum.prev_valid = false;
            quantum.status = "Dialling " + sim.galaxy.star(star).name + ".";
            return true;
        }
        return false;
    }

    // Opens the portal a few seconds after dialling, and jumps when the flown ship passes through the open disc.
    void App::update_portal(double dt)
    {
        kosmos::Ship *portal = portal_ship(sim);
        if (!portal || !ship || quantum.dialled < 0)
        {
            quantum.prev_valid = false;
            if (!portal)
                quantum.portal_open = 0.0f;
            return;
        }
        quantum.portal_age += dt;
        const double charge = 3.0, opening = 6.0, stays = 600.0;
        if (quantum.portal_age > charge + opening + stays)
        {
            quantum.dialled = -1;
            quantum.portal_open = 0.0f;
            quantum.status = "The portal closed.";
            return;
        }
        quantum.portal_open = static_cast<float>(std::clamp((quantum.portal_age - charge) / opening, 0.0, 1.0));
        const sl::FlightBody &pb = portal->body();
        const sl::dvec3 local = glm::inverse(pb.orientation) * (ship->body().position - pb.position);
        if (quantum.portal_open >= 1.0f && quantum.prev_valid && quantum.prev_ship == ship && ship != portal && quantum.prev_local.z * local.z <= 0.0 && quantum.prev_local.z != local.z)
        {
            const double k = quantum.prev_local.z / (quantum.prev_local.z - local.z);
            const double x = quantum.prev_local.x + (local.x - quantum.prev_local.x) * k, y = quantum.prev_local.y + (local.y - quantum.prev_local.y) * k;
            if (std::hypot(x, y) < portal->spec().portal_radius)
            {
                std::fprintf(stderr, "kosmos: through the portal to %s\n", sim.galaxy.star(quantum.dialled).name.c_str());
                quantum.jump_to = quantum.dialled;
                quantum.dialled = -1;
                return;
            }
        }
        quantum.prev_local = local;
        quantum.prev_ship = ship;
        quantum.prev_valid = true;
    }
} // namespace kosmos::app
