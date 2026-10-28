#include "app_state.h"

namespace kosmos::app
{
    // Time warp, the simulation step and the ship events that follow.
    void App::advance_time()
    {
    kosmos::Universe &universe = *universe_ref;
    sl::PropertyTree &props = *props_ref;
        // The shot option can skip ahead in time on rails so a screenshot of an orbit needs no waiting.
        if (options.shot_time > 0.0 && universe.time() < options.shot_time && ship->can_rails(Controls{}))
            props.set_int("/sim/warp-index", Sim::warp_levels - 1);

        // Time warp is held at 1x while others share the sky.
        netplay.update(frame);
        netplay.lobby_update(net_ui.advertise, net_ui.title);
        if (netplay.shared())
            props.set_int("/sim/warp-index", 0);
        // Standing still on the ground you can warp time; moving or floating holds it at 1x.
        else if (eva.active && (eva_moving || !eva.grounded || eva.speed > 0.3) && props.get_int("/sim/warp-index") > 0)
            props.set_int("/sim/warp-index", 0);

        if (edit_paused && !blender_edit.active())
        {
            edit_paused = false;
            props.set_bool("/sim/paused", edit_was_paused);
            props.set_bool("/sim/blender-edit", false);
            part_status = "Blender closed; simulation resumed";
        }
        model_poll += frame;
        if (model_poll > 0.5)
        {
            model_poll = 0.0;
            if (vis.reload_models() > 0 && edit_paused)
                part_status = "Reloaded the edited mesh";
        }
        double step_dt = frame;
        if (link_server && link_server->running() && !props.get_bool("/sim/paused"))
        {
            // Lockstep: a program that asked for it computes each step before the simulation takes it (at 1x).
            if (link_server->lockstep_active() && props.get_int("/sim/warp-index") > 0)
                props.set_int("/sim/warp-index", 0);
            const double fixed = link_server->lockstep_gate(frame, universe.time());
            if (fixed > 0.0)
                step_dt = fixed;
        }
        sim.step(step_dt);
        {
            const bool storms = sim.props.get_bool("/sim/settings/weather", true) && sim.props.get_bool("/sim/settings/weather-rain", true);
            const double thunder = storms && sim.current_warp() <= 4.0 ? sim.props.get_double("/environment/thunder") : 0.0;
            lightning.update(frame, thunder, sim.props.get_bool("/sim/settings/sound") ? 0.9 : 0.0);
        }
        lua_console.tick(frame);
        ship = sim.active_ship();
        if (eva.active)
        {
            kosmos::EvaInput in;
            double yaw = eva_look_yaw, pitch = eva_look_pitch;
            eva_look_yaw = eva_look_pitch = 0.0;
            if (!typing)
            {
                auto held = [this](const char *id) { return keys.down(id) ? 1.0 : 0.0; };
                in.forward = held("walk_forward") - held("walk_back");
                in.right = held("walk_right") - held("walk_left");
                const bool ctrl = keys.down("crouch");
                const bool space = keys.down("jump");
                in.jump = space;
                in.crouch = ctrl;
                in.up = (space ? 1.0 : 0.0) - (ctrl ? 1.0 : 0.0);
                in.run = keys.down("run");
                yaw += (held("look_left") - held("look_right")) * 1.8 * frame;
                pitch += (held("look_up") - held("look_down")) * 1.2 * frame;
                in.roll = (held("foot_roll_left") - held("foot_roll_right")) * 1.2 * frame;
            }
            if (options.eva_walk)
            {
                in.forward = 1.0;
                in.run = true;
                in.jump = std::fmod(clock, 3.0) < 0.15;
                static double next_report = 0.0;
                if (clock > next_report)
                {
                    next_report = clock + 1.0;
                    std::fprintf(stderr, "[eva] t=%.1f height=%.2f speed=%.2f grounded=%d upright=%d\n", clock, eva.height, eva.speed, eva.grounded, eva.upright);
                }
            }
            in.yaw = yaw;
            in.pitch = pitch;
            const kosmos::Astronaut::Solids solids = [&](const sl::dvec3 &from, const sl::dvec3 &to, double height)
            {
                kosmos::SolidScene scene;
                scene.universe = &sim.universe;
                scene.body = eva.body;
                scene.bases = &sim.bases;
                for (int i = 0; i < sim.ship_count(); ++i)
                    scene.ships.push_back(sim.ship(i));
                for (const std::unique_ptr<BodyTerrain> &bt : body_terrains)
                    if (bt->body == eva.body)
                        scene.clutter = bt->clutter.get();
                return kosmos::push_out_of_solids(scene, from, to, height);
            };
            eva.step(sim.universe, terrain_of(eva.body), in, frame, solids);
            const double warp_now = sim.current_warp();
            if (warp_now > 1.0)
                eva.carry(sim.universe, terrain_of(eva.body), frame * (warp_now - 1.0));
            double ship_distance = 0.0;
            const int near_ship = nearest_ship(ship_distance);
            bool at_base = false;
            const kosmos::Body &eva_body = sim.universe.bodies[static_cast<std::size_t>(eva.body)];
            for (const kosmos::Base &b : sim.bases)
                if (b.body == eva_body.name && eva.grounded)
                    at_base = at_base || std::acos(std::clamp(glm::dot(kosmos::base_direction(sim.universe, eva.body, b), eva.local_up()), -1.0, 1.0)) * eva_body.radius < 150.0;
            if (near_ship >= 0 && ship_distance < 30.0 || at_base)
                eva.recharge(frame * std::max(1.0, warp_now));
            {
                static bool trigger_was_down = false;
                const bool trigger = !typing && mouse_captured && sl::mouse_button_down(sl::MouseButton::left);
                arsenal.use_held(arsenal_ctx, trigger && !trigger_was_down, trigger);
                trigger_was_down = trigger;
            }
            if (eva.suit.health <= 0.0)
            {
                sim.log("You were killed: revived aboard your ship");
                board_ship(true);
                eva.suit.health = 1.0;
                ship = sim.active_ship();
            }
            if (eva.suit.failed)
            {
                sim.log("Suit failure: the crew pulled you back aboard");
                board_ship(true);
                ship = sim.active_ship();
            }
        }
        if (options.selftest_drop >= 0.0 && clock >= options.selftest_drop && ship)
        {
            options.selftest_drop = -1.0;
            arsenal.release(arsenal_ctx, *ship, options.selftest_drop_atom ? static_cast<int>(ship->spec().hardpoints.size()) - 1 : -1);
        }
        arsenal.set_sound(sound_on);
        arsenal.update(arsenal_ctx, frame * std::min(sim.current_warp(), 4.0), frame);
        if (ship != mfd_ship || ship->spec().name != mfd_ship_id)
        {
            // Each ship brings its own default instruments whenever it becomes the flown one.
            mfd_ship = ship;
            mfd_ship_id = ship->spec().name;
            panel_index = first_ship_seen ? options.panel : 0;
            first_ship_seen = false;
            const auto &cockpit_spec = ship->spec().cockpit;
            sim.mfd.set_domain(ship->spec().mfd_domain());
            if (panel_index > 0 && panel_index < static_cast<int>(cockpit_spec.panels_2d().size()))
                menu_actions.show_panel(panel_index);
            else
            {
                panel_index = 0;
                sim.mfd.set_unit_count(static_cast<int>(cockpit_spec.instruments.size()));
                for (std::size_t i = 0; i < cockpit_spec.instruments.size(); ++i)
                    sim.mfd.set_mode(static_cast<int>(i), std::max(0, sim.mfd.mode_by_id(cockpit_spec.instruments[i].mode)));
                // A default the ship's regime hides falls back to the surface display.
                const auto usable = [&](const std::string &id) { return sim.mfd.allowed(id) ? id : std::string("surface"); };
                if (cockpit_spec.instruments.empty() || !options.mfd_left.empty())
                    sim.mfd.set_mode(0, std::max(0, sim.mfd.mode_by_id(usable(options.mfd_left.empty() ? cockpit_spec.mfd_left : options.mfd_left))));
                if (cockpit_spec.instruments.empty() || !options.mfd_right.empty())
                    sim.mfd.set_mode(1, std::max(0, sim.mfd.mode_by_id(usable(options.mfd_right.empty() ? cockpit_spec.mfd_right : options.mfd_right))));
                for (int side = 0; side < 2 && side < sim.mfd.unit_count(); ++side)
                {
                    const std::string key = side == 0 ? "/sim/scenario-mfd-left" : "/sim/scenario-mfd-right";
                    const std::string wanted = props.get_string(key);
                    if (!wanted.empty() && sim.mfd.allowed(wanted))
                        sim.mfd.set_mode(side, std::max(0, sim.mfd.mode_by_id(wanted)));
                }
            }
        }
        const kosmos::Telemetry &tm = ship->telemetry(); tm_ref = &tm;
        if (netplay.joined())
        {
            kosmos::GhostState mine;
            const sl::FlightBody &nb = ship->body();
            mine.ship = ship->spec().name;
            mine.soi = tm.soi;
            mine.stage = static_cast<std::uint8_t>(std::clamp(ship->active_stage(), 0, 255));
            mine.throttle = static_cast<float>(props.get_double("/controls/throttle"));
            mine.engine = tm.thrust > 0.0;
            mine.position = nb.position - universe.position(tm.soi);
            mine.velocity = nb.velocity - universe.world.body(tm.soi)->velocity;
            mine.orientation = nb.orientation;
            netplay.publish(mine);
        }
        sl::FlightBody &fb = ship->body(); fb_ref = &fb;

        if (interior_cam >= static_cast<int>(ship->spec().cameras.size()))
            interior_cam = -1;
        if (base_view >= static_cast<int>(sim.bases.size()) || (base_view >= 0 && base_view_cam >= static_cast<int>(sim.bases[static_cast<std::size_t>(base_view)].cameras.size())))
            base_view = -1;
        if (user_cam >= static_cast<int>(user_cams.size()))
            user_cam = -1;
        in_eva = eva.active && !map_mode && !galaxy_mode;
        in_user = user_cam >= 0 && !map_mode && !galaxy_mode && !in_eva;
        in_base_cam = base_view >= 0 && !map_mode && !galaxy_mode && !in_user && !in_eva;
        in_interior = interior_cam >= 0 && !map_mode && !in_base_cam && !in_user && !in_eva;
        in_cockpit = cockpit_view && ship->spec().cockpit.present && !map_mode && !in_interior && !in_base_cam && !in_user && !in_eva;
        in_cockpit3d = in_cockpit && cockpit3d && ship->spec().cockpit.mfds;
        const int mx = sl::mouse_x(), my = sl::mouse_y();
        const int flip_x = sim.props.get_bool("/sim/settings/mouse-invert-x") ? -1 : 1, flip_y = sim.props.get_bool("/sim/settings/mouse-invert-y") ? -1 : 1;
        const int dx = (mx - last_mouse_x) * flip_x, dy = (my - last_mouse_y) * flip_y;
        const bool dragging = sl::mouse_button_down(sl::MouseButton::right) && !(sl::gui_available() && ImGui::GetIO().WantCaptureMouse);
        last_mouse_x = mx;
        last_mouse_y = my;
        {
            // On foot the mouse steers the view like a first-person game; the ` key, Alt, or any open window gives the pointer back.
            const bool windows_open = camera_window || settings_window || property_window || lua_window || net_window || systems_window || camera_editor_window || part_window || joystick_window || quantum_window;
            const bool free_pointer = windows_open || pointer_free || sl::key_down(sl::Key::left_alt) || sl::key_down(sl::Key::right_alt) || (sl::gui_available() && ImGui::GetIO().WantTextInput);
            const bool capture = in_eva && options.shot.empty() && !free_pointer;
            if (capture != mouse_captured)
            {
                sl::set_relative_mouse_mode(capture);
                mouse_captured = capture;
            }
        }
        const double zoom_in = keys.down("zoom_in") ? 1.0 / (1.0 + 2.0 * frame) : 1.0;
        const double zoom_out = keys.down("zoom_out") ? 1.0 + 2.0 * frame : 1.0;
        if (galaxy_mode)
            gview.ly_per_pixel = std::clamp(gview.ly_per_pixel * zoom_in * zoom_out, 0.02, 400.0);
        else if (map_mode)
        {
            if (dragging)
            {
                map.yaw -= static_cast<float>(dx) * 0.008f;
                map.pitch = std::clamp(map.pitch + static_cast<float>(dy) * 0.008f, 0.0f, 1.5708f);
            }
            map.metres_per_pixel = std::clamp(map.metres_per_pixel * zoom_in * zoom_out, 1.0, 1.0e12);
        }
        else if (in_base_cam)
        {
        }
        else if (in_eva)
        {
            if (mouse_captured)
            {
                int rx = 0, ry = 0;
                sl::mouse_relative_state(&rx, &ry);
                eva_look_yaw -= rx * flip_x * 0.003;
                eva_look_pitch -= ry * flip_y * 0.003;
            }
            else if (dragging)
            {
                eva_look_yaw -= dx * 0.004;
                eva_look_pitch -= dy * 0.004;
            }
        }
        else if (in_user)
        {
            kosmos::UserCamera &uc = user_cams[static_cast<std::size_t>(user_cam)];
            if (uc.orbit)
            {
                uc.yaw += uc.spin * frame;
                if (dragging)
                {
                    uc.yaw -= dx * 0.4;
                    uc.pitch = std::clamp(uc.pitch + dy * 0.4, -89.0, 89.0);
                }
                if (zoom_in * zoom_out != 1.0)
                {
                    double d = uc.distance;
                    if (d <= 0.0)
                    {
                        const int idx = uc.attach == kosmos::UserCamera::body ? universe.find(uc.attach_name) : -1;
                        d = idx >= 0 ? universe.bodies[static_cast<std::size_t>(idx)].radius * 3.0 : ship->spec().camera_distance;
                    }
                    uc.distance = std::clamp(d * zoom_in * zoom_out, 2.0, 3.0e11);
                }
            }
            else if (dragging)
            {
                look_yaw -= static_cast<float>(dx) * 0.008f;
                look_pitch = std::clamp(look_pitch - static_cast<float>(dy) * 0.008f, -1.4f, 1.4f);
            }
        }
        else if (in_cockpit || in_interior)
        {
            if (dragging)
            {
                look_yaw -= static_cast<float>(dx) * 0.008f;
                look_pitch = std::clamp(look_pitch - static_cast<float>(dy) * 0.008f, -1.4f, 1.4f);
            }
        }
        else
        {
            if (dragging)
            {
                // Dragging swings the chase camera around the ship, which stays centred.
                cam_yaw -= static_cast<float>(dx) * 0.006f;
                cam_pitch = std::clamp(cam_pitch + static_cast<float>(dy) * 0.006f, -1.5f, 1.5f);
            }
            cam_distance = std::clamp(cam_distance * zoom_in * zoom_out, 8.0, 3.0e9);
        }
    }

    // Where the camera is.
    void App::place_camera()
    {
    kosmos::Universe &universe = *universe_ref;
    const kosmos::Telemetry &tm = *tm_ref;
    sl::FlightBody &fb = *fb_ref;
        // The camera orbits the ship in its local horizon frame, so the horizon stays level as the rocket pitches over.
        up = sl::dvec3();
        east = sl::dvec3();
        north = sl::dvec3();
        ship->local_frame(up, east, north);
        {
            // True east reverses over a pole, which would swing the camera to the far side. Carry the previous east along the surface
            // instead, and ease back to the compass east only where it is well defined.
            static sl::dvec3 carried_east(0.0);
            static int carried_soi = -1;
            if (carried_soi != tm.soi || glm::length(carried_east) < 0.5)
                carried_east = east;
            carried_soi = tm.soi;
            sl::dvec3 e = carried_east - up * glm::dot(carried_east, up);
            e = glm::length(e) > 1e-6 ? glm::normalize(e) : east;
            const double polar = std::fabs(tm.latitude);
            const double w = std::clamp((70.0 - polar) / 20.0, 0.0, 1.0) * std::min(1.0, 3.0 * frame);
            const sl::dvec3 blended = e * (1.0 - w) + east * w;
            carried_east = glm::length(blended) > 1e-6 ? glm::normalize(blended) : e;
            east = carried_east;
            north = glm::cross(up, east);
        }
        const double yaw_d = cam_yaw, pitch_d = cam_pitch;
        sl::dvec3 offset_dir = (east * std::sin(yaw_d) + north * std::cos(yaw_d)) * std::cos(pitch_d) + up * std::sin(pitch_d);
        if (options.cam_sunward)
            offset_dir = glm::normalize(universe.position(0) - fb.position);
        if (options.cam_antisun)
            offset_dir = glm::normalize(glm::normalize(fb.position - universe.position(0)) + east * std::sin(yaw_d) * 0.9 + up * std::sin(pitch_d) * 0.9);
        // A tall ship can name the point the external camera frames, such as the capsule at the top of a Saturn V.
        const sl::dvec3 focus = ship->spec().has_camera_target ? ship->origin() + fb.direction_to_world(ship->spec().camera_target) : fb.position;
        eye_world = focus + offset_dir * cam_distance;
        ship_rel = glm::vec3(focus - eye_world);
        if (options.cam_antisun)
            ship_rel = glm::vec3(glm::normalize(universe.position(0) - eye_world) * 1000.0);
        if (!in_cockpit && !in_interior && !in_base_cam && !map_mode && !galaxy_mode && (free_yaw != 0.0f || free_pitch != 0.0f))
        {
            const sl::dvec3 fwd = glm::normalize(sl::dvec3(ship_rel));
            const sl::dvec3 right = glm::normalize(glm::cross(fwd, up));
            const sl::dvec3 turned = glm::angleAxis(static_cast<double>(free_yaw), up) * (glm::angleAxis(static_cast<double>(free_pitch), right) * fwd);
            ship_rel = glm::vec3(turned * glm::length(sl::dvec3(ship_rel)));
        }
        view_fov = options.cam_fov;
        // The radius of a body an orbiting camera is on, so the near plane frames it rather than the flown ship.
        user_extent = -1.0;
        if (in_cockpit || in_interior)
        {
            // Seen from the pilot's seat: the eye sits in the ship frame and the view looks along -Z, free to turn with the mouse.
            sl::dvec3 seat = ship->spec().cockpit.eye + (in_interior ? sl::dvec3(0.0) : eye_shift);
            double yaw = look_yaw, pitch = look_pitch;
            if (!in_interior)
            {
                yaw += ship->spec().cockpit.yaw * pi / 180.0;
                pitch += ship->spec().cockpit.pitch * pi / 180.0;
                if (ship->spec().cockpit.fov > 0.0)
                    view_fov = ship->spec().cockpit.fov;
            }
            if (in_interior)
            {
                const kosmos::CameraSpec &cs = ship->spec().cameras[static_cast<std::size_t>(interior_cam)];
                seat = cs.position;
                yaw += cs.yaw * pi / 180.0;
                pitch += cs.pitch * pi / 180.0;
                view_fov = cs.fov;
            }
            eye_world = ship->origin() + fb.direction_to_world(seat);
            const double cl = std::cos(pitch);
            sl::dvec3 look(-std::sin(yaw) * cl, std::sin(pitch), -std::cos(yaw) * cl);
            sl::dvec3 view_up(0.0, 1.0, 0.0);
            if (ship->spec().building() && !ship->spec().horizontal && !in_interior)
            {
                // An upright building's roof is -Z; looking along +X (out of the door) is yaw 0.
                look = sl::dvec3(std::cos(yaw) * cl, -std::sin(yaw) * cl, -std::sin(pitch));
                view_up = sl::dvec3(0.0, 0.0, -1.0);
            }
            ship_rel = glm::vec3(fb.direction_to_world(look) * 1000.0);
            up = fb.direction_to_world(view_up);
        }
        else if (in_eva)
        {
            eye_world = eva.eye_world(universe);
            ship_rel = glm::vec3(eva.forward() * 1000.0);
            up = eva.view_up();
            view_fov = std::max(options.cam_fov, 60.0);
        }
        else if (in_user)
        {
            const kosmos::UserCamera &uc = user_cams[static_cast<std::size_t>(user_cam)];
            // Where an attachment is, and the axes its camera offsets are in (a ship's own; for a body, east, up and south).
            struct Anchor
            {
                sl::dvec3 position;
                sl::dvec3 x, y, z;
                double radius = 0.0;
                bool ship = false;
            };
            auto ship_anchor = [](Ship &sh)
            {
                sl::FlightBody &b = sh.body();
                return Anchor{sh.origin(), b.direction_to_world(sl::dvec3(1, 0, 0)), b.direction_to_world(sl::dvec3(0, 1, 0)), b.direction_to_world(sl::dvec3(0, 0, 1)), 0.0, true};
            };
            auto anchor = [&](int kind, const std::string &name)
            {
                if (kind == kosmos::UserCamera::other_ship)
                    for (int si = 0; si < sim.ship_count(); ++si)
                        if (sim.ship(si)->name == name)
                            return ship_anchor(*sim.ship(si));
                if (kind == kosmos::UserCamera::body)
                {
                    const int bi = universe.find(name);
                    if (bi >= 0)
                        return Anchor{universe.position(bi), east, up, -north, universe.bodies[static_cast<std::size_t>(bi)].radius, false};
                }
                return ship_anchor(*ship);
            };
            const Anchor host = anchor(uc.attach, uc.attach_name);
            // The point of a ship a camera frames: its `camera_target` when it names one, else its origin.
            auto focus_of = [&](const int kind, const std::string &name, const Anchor &a)
            {
                Ship *sh = ship;
                if (kind == kosmos::UserCamera::other_ship)
                    for (int si = 0; si < sim.ship_count(); ++si)
                        if (sim.ship(si)->name == name)
                            sh = sim.ship(si);
                if (!a.ship || !sh || !sh->spec().has_camera_target)
                    return a.position;
                return a.position + sh->body().direction_to_world(sh->spec().camera_target);
            };
            view_fov = uc.fov;
            if (uc.orbit)
            {
                const double distance = uc.distance > 0.0 ? uc.distance : host.ship ? ship->spec().camera_distance : host.radius * 3.0;
                const double yaw = uc.yaw * pi / 180.0, pitch = uc.pitch * pi / 180.0;
                const sl::dvec3 dir = (east * std::sin(yaw) + north * std::cos(yaw)) * std::cos(pitch) + up * std::sin(pitch);
                eye_world = focus_of(uc.attach, uc.attach_name, host) + dir * distance;
                ship_rel = glm::vec3(-dir * distance);
                if (!host.ship)
                    user_extent = host.radius;
            }
            else
            {
                eye_world = host.position + host.x * uc.x + host.y * uc.y + host.z * uc.z;
                if (uc.look_at != kosmos::UserCamera::look_ahead)
                {
                    const int kind = uc.look_at == kosmos::UserCamera::look_flown_ship ? 0 : uc.look_at == kosmos::UserCamera::look_ship ? 1 : 2;
                    const sl::dvec3 to = anchor(kind, uc.look_name).position - eye_world;
                    ship_rel = glm::vec3(glm::length(to) > 1e-3 ? to : -host.z);
                    up = host.y;
                }
                else
                {
                    const double yaw = uc.look_yaw * pi / 180.0 + look_yaw, pitch = uc.look_pitch * pi / 180.0 + look_pitch;
                    const double cl = std::cos(pitch);
                    ship_rel = glm::vec3((host.x * -std::sin(yaw) * cl + host.y * std::sin(pitch) + host.z * -std::cos(yaw) * cl) * 1000.0);
                    up = host.y;
                }
            }
        }
        else if (in_base_cam)
        {
            const kosmos::Base &bs = sim.bases[static_cast<std::size_t>(base_view)];
            const kosmos::BaseCamera &bc = bs.cameras[static_cast<std::size_t>(base_view_cam)];
            sl::dvec3 look;
            kosmos::base_camera_pose(universe, bs, bc, &fb.position, eye_world, look, up);
            ship_rel = glm::vec3(look * 1000.0);
            view_fov = bc.fov;
        }

        soi = tm.soi;
        const kosmos::Body &soi_body = universe.bodies[static_cast<std::size_t>(soi)]; soi_body_ref = &soi_body;
        soi_pos = universe.position(soi);
        // The chase camera may not dip below the ground.
        if (!in_cockpit && !in_cockpit3d && !in_interior && !in_base_cam && !in_eva)
        {
            const sl::Terrain *soi_terrain = nullptr;
            for (const std::unique_ptr<BodyTerrain> &bt : body_terrains)
                if (bt->body == soi)
                    soi_terrain = bt->terrain.get();
            const sl::dvec3 from_centre = eye_world - soi_pos;
            const double dist = glm::length(from_centre);
            if (soi_terrain && dist > 1.0)
            {
                const sl::dvec3 dir = from_centre / dist;
                const double floor_r = soi_body.radius + soi_terrain->height_at_direction(dir) + 2.0;
                if (dist < floor_r)
                {
                    const sl::dvec3 shift = dir * (floor_r - dist);
                    eye_world += shift;
                    ship_rel -= glm::vec3(shift);
                }
            }
        }
        home = soi == earth_id;
        earth_pos = universe.position(earth_id);
        eye_from_earth = eye_world - earth_pos;
        eye_earth_distance = glm::length(eye_from_earth);

        view = sl::TerrainView();
        view.position = eye_from_earth;
        view.forward = glm::normalize(sl::dvec3(ship_rel));
        view.up = up;
        view.fov_y_degrees = view_fov;
        view.aspect = static_cast<double>(screen_w) / screen_h;
        view.viewport_height = screen_h;
        view.far_distance = eye_earth_distance + earth_body->radius * 1.3;
        near_earth = eye_earth_distance < earth_body->radius + 2.0e7;
    }

    // Which terrain and sphere are drawn.
    void App::prepare_scene()
    {
    kosmos::Universe &universe = *universe_ref;
    sl::PropertyTree &props = *props_ref;
    const kosmos::Body &soi_body = *soi_body_ref;
        // With a hi-res surface image, the terrain gives way to a plain textured sphere above 300 km (lower when the tiled imagery is present, which stays sharp down to there), scaled by the body's size.
        const bool earth_has_image = std::find(vis.surface_textured.begin(), vis.surface_textured.end(), earth_id) != vis.surface_textured.end();
        earth_sphere_switch_m = props.get_bool("/sim/settings/gfx-earth-relief") ? 300.0e3 : 20.0e3;
        earth_as_sphere = earth_has_image && eye_earth_distance - earth_body->radius > (vis.earth_tiles ? earth_sphere_switch_m : 300.0e3) * earth_body->radius / 6.371e6;
        for (const std::unique_ptr<BodyTerrain> &bt : body_terrains)
        {
            const sl::FlightBody &fb = *universe.world.body(bt->body);
            bt->terrain->set_rotation(fb.spin_phase + fb.spin_rate * universe.world.time(), fb.spin_axis);
            kosmos::Body &tb = universe.bodies[static_cast<std::size_t>(bt->body)];
            for (kosmos::Base &base : sim.bases)
                if (base.body == tb.name)
                {
                    base.direction = kosmos::base_direction(universe, bt->body, base);
                    base.pole = universe.pole(bt->body);
                }
        }
        if (near_earth)
        {
            if (first_frame)
                settle(*terrain, view);
            else
                terrain->update(view);
        }
        for (std::size_t k = 1; k < body_terrains.size(); ++k)
        {
            BodyTerrain &bt = *body_terrains[k];
            const kosmos::Body &b = universe.bodies[static_cast<std::size_t>(bt.body)];
            bt.eye_rel = eye_world - universe.position(bt.body);
            const double threshold = 300.0e3 * b.radius / 6.371e6;
            const bool has_image = std::find(vis.surface_textured.begin(), vis.surface_textured.end(), bt.body) != vis.surface_textured.end();
            bt.active = glm::length(bt.eye_rel) - b.radius < (has_image ? threshold : 10.0 * threshold);
            if (bt.active)
            {
                sl::TerrainView body_view = view;
                body_view.position = bt.eye_rel;
                body_view.far_distance = glm::length(bt.eye_rel) + b.radius * 1.3;
                if (first_frame)
                    settle(*bt.terrain, body_view);
                else
                    bt.terrain->update(body_view);
            }
        }
        {
            // Trees, houses, rocks and outposts exist only near the ground; build them lazily, outside begin_surface_3d().
            const bool clutter_on = props.get_bool("/sim/settings/gfx-clutter", true) && !options.no_clutter;
            const double clutter_density = std::clamp(props.get_int("/sim/settings/gfx-clutter-density", 100), 10, 200) / 100.0;
            for (std::size_t k = 0; k < body_terrains.size(); ++k)
            {
                BodyTerrain &bt = *body_terrains[k];
                const kosmos::Body &b = universe.bodies[static_cast<std::size_t>(bt.body)];
                const bool drawn = k == 0 ? near_earth && !earth_as_sphere : bt.active;
                const sl::dvec3 &eye_rel = k == 0 ? eye_from_earth : bt.eye_rel;
                bt.clutter_live = clutter_on && drawn && glm::length(eye_rel) - b.radius < 6000.0 && kosmos::has_clutter(b);
                if (bt.clutter_live)
                {
                    // Scatter only where it can matter: within 50 km of a base, or of a ship flying below 5 km.
                    const glm::dquat unturn = glm::inverse(glm::angleAxis(bt.terrain->rotation(), bt.terrain->rotation_axis()));
                    const sl::dvec3 eye_dir = glm::normalize(unturn * eye_rel);
                    constexpr double clutter_range = 50.0e3;
                    bool near = kosmos::distance_to_zones(bt.clutter_zones, eye_dir, b.radius) < clutter_range;
                    const kosmos::Telemetry &tel = ship->telemetry();
                    if (!near && tel.soi == bt.body && tel.altitude < 5000.0)
                    {
                        const sl::dvec3 ship_dir = glm::normalize(unturn * (ship->origin() - universe.position(bt.body)));
                        near = std::acos(std::clamp(glm::dot(eye_dir, ship_dir), -1.0, 1.0)) * b.radius < clutter_range;
                    }
                    bt.clutter_live = near;
                }
                if (!bt.clutter_live)
                    continue;
                if (bt.clutter && bt.clutter_density != clutter_density)
                    bt.clutter.reset();
                if (!bt.clutter)
                {
                    kosmos::ClutterWorld world;
                    world.body = &b;
                    world.frame = bt.frame;
                    world.zones = bt.clutter_zones;
                    world.land = bt.landclass;
                    world.density = clutter_density;
                    world.near_direction = glm::normalize(glm::inverse(glm::angleAxis(bt.terrain->rotation(), bt.terrain->rotation_axis())) * eye_rel);
                    world.root = sim.root();
                    bt.clutter = kosmos::make_clutter(*bt.terrain, world);
                    bt.clutter_density = clutter_density;
                }
                if (!bt.clutter)
                {
                    bt.clutter_live = false;
                    continue;
                }
                sl::TerrainView clutter_view = view;
                clutter_view.position = eye_rel;
                clutter_perf.update_ms += time_ms([&] { bt.clutter->update(clutter_view); });
            }
        }
        first_frame = false;
        if (options.perf)
        {
            ++clutter_perf.frames;
            const auto now = std::chrono::steady_clock::now();
            const double span = std::chrono::duration<double>(now - clutter_perf.last).count();
            if (span >= 3.0)
            {
                std::fprintf(stderr, "[perf] %.1f fps (%.2f ms/frame); clutter CPU: update %.3f ms, draw %.3f ms per frame\n", clutter_perf.frames / span,
                             1000.0 * span / clutter_perf.frames, clutter_perf.update_ms / clutter_perf.frames, clutter_perf.draw_ms / clutter_perf.frames);
                clutter_perf = ClutterPerf();
            }
        }
        loading.end();

        const sl::dvec3 sun_dir_world = glm::normalize(-eye_world);
        to_sun = glm::vec3(sun_dir_world);

        camera = sl::Camera();
        camera.position = glm::vec3(0.0f);
        camera.target = ship_rel;
        camera.up = glm::vec3(up);
        camera.field_of_view = static_cast<float>(view_fov);
        double extent = 4.0;
        for (const kosmos::StageSpec &st : ship->spec().stages)
            extent = std::max(extent, std::max(std::fabs(st.z - st.length * 0.5), std::fabs(st.z + st.length * 0.5)) + 6.0);
        eye_soi_altitude = glm::length(eye_world - soi_pos) - soi_body.radius;
        const double nearest_ground = std::max(1.0, 0.9 * (eye_soi_altitude - 10.0e3));
        view_distance = in_user && user_cams[static_cast<std::size_t>(user_cam)].orbit ? glm::length(sl::dvec3(ship_rel)) : cam_distance;
        in_user_mounted = in_user && !user_cams[static_cast<std::size_t>(user_cam)].orbit;
        ship_near = in_cockpit3d ? 0.25 : (in_cockpit || in_interior) ? 1.0 : (in_base_cam || in_user_mounted) ? 1.0 : std::max(1.0, std::min(view_distance - (user_extent >= 0.0 ? user_extent : extent), nearest_ground));
    }
} // namespace kosmos::app
