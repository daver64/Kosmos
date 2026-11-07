#include <functional>
#include "app_state.h"
#include "cockpit_panels.h"

namespace kosmos::app
{
    // The orbit line, HUD and labels over the scene.
    void App::draw_orbit_line()
    {
        kosmos::Universe &universe = *universe_ref;
        const kosmos::Body &soi_body = *soi_body_ref;
        sl::OrbitElements elements;
        const bool have_orbit = universe.world.orbit_of(ship->body_id(), soi, elements);
        if (have_orbit && show_orbit && !options.hide_info && !map_mode && view_distance > 2.0e3 && !ship->on_rails())
        {
            const std::vector<sl::dvec3> path = sl::orbit_path(soi_body.gm, elements, 360, std::min(soi_body.soi, 1.0e13));
            const float body_r = static_cast<float>(soi_body.radius);
            const glm::vec3 body_p = glm::vec3(soi_pos - eye_world);
            bool have_previous = false;
            float px = 0.0f, py = 0.0f;
            for (const sl::dvec3 &point : path)
            {
                const glm::vec3 p = glm::vec3(point + soi_pos - eye_world);
                const float ray_length = glm::length(p);
                const glm::vec3 dir = p / ray_length;
                const float along = glm::dot(body_p, dir);
                const bool hidden = along > 0.0f && along < ray_length && glm::length(body_p - dir * along) < body_r * 0.999f;
                float x = 0.0f, y = 0.0f;
                const bool visible = !hidden && project(view_proj, p, x, y) && std::fabs(x) < 1.0e4f && std::fabs(y) < 1.0e4f;
                if (visible && have_previous)
                    sl::line(sl::screen, px, py, x, y, {80, 255, 120});
                have_previous = visible;
                px = x;
                py = y;
            }
        }
    }

    void App::draw_head_up()
    {
        sl::FlightBody &fb = *fb_ref;
        if (hud.mode() != sl::HudMode::off && hud_allowed(hud.mode(), ship->spec().hud_modes()) && (in_cockpit || in_interior) && !map_mode && !galaxy_mode && !(in_cockpit3d && !ship->spec().cockpit.vc_model.empty()))
        {
            sl::HudState hs;
            const glm::dvec3 fwd = glm::normalize(sl::dvec3(ship_rel));
            hs.camera.forward = fwd;
            hs.camera.right = glm::normalize(glm::cross(fwd, sl::dvec3(up)));
            hs.camera.up = glm::cross(hs.camera.right, fwd);
            hs.camera.focal = screen_h * 0.5 / std::tan(glm::radians(view_fov * 0.5));
            hs.camera.width = screen_w;
            hs.camera.height = screen_h;
            hs.nose = fb.direction_to_world(sl::dvec3(0.0, 0.0, -1.0));
            hs.roof = fb.direction_to_world(sl::dvec3(0.0, 1.0, 0.0));
            fill_hud_state(hs);
            hud.draw(hs);
        }
    }

    // The HUD of a virtual cockpit is drawn onto a texture for the HUD glass, looking along the nose.
    void App::render_vc_hud_target()
    {
        if (!targets.hud_target)
            return;
        const bool active = hud.mode() != sl::HudMode::off && hud_allowed(hud.mode(), ship->spec().hud_modes());
        sl::FlightBody &fb = *fb_ref;
        sl::HudState hs;
        hs.nose = fb.direction_to_world(sl::dvec3(0.0, 0.0, -1.0));
        hs.roof = fb.direction_to_world(sl::dvec3(0.0, 1.0, 0.0));
        hs.camera.forward = hs.nose;
        hs.camera.up = hs.roof;
        hs.camera.right = glm::normalize(glm::cross(hs.nose, hs.roof));
        // The HUD plate is 0.15 m wide, 0.31 m from the pilot's eye.
        const int factor = screen_supersample();
        sl::Bitmap *big = supersample_target(factor);
        const int size = big ? mfd_target_size * factor : mfd_target_size;
        hs.camera.focal = size * 0.5 / (0.075 / 0.308);
        hs.camera.width = size;
        hs.camera.height = size;
        if (active)
            fill_hud_state(hs);
        sl::Bitmap *into = big ? big : targets.hud_target;
        if (!sl::begin_render_target(into))
            return;
        sl::clear_render_target({0, 0, 0, 0});
        if (active)
            hud.draw(hs);
        sl::end_render_target();
        if (big)
            downsample_target(big, targets.hud_target, {0, 0, 0, 0});
    }

    void App::fill_hud_state(sl::HudState &hs)
    {
        kosmos::Universe &universe = *universe_ref;
        sl::PropertyTree &props = *props_ref;
        const kosmos::Telemetry &tm = *tm_ref;
        sl::FlightBody &fb = *fb_ref;
        const kosmos::Body &soi_body = *soi_body_ref;
        {
            const sl::FlightBody &soi_fb = *universe.world.body(soi);
            const sl::dvec3 from_soi = fb.position - soi_fb.position;
            hs.orbit.valid = true;
            hs.orbit.reference = soi_body.name;
            hs.orbit.velocity = fb.velocity - soi_fb.velocity;
            hs.orbit.position = from_soi;
            hs.surface.valid = true;
            hs.surface.reference = soi_body.name;
            hs.surface.altitude = tm.altitude;
            hs.surface.radar = tm.altitude < 1.0e4;
            hs.surface.speed = tm.surface_speed;
            hs.surface.airspeed = tm.dynamic_pressure > 0.5;
            hs.surface.ground_velocity = ship->relative_velocity();
            hs.surface.up = glm::normalize(from_soi);
            hs.surface.north = universe.pole(soi);
            if (props.get_bool("/dock/valid"))
            {
                const auto vec = [&props](const char *name) {
                    const std::string base = std::string("/dock/hud/") + name + "-";
                    return sl::dvec3(props.get_double(base + "x"), props.get_double(base + "y"), props.get_double(base + "z"));
                };
                hs.docking.valid = true;
                hs.docking.target = props.get_string("/dock/target");
                hs.docking.reference = hs.docking.target + " [" + props.get_string("/dock/own-port-name") + "]";
                hs.docking.velocity = vec("velocity");
                hs.docking.position = vec("position");
                hs.docking.has_port = true;
                hs.docking.port_position = hs.docking.position;
                hs.docking.port_direction = vec("direction");
                hs.docking.port_up = vec("up");
            }
        }
    }

    void App::draw_eva_overlay()
    {
        kosmos::Universe &universe = *universe_ref;
        {
            double ship_distance = 0.0;
            const int near_ship = nearest_ship(ship_distance);
            const double ship_speed = near_ship >= 0 ? glm::length(sim.ship(near_ship)->body().velocity - universe.world.body(eva.body)->velocity - eva.velocity) : 0.0;
            // Mark each building's door so it can be found, and say when Enter will go in.
            const sl::dvec3 eva_eye = eva.eye_world(universe);
            for (int s = 0; s < sim.ship_count(); ++s)
            {
                if (!sim.ship(s)->spec().building())
                    continue;
                const sl::dvec3 rel = door_world(*sim.ship(s)) - eva_eye;
                const double distance = glm::length(rel);
                if (distance > 300.0)
                    continue;
                if (eva.building == s && walk.interior_panel >= 0)
                    sl::gprintf_center(screen_h / 2 + 70, {120, 255, 150}, "Press Enter to use the panel");
                else if (s == building_at_door() && eva.building == s && !sim.ship(s)->spec().interior->airlock_name().empty())
                {
                    if (walk.in_airlock)
                        sl::gprintf_center(screen_h / 2 + 90, {120, 255, 150}, walk.room_kpa > kosmos::airlock_safe_kpa ? "Press Enter to depressurise and go out" : "Press Enter to go outside");
                }
                else if (s == building_at_door())
                    sl::gprintf_center(screen_h / 2 + 90, {120, 255, 150}, eva.building == s ? "Press Enter to go outside" : "Press Enter to go into %s", sim.ship(s)->name.c_str());
                if (eva.building == s && walk.room_kpa >= 0.0)
                    sl::gprintf_center(screen_h / 2 + 130, {160, 210, 255}, "%s  %.0f kPa", walk.room_label.empty() ? "Room" : walk.room_label.c_str(), walk.room_kpa);
                float x = 0.0f, y = 0.0f;
                if (distance < 1.0 || !project(view_proj, glm::vec3(rel / distance * 1000.0), x, y) || x < 0 || x > screen_w || y < 0 || y > screen_h)
                    continue;
                sl::circle(sl::screen, x, y, 6.0f, {120, 255, 150}, 1.0f);
                sl::gprintf(static_cast<int>(x) + 10, static_cast<int>(y) - 6, {120, 255, 150}, "DOOR %s  %s", sim.ship(s)->name.c_str(), format_distance(distance).c_str());
            }
            kosmos::draw_suit_hud(eva, universe, screen_w, screen_h, near_ship >= 0 ? ship_distance : -1.0, ship_speed, near_ship >= 0 ? sim.ship(near_ship)->name.c_str() : nullptr);
        }
    }

    void App::draw_body_labels()
    {
        kosmos::Universe &universe = *universe_ref;
        sl::PropertyTree &props = *props_ref;
        const float focal = static_cast<float>(screen_h * 0.5 / std::tan(glm::radians(view_fov * 0.5)));
        const bool body_labels = props.get_bool("/sim/settings/labels");
        if (props.get_bool("/sim/settings/ship-labels") && !map_mode && !galaxy_mode)
            for (int s = 0; s < sim.ship_count(); ++s)
            {
                if (sim.ship(s) == ship)
                    continue;
                const sl::dvec3 rel = sim.ship(s)->body().position - eye_world;
                const double distance = glm::length(rel);
                float x = 0.0f, y = 0.0f;
                if (distance < 1.0 || !project(view_proj, glm::vec3(rel / distance * 1000.0), x, y) || x < 0 || x > screen_w || y < 0 || y > screen_h)
                    continue;
                sl::circle(sl::screen, x, y, 5.0f, {120, 255, 150}, 1.0f);
                sl::gprintf(static_cast<int>(x) + 9, static_cast<int>(y) - 6, {120, 255, 150}, "%s  %s", sim.ship(s)->name.c_str(), format_distance(distance).c_str());
            }
        if (!map_mode && !galaxy_mode)
            for (const kosmos::Ghost &g : netplay.ghosts())
            {
                if (!g.have_state || g.age > 5.0 || g.state.soi < 0 || g.state.soi >= static_cast<int>(universe.bodies.size()))
                    continue;
                const sl::dvec3 rel = universe.position(g.state.soi) + g.shown_position() - eye_world;
                const double distance = glm::length(rel);
                float x = 0.0f, y = 0.0f;
                if (distance < 1.0 || !project(view_proj, glm::vec3(rel / distance * 1000.0), x, y) || x < 0 || x > screen_w || y < 0 || y > screen_h)
                    continue;
                sl::circle(sl::screen, x, y, 6.0f, {255, 200, 90}, 1.0f);
                sl::gprintf(static_cast<int>(x) + 10, static_cast<int>(y) - 6, {255, 200, 90}, "%s  %s", g.nick.c_str(), format_distance(distance).c_str());
            }
        for (std::size_t i = 0; i < universe.bodies.size() && !map_mode && body_labels; ++i)
        {
            const kosmos::Body &b = universe.bodies[i];
            if (b.minor)
                continue;
            const sl::dvec3 rel = universe.position(static_cast<int>(i)) - eye_world;
            const double distance = glm::length(rel);
            const double pixel_radius = b.radius / distance * focal;
            if (pixel_radius > 5.0 || distance < 1.0)
                continue;
            float x = 0.0f, y = 0.0f;
            if (!project(view_proj, glm::vec3(rel / distance * 1000.0), x, y) || x < 0 || x > screen_w || y < 0 || y > screen_h)
                continue;
            sl::circle(sl::screen, x, y, 6.0f, {200, 220, 255}, 1.0f);
            sl::gprintf(static_cast<int>(x) + 10, static_cast<int>(y) - 6, {200, 220, 255}, "%s", b.name.c_str());
        }
    }

    void App::draw_overlays()
    {
        // Orbit line about the sphere-of-influence body, hidden where the body covers it.
        draw_orbit_line();

        // Head-up display.
        draw_head_up();

        if (in_eva && !options.clean)
            draw_eva_overlay();
        if (!options.clean)
            arsenal.draw_hud(sim, eva, screen_w, screen_h, eye_world);

        // Labels for bodies too small on screen to recognise.
        draw_body_labels();
    }

    // Engine and wind sound.
    void App::cabin_ambience_tick()
    {
        const bool in_room = in_eva && walk.room_kpa > 20.0;
        sound.data_root = sim.root();
        sound.cabin_ambience(in_cockpit || in_interior || in_room, sound_on);
    }

    void App::update_sound()
    {
    sl::PropertyTree &props = *props_ref;
    const kosmos::Telemetry &tm = *tm_ref;
    sl::FlightBody &fb = *fb_ref;
        // Sound.
        double rocket_level = 0.0;
        for (const sl::Thruster &t : fb.thrusters)
            if (t.max_thrust > 0.0 && t.level > 0.0 && t.tank >= 0 && fb.tanks[static_cast<std::size_t>(t.tank)].mass > 0.0)
                rocket_level = std::max(rocket_level, t.level);
        const double air = home ? 1.0 - fx::smoothstep(20.0e3, 90.0e3, tm.altitude) : 0.0;
        // Wind: a calm breeze near the ground, rising with airspeed, and drowned out by the engines.
        const double near_ground = home ? 1.0 - fx::smoothstep(200.0, 3000.0, tm.altitude) : 0.0;
        const double wind_level = air * std::max(0.45 * near_ground, std::min(1.0, tm.surface_speed / 200.0)) * (1.0 - std::min(1.0, rocket_level * 2.0));
        double rcs_level = 0.0;
        for (const sl::Thruster &t : fb.thrusters)
            if (t.max_thrust > 0.0 && t.level > 0.0 && t.tank >= 0 && t.max_thrust < 4000.0 && fb.tanks[static_cast<std::size_t>(t.tank)].mass > 0.0)
                rcs_level = std::max(rcs_level, t.level);
        sound.data_root = sim.root();
        sound.set_engine_file(ship->spec().engine_sound.empty() ? std::string() : ship->spec().directory + "/" + ship->spec().engine_sound);
        sound.set(sound_on ? rocket_level * (0.35 + 0.65 * air) : 0.0, sound_on ? rcs_level : 0.0, sound_on ? wind_level : 0.0);
        cabin_ambience_tick();
        {
            // Event cues: one-shots on the edge of a state change. The first frame only records the state.
            const bool landed_now = tm.landed, crashed_now = ship->crashed(), docked_now = props.get_bool("/dock/docked");
            const bool alarm_now = ship->systems() && !ship->systems()->alarms().empty();
            // Landing and docking state settle over the first moments after a start, so cues wait a couple of seconds.
            if (!memory.cues_seen)
                memory.cues_from = sl::time_ms() / 1000.0 + 2.0;
            if (memory.cues_seen && sound_on && sl::time_ms() / 1000.0 >= memory.cues_from)
            {
                if (crashed_now && !memory.was_crashed)
                    sound.play("crash", 255);
                else if (landed_now && !memory.was_landed && !crashed_now)
                    sound.play("touchdown", 220);
                if (docked_now != memory.was_docked)
                    sound.play(docked_now ? "docking" : "undocking", 200);
                if (ship->active_stage() != memory.was_stage)
                    sound.play("thump", 230);
                if (alarm_now && (!memory.was_alarm || sl::time_ms() / 1000.0 > memory.alarm_next))
                {
                    sound.play("warning_beep", 160);
                    memory.alarm_next = sl::time_ms() / 1000.0 + 4.0;
                }
            }
            memory.cues_seen = true;
            memory.was_landed = landed_now;
            memory.was_crashed = crashed_now;
            memory.was_docked = docked_now;
            memory.was_alarm = alarm_now;
            memory.was_stage = ship->active_stage();
        }
    }

    void App::draw_flight_readout(const std::string &soi_name)
    {
    kosmos::Universe &universe = *universe_ref;
    sl::PropertyTree &props = *props_ref;
    const kosmos::Telemetry &tm = *tm_ref;
    sl::FlightBody &fb = *fb_ref;
            sl::gprintf(16, 34, {200, 220, 255}, "%s  Alt %s   Speed %.1f m/s   Vertical %.1f m/s   %.0f fps (%.1f ms)", soi_name.c_str(), format_distance(tm.altitude).c_str(), tm.surface_speed, tm.vertical_speed,
                        props.get_double("/sim/frame-rate"), props.get_double("/sim/frame-time") * 1000.0);
            if (ship->flies_as_aircraft())
                sl::gprintf(16, 52, {120, 255, 150}, "Climb %.1f m/s   Flight path %.1f deg   AoA %.1f deg   Slip %.1f deg   Q %.0f Pa   Flaps %.0f%%%s%s", tm.vertical_speed, tm.flight_path,
                            tm.alpha, tm.sideslip, tm.dynamic_pressure, props.get_double("/controls/flaps") * 100.0, props.get_double("/controls/brake") > 0.5 ? "   BRAKES" : "",
                            !ship->spec().retractable_gear ? "" : ship->gear_position() >= 0.999 ? "   GEAR DOWN" : ship->gear_position() <= 0.001 ? "   GEAR UP" : "   GEAR MOVING");
            else if (tm.bound)
                sl::gprintf(16, 52, {120, 255, 150}, "Apoapsis %s   Periapsis %s   Period %.1f min   Ecc %.3f", format_distance(tm.apoapsis).c_str(),
                            format_distance(tm.periapsis).c_str(), tm.period / 60.0, tm.eccentricity);
            else
                sl::gprintf(16, 52, {255, 200, 120}, "Escape trajectory   Periapsis %s", format_distance(tm.periapsis).c_str());
            if (ship->spec().has_hover())
                sl::gprintf(16, 70, {255, 220, 150}, "Throttle %3.0f%%  Hover %3.0f%%   Thrust %.0f kN   TWR %.2f   Stage %d/%d fuel %.0f kg   Mass %.1f t",
                            props.get_double("/controls/throttle") * 100.0, props.get_double("/controls/hover") * 100.0, tm.thrust / 1000.0, tm.twr,
                            ship->active_stage() + 1, ship->stage_count(), tm.stage_fuel, tm.mass / 1000.0);
            else
                sl::gprintf(16, 70, {255, 220, 150}, "Throttle %3.0f%%   Thrust %.0f kN   TWR %.2f   Stage %d/%d fuel %.0f kg   Mass %.1f t", props.get_double("/controls/throttle") * 100.0,
                            tm.thrust / 1000.0, tm.twr, ship->active_stage() + 1, ship->stage_count(), tm.stage_fuel, tm.mass / 1000.0);
            const std::string sas = props.get_string("/controls/sas");
            const char *earth_hold_status = "";
            if (sas == "earth-communications")
            {
                kosmos::Controls controls;
                controls.sas = sas;
                bool valid = false;
                ship->sas_direction(controls, valid);
                if (!valid)
                    earth_hold_status = " (Earth unavailable)";
                else if (ship->on_rails())
                    earth_hold_status = " (suspended on rails)";
                else if (!ship->attitude_control_available() ||
                         (ship->spec().rcs_modes && props.get_string("/controls/rcs-mode", "rot") == "off"))
                    earth_hold_status = " (attitude control unavailable)";
            }
            sl::gprintf(16, 88, {220, 220, 220}, "Pitch %.1f  Heading %.1f   Hold: %s%s   RCS: %s   Warp x%g%s%s", tm.pitch, tm.heading, sas.c_str(), earth_hold_status,
                        props.get_string("/controls/rcs-mode", "rot") == "lin" ? "LIN" : props.get_string("/controls/rcs-mode", "rot") == "off" ? "OFF" : "ROT", sim.current_warp(),
                        ship->on_rails() ? " (rails)" : "",
                        ship->on_rails() && props.get_bool("/sim/settings/nonspherical-gravity") ? "  GRAVITY FIELD SUSPENDED" : "");
            const std::string status = props.get_string("/autopilot/status");
            const bool autopilot_enabled = props.get_bool("/autopilot/enabled") || props.get_bool("/autopilot/land/armed");
            if (autopilot_enabled || !status.empty())
                sl::gprintf(16, 106, {120, 220, 255}, "Autopilot: %s%s", status.c_str(), autopilot_enabled ? "" : " (off)");
            int nearest = -1;
            double nearest_distance = 0.0;
            int hud_end_y = 88;
            for (std::size_t i = 0; i < sim.bases.size(); ++i)
            {
                const kosmos::Base &bs = sim.bases[i];
                const int body = universe.find(bs.body);
                if (body < 0)
                    continue;
                const double d = glm::length(universe.position(body) + bs.direction * (universe.bodies[static_cast<std::size_t>(body)].radius + bs.height) - fb.position);
                if (nearest < 0 || d < nearest_distance)
                {
                    nearest = static_cast<int>(i);
                    nearest_distance = d;
                }
            }
            hud_end_y = props.get_bool("/autopilot/enabled") || !status.empty() ? 106 : 88;
            if (nearest >= 0)
                hud_end_y = props.get_bool("/autopilot/enabled") || !status.empty() ? 124 : 106;
            if (nearest >= 0)
                sl::gprintf(16, props.get_bool("/autopilot/enabled") || !status.empty() ? 124 : 106, {190, 190, 255}, "Nearest base: %s (%s)   %s",
                            sim.bases[static_cast<std::size_t>(nearest)].name.c_str(), sim.bases[static_cast<std::size_t>(nearest)].body.c_str(), format_distance(nearest_distance).c_str());
            if (netplay.mode() != kosmos::NetPlay::Mode::none)
            {
                const int line_y = (props.get_bool("/autopilot/enabled") || !status.empty() ? 124 : 106) + (nearest >= 0 ? 18 : 0);
                std::string near_text;
                double near_d = 0.0;
                for (const kosmos::Ghost &g : netplay.ghosts())
                {
                    if (!g.have_state || g.age > 5.0 || g.state.soi < 0 || g.state.soi >= static_cast<int>(universe.bodies.size()))
                        continue;
                    const double d = glm::length(universe.position(g.state.soi) + g.shown_position() - fb.position);
                    if (near_text.empty() || d < near_d)
                    {
                        near_text = g.nick;
                        near_d = d;
                    }
                }
                hud_end_y = line_y;
                sl::gprintf(16, line_y, {255, 200, 90}, "%s%s%s", netplay.status().c_str(), near_text.empty() ? "" : "   Nearest player: ", near_text.empty() ? "" : (near_text + " " + format_distance(near_d)).c_str());
            }
            if (!map_mode && props.get_bool("/sim/settings/help-text"))
            {
                sl::gprintf(16, hud_end_y + 24, {200, 210, 220}, "%s", "[Num+/Num*] Throttle  [Ctrl+Num+/-] +/-  [Space] Stage  [Num 2 8 1 3 4 6] Rotate  [Arrows] RCS  [Num5 [ ] ; '] Hold  [P] Autopilot");
                sl::gprintf(16, hud_end_y + 42, {200, 210, 220}, "%s", "[R/T] Warp  [R-drag] Look  [- =] Zoom  [X/Z] FOV  [O] Orbit  [Ctrl+M] Map  [C] Cockpit/3D  [V] Ship  [F2] Cameras  [F3] Settings  [F4] Properties  [F11] Lua  [F12] Multiplayer  [J] Jump to player  [F5-F9] MFD  [F10] Scenario  [Shift+R] Reset");
            }
    }

    namespace
    {
        // The nearest hotspot a ray (ship frame) passes through, no further than `limit`. A lever may be grabbed where it rests or where it has swung to.
        const kosmos::Cockpit::VcHotspot *hotspot_on_ray(const Ship &ship, const std::function<glm::mat4(const kosmos::VcAnimation &)> &animation, const glm::vec3 &origin_local, const glm::vec3 &dir_local, float limit, float &hit_t)
        {
            const kosmos::Cockpit::VcHotspot *spot = nullptr;
            float spot_t = limit;
            for (const kosmos::Cockpit::VcHotspot &hs : ship.spec().cockpit.vc_hotspots)
            {
                glm::mat4 moved(1.0f);
                if (!hs.follow.empty())
                    for (const kosmos::VcAnimation &va : ship.spec().cockpit.vc_animations)
                        for (const std::string &pn : va.parts)
                            if (pn == hs.follow)
                                moved = animation(va);
                bool hit = false;
                float t = 0.0f;
                for (int pass = 0; pass < 2 && !hit; ++pass)
                {
                    glm::vec3 corner[3];
                    for (int ci = 0; ci < 3; ++ci)
                        corner[ci] = glm::vec3((pass == 0 ? glm::mat4(1.0f) : moved) * glm::vec4(hs.quad[ci][0], hs.quad[ci][1], hs.quad[ci][2], 1.0f));
                    const glm::vec3 q0 = corner[0], e1 = corner[1] - q0, e2 = corner[2] - q0, n = glm::cross(e1, e2);
                    const float denom = glm::dot(dir_local, n);
                    if (std::fabs(denom) < 1e-12f)
                        continue;
                    t = glm::dot(q0 - origin_local, n) / denom;
                    if (t <= 0.0f || t >= spot_t + 0.02f)
                        continue;
                    const glm::vec3 d = origin_local + dir_local * t - q0;
                    const float u = glm::dot(d, e1) / glm::dot(e1, e1), v = glm::dot(d, e2) / glm::dot(e2, e2);
                    hit = u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f;
                }
                if (!hit)
                    continue;
                spot = &hs;
                spot_t = t;
            }
            hit_t = spot_t;
            return spot;
        }

        // A small label by the mouse pointer, kept on screen.
        void draw_tooltip(const std::string &text, int screen_w, int screen_h)
        {
            static sl::Font *font = nullptr;
            if (text.empty())
                return;
            if (!font)
                font = sl::open_sans_font(15);
            if (!font)
                return;
            const int w = sl::text_length(font, text) + 12, h = sl::text_height(font) + 6;
            const int x = std::clamp(static_cast<int>(sl::mouse_x()) + 16, 0, std::max(0, screen_w - w));
            const int y = std::clamp(static_cast<int>(sl::mouse_y()) + 20, 0, std::max(0, screen_h - h));
            sl::rectfill(sl::screen, static_cast<float>(x), static_cast<float>(y), static_cast<float>(x + w), static_cast<float>(y + h), {12, 16, 22, 225});
            sl::rect(sl::screen, static_cast<float>(x), static_cast<float>(y), static_cast<float>(x + w), static_cast<float>(y + h), {120, 150, 170, 255});
            sl::textout(font, x + 6, y + 3, {235, 240, 245, 255}, text);
        }
    } // namespace

    void App::draw_cockpit3d_overlay()
    {
    sl::PropertyTree &props = *props_ref;
    sl::FlightBody &fb = *fb_ref;
            if (!ship->spec().cockpit.vc_model.empty())
            {
                // Debug: outline every hotspot where picking will test it; selected buttons are outlined in red.
                const bool debug = props.get_bool("/sim/settings/vc-hotspots");
                const glm::vec3 f = glm::normalize(ship_rel), r = glm::normalize(glm::cross(f, glm::vec3(up))), u2 = glm::cross(r, f);
                const glm::mat3 to_w = glm::mat3_cast(glm::quat(fb.orientation));
                const glm::vec3 o_local = glm::transpose(to_w) * (-glm::vec3(ship->origin() - eye_world));
                const float focal = static_cast<float>(screen_h * 0.5 / std::tan(glm::radians(view_fov * 0.5)));
                const glm::mat3 from_local = to_w;
                for (const kosmos::Cockpit::VcHotspot &hs : ship->spec().cockpit.vc_hotspots)
                {
                    const bool active = hs.outline_active && control_lit(sim, hs.control);
                    if (!debug && !active)
                        continue;
                    glm::mat4 moved(1.0f);
                    for (const kosmos::VcAnimation &va : ship->spec().cockpit.vc_animations)
                        for (const std::string &pn : va.parts)
                            if (!hs.follow.empty() && pn == hs.follow)
                                moved = vc_animation_matrix(*ship, va);
                    glm::vec3 c[4];
                    for (int i = 0; i < 4; ++i)
                        c[i] = glm::vec3(moved * glm::vec4(hs.quad[i][0], hs.quad[i][1], hs.quad[i][2], 1.0f));
                    const int order[5] = {0, 1, 3, 2, 0};
                    for (int i = 0; i < 4; ++i)
                    {
                        float px[2], py[2];
                        bool ok = true;
                        for (int k = 0; k < 2; ++k)
                        {
                            const glm::vec3 w = from_local * (c[order[i + k]] - o_local);
                            const float z = glm::dot(w, f);
                            if (z < 0.01f)
                                ok = false;
                            px[k] = screen_w * 0.5f + focal * glm::dot(w, r) / z;
                            py[k] = screen_h * 0.5f - focal * glm::dot(w, u2) / z;
                        }
                        if (!ok)
                            continue;
                        if (active)
                            for (float off : {-1.0f, 0.0f, 1.0f})
                                sl::line(sl::screen, px[0] + off, py[0] + off, px[1] + off, py[1] + off, {255, 40, 30});
                        else
                            sl::line(sl::screen, px[0], py[0], px[1], py[1], {255, 60, 60});
                    }
                }
            }
            // Pick MFD buttons by casting the mouse ray at the two screen quads.
            const bool down = ui_mouse_down();
            if (down && (!drag.mfd_mouse_prev || drag.vc_drag_spot))
            {
                const kosmos::Cockpit cp = resolved_cockpit(ship->spec().cockpit, props.get_int("/sim/active-ship"));
                const glm::vec3 fwd = glm::normalize(ship_rel);
                const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(up)));
                const glm::vec3 cup = glm::cross(right, fwd);
                const float ty = std::tan(glm::radians(static_cast<float>(view_fov) * 0.5f));
                const float nx = (2.0f * sl::mouse_x() / screen_w - 1.0f) * ty * static_cast<float>(screen_w) / screen_h;
                const float ny = (1.0f - 2.0f * sl::mouse_y() / screen_h) * ty;
                const glm::vec3 dir = glm::normalize(fwd + right * nx + cup * ny);
                const glm::mat3 basis = cockpit_screen_basis();
                const glm::mat3 to_world = glm::mat3_cast(glm::quat(fb.orientation));
                const glm::vec3 origin_local = glm::transpose(to_world) * (-glm::vec3(ship->origin() - eye_world));
                const glm::vec3 dir_local = glm::transpose(to_world) * dir;
                if (drag.mfd_mouse_prev)
                {
                    // Held button: a drag hotspot follows the mouse along its quad.
                    const kosmos::Cockpit::VcHotspot &hs = *drag.vc_drag_spot;
                    const glm::vec3 q0(hs.quad[0][0], hs.quad[0][1], hs.quad[0][2]), e1 = glm::vec3(hs.quad[1][0], hs.quad[1][1], hs.quad[1][2]) - q0, e2 = glm::vec3(hs.quad[2][0], hs.quad[2][1], hs.quad[2][2]) - q0, n = glm::cross(e1, e2);
                    const float denom = glm::dot(dir_local, n);
                    if (std::fabs(denom) > 1e-12f)
                    {
                        const float t = glm::dot(q0 - origin_local, n) / denom;
                        const float v = glm::dot(origin_local + dir_local * t - q0, e2) / glm::dot(e2, e2);
                        const double level = std::clamp(props.get_double(hs.drag) + hs.drag_gain * (v - drag.vc_drag_v), hs.drag_min, hs.drag_max);
                        const double moved = level - props.get_double(hs.drag);
                        props.set_double(hs.drag, level < 0.01 && level > -0.01 ? 0.0 : level);
                        if (drag.vc_drag_twin)
                        {
                            const kosmos::Cockpit::VcHotspot &tw = *drag.vc_drag_twin;
                            const double twin_level = std::clamp(props.get_double(tw.drag) + moved * (tw.drag_gain / hs.drag_gain), tw.drag_min, tw.drag_max);
                            props.set_double(tw.drag, twin_level < 0.01 && twin_level > -0.01 ? 0.0 : twin_level);
                        }
                        drag.vc_drag_v = v;
                    }
                }
                else if (!cp.vc_model.empty())
                {
                    // Ray-cast the MFD screen triangles of the virtual cockpit and press the button under the hit.
                    const auto &screens = vis.vc_screen_tris(ship->spec().cockpit, ship->spec().directory);
                    float best = 1e30f;
                    int best_side = -1;
                    glm::vec2 best_uv{0.0f};
                    for (int side = 0; side < 2; ++side)
                        for (const Visuals::VcTri &tri : screens[side])
                        {
                            const glm::vec3 e1 = tri.p[1] - tri.p[0], e2 = tri.p[2] - tri.p[0];
                            const glm::vec3 pv = glm::cross(dir_local, e2);
                            const float det = glm::dot(e1, pv);
                            if (std::fabs(det) < 1e-9f)
                                continue;
                            const glm::vec3 tv = origin_local - tri.p[0];
                            const float bu = glm::dot(tv, pv) / det;
                            const glm::vec3 qv = glm::cross(tv, e1);
                            const float bv = glm::dot(dir_local, qv) / det;
                            const float t = glm::dot(e2, qv) / det;
                            if (bu < 0.0f || bv < 0.0f || bu + bv > 1.0f || t <= 0.0f || t >= best)
                                continue;
                            best = t;
                            best_side = side;
                            best_uv = tri.uv[0] * (1.0f - bu - bv) + tri.uv[1] * bu + tri.uv[2] * bv;
                        }
                    // Hotspots (bezel buttons, HUD and nav buttons) win when they are nearer than the screen.
                    float spot_t = best;
                    const kosmos::Cockpit::VcHotspot *spot = hotspot_on_ray(*ship, [&](const kosmos::VcAnimation &va) { return vc_animation_matrix(*ship, va); }, origin_local, dir_local, best, spot_t);
                    if (props.get_bool("/sim/settings/vc-hotspots"))
                        sim.log("VC click o=" + std::to_string(origin_local.x) + "," + std::to_string(origin_local.y) + "," + std::to_string(origin_local.z) + " d=" + std::to_string(dir_local.x) + "," + std::to_string(dir_local.y) + "," + std::to_string(dir_local.z) + (spot ? " HIT " + spot->control.property : ""));
                    if (spot && !spot->drag.empty())
                    {
                        const glm::vec3 q0(spot->quad[0][0], spot->quad[0][1], spot->quad[0][2]), e1 = glm::vec3(spot->quad[1][0], spot->quad[1][1], spot->quad[1][2]) - q0, e2 = glm::vec3(spot->quad[2][0], spot->quad[2][1], spot->quad[2][2]) - q0, n = glm::cross(e1, e2);
                        const float denom = glm::dot(dir_local, n);
                        if (std::fabs(denom) > 1e-12f)
                        {
                            drag.vc_drag_spot = spot;
                            drag.vc_drag_twin = nullptr;
                            // Twin levers (pod-left and pod-right): a grab near the middle of the pair moves both.
                            const std::string &name = spot->drag;
                            const bool is_left = name.size() > 5 && name.compare(name.size() - 5, 5, "-left") == 0;
                            const bool is_right = name.size() > 6 && name.compare(name.size() - 6, 6, "-right") == 0;
                            if (is_left || is_right)
                            {
                                const std::string other = is_left ? name.substr(0, name.size() - 5) + "-right" : name.substr(0, name.size() - 6) + "-left";
                                for (const kosmos::Cockpit::VcHotspot &h : ship->spec().cockpit.vc_hotspots)
                                {
                                    if (h.drag != other)
                                        continue;
                                    const glm::vec3 hit = origin_local + dir_local * spot_t;
                                    const glm::vec3 s0(spot->quad[0][0], spot->quad[0][1], spot->quad[0][2]);
                                    const glm::vec3 s1 = glm::vec3(spot->quad[1][0], spot->quad[1][1], spot->quad[1][2]) - s0;
                                    const float along = glm::dot(hit - s0, s1) / glm::dot(s1, s1);
                                    const glm::vec3 t0(h.quad[0][0], h.quad[0][1], h.quad[0][2]), t1 = glm::vec3(h.quad[1][0], h.quad[1][1], h.quad[1][2]) - t0;
                                    // Which side of the pair this lever is on, from where the other one lies along the same edge.
                                    const float toward = glm::dot(t0 + t1 * 0.5f - (s0 + s1 * 0.5f), s1);
                                    const bool inner = toward > 0.0f ? along > 0.5f : along < 0.5f;
                                    if (inner)
                                        drag.vc_drag_twin = &h;
                                    break;
                                }
                            }
                            drag.vc_drag_v = glm::dot(origin_local + dir_local * (glm::dot(q0 - origin_local, n) / denom) - q0, e2) / glm::dot(e2, e2);
                        }
                    }
                    else if (spot)
                        panel_control_press(sim, spot->control);
                    else if (best_side >= 0)
                    {
                        best_uv = vc_screen_hit_uv(best_uv, cp.vc_mfd_flip);
                        const int button = sim.mfd.hit_test(best_side, mfd_target_layout(true), best_uv.x * mfd_target_size, best_uv.y * mfd_target_size);
                        if (button >= 0)
                            sim.mfd.press(best_side, button);
                    }
                }
                else if (!cp.instruments.empty() || !cp.controls.empty())
                {
                    const glm::mat3 iface = interface_frame(ship->spec());
                    const glm::mat3 pb = iface * panel_basis(cp);
                    const glm::vec3 centre = glm::vec3(cp.eye) + iface * glm::vec3(cp.panel_pos);
                    const float denom = glm::dot(dir_local, pb[2]);
                    const float t = std::fabs(denom) < 1e-5f ? -1.0f : glm::dot(centre - origin_local, pb[2]) / denom;
                    if (t > 0.0f)
                    {
                        const glm::vec3 hit = origin_local + dir_local * t - centre;
                        const float pw = static_cast<float>(cp.panel_size), ph = pw * cp.panel_height / cp.panel_width;
                        const float u = glm::dot(hit, pb[0]) / pw + 0.5f, v = 0.5f - glm::dot(hit, pb[1]) / ph;
                        if (u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f)
                        {
                            const float scale = static_cast<float>(panel_target_width) / cp.panel_width;
                            panel_click(sim, cp, 0.0f, 0.0f, scale, u * cp.panel_width * scale, v * cp.panel_height * scale);
                        }
                    }
                }
                else
                for (int side = 0; side < 2; ++side)
                {
                    const CockpitScreen cs = cockpit_screen(cp, side);
                    const float denom = glm::dot(dir_local, basis[2]);
                    if (std::fabs(denom) < 1e-5f)
                        continue;
                    const float t = glm::dot(cs.centre - origin_local, basis[2]) / denom;
                    if (t <= 0.0f)
                        continue;
                    const glm::vec3 hit = origin_local + dir_local * t - cs.centre;
                    const float u = glm::dot(hit, basis[0]) / (cs.width * 1.18f) + 0.5f;
                    const float v = 0.5f - glm::dot(hit, basis[1]) / (cs.height * 1.13f);
                    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
                        continue;
                    const int button = sim.mfd.hit_test(side, mfd_target_layout(), u * mfd_target_size, v * mfd_target_size);
                    if (button >= 0)
                        sim.mfd.press(side, button);
                    break;
                }
            }
            if (!down)
                drag.vc_drag_spot = nullptr, drag.vc_drag_twin = nullptr;
            drag.mfd_mouse_prev = down;
            if (!down && props.get_bool("/sim/settings/tooltips") && !ship->spec().cockpit.vc_model.empty() && !(sl::gui_available() && ImGui::GetIO().WantCaptureMouse))
            {
                const glm::vec3 fwd = glm::normalize(ship_rel);
                const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(up)));
                const glm::vec3 cup = glm::cross(right, fwd);
                const float ty = std::tan(glm::radians(static_cast<float>(view_fov) * 0.5f));
                const float nx = (2.0f * sl::mouse_x() / screen_w - 1.0f) * ty * static_cast<float>(screen_w) / screen_h;
                const float ny = (1.0f - 2.0f * sl::mouse_y() / screen_h) * ty;
                const glm::mat3 to_world = glm::mat3_cast(glm::quat(fb.orientation));
                const glm::vec3 origin_local = glm::transpose(to_world) * (-glm::vec3(ship->origin() - eye_world));
                const glm::vec3 dir_local = glm::transpose(to_world) * glm::normalize(fwd + right * nx + cup * ny);
                float t = 0.0f;
                if (const kosmos::Cockpit::VcHotspot *spot = hotspot_on_ray(*ship, [&](const kosmos::VcAnimation &va) { return vc_animation_matrix(*ship, va); }, origin_local, dir_local, 1e30f, t))
                    draw_tooltip(control_tooltip(spot->control), screen_w, screen_h);
            }
    }

    // The left button as the panels and MFDs see it: a press that lands on an imgui window belongs to that window.
    bool App::ui_mouse_down() const
    {
        if (!sl::mouse_button_down(sl::MouseButton::left))
            return false;
        if (drag.ui_drag_id < 0 && !drag.mfd_mouse_prev && sl::gui_available() && ImGui::GetIO().WantCaptureMouse)
            return false;
        return true;
    }

    // A press that lands on no control starts a drag; the offset follows the mouse until the button is released.
    void App::ui_drag(int id, bool inside, bool down, bool pressed, bool consumed, float &offset_x, float &offset_y)
    {
        const float mx = static_cast<float>(sl::mouse_x()), my = static_cast<float>(sl::mouse_y());
        if (pressed && !consumed && inside)
        {
            drag.ui_drag_id = id;
            drag.ui_drag_last_x = mx;
            drag.ui_drag_last_y = my;
        }
        else if (drag.ui_drag_id == id && down)
        {
            offset_x += mx - drag.ui_drag_last_x;
            offset_y += my - drag.ui_drag_last_y;
            drag.ui_drag_last_x = mx;
            drag.ui_drag_last_y = my;
        }
        if (!down && drag.ui_drag_id == id)
            drag.ui_drag_id = -1;
    }

    // Moves one MFD by its saved offset, keeping it on screen.
    void App::place_mfd(int side, kosmos::MfdLayout &layout)
    {
        float *off = drag.mfd_offset[side];
        drag.mfd_base[side][0] = layout.x;
        drag.mfd_base[side][1] = layout.y;
        layout.x = std::clamp(layout.x + off[0], 0.0f, std::max(0.0f, screen_w - layout.size));
        layout.y = std::clamp(layout.y + off[1], 0.0f, std::max(0.0f, screen_h - layout.size));
        off[0] = layout.x - drag.mfd_base[side][0];
        off[1] = layout.y - drag.mfd_base[side][1];
    }

    // An already placed MFD can be dragged by anything that is not a button.
    void App::drag_mfd(int side, kosmos::MfdLayout &layout, bool down, bool pressed, bool consumed)
    {
        const float mx = static_cast<float>(sl::mouse_x()), my = static_cast<float>(sl::mouse_y());
        const bool inside = mx >= layout.x && mx <= layout.x + layout.size && my >= layout.y && my <= layout.y + layout.size;
        ui_drag(1 + side, inside, down, pressed, consumed, drag.mfd_offset[side][0], drag.mfd_offset[side][1]);
        layout.x = std::clamp(drag.mfd_base[side][0] + drag.mfd_offset[side][0], 0.0f, std::max(0.0f, screen_w - layout.size));
        layout.y = std::clamp(drag.mfd_base[side][1] + drag.mfd_offset[side][1], 0.0f, std::max(0.0f, screen_h - layout.size));
        drag.mfd_offset[side][0] = layout.x - drag.mfd_base[side][0];
        drag.mfd_offset[side][1] = layout.y - drag.mfd_base[side][1];
    }

    void App::draw_panel2d()
    {
    sl::PropertyTree &props = *props_ref;
            kosmos::Cockpit cp = ship->spec().cockpit;
            const auto panels = cp.panels_2d();
            const auto &shown = panels[static_cast<std::size_t>(std::clamp(panel_index, 0, static_cast<int>(panels.size()) - 1))];
            cp.instruments = shown.instruments;
            cp.controls = shown.controls;
            const std::string panel_root = "/ships/ship[" + std::to_string(props.get_int("/sim/active-ship")) + "]";
            for (kosmos::Cockpit::PanelControl &pc : cp.controls)
                for (std::string *path : {&pc.property, &pc.gate_property})
                    if (const std::size_t token = path->find("{ship}"); token != std::string::npos)
                        path->replace(token, 6, panel_root);
            cp.panel_width = shown.width;
            cp.panel_height = shown.height;
            cp.panel_image = shown.image;
            const float scale = std::min(screen_w / cp.panel_width, screen_h * 0.55f / cp.panel_height);
            float ox = (screen_w - cp.panel_width * scale) * 0.5f + drag.panel_offset[0], oy = screen_h - cp.panel_height * scale + drag.panel_offset[1];
            sim.mfd.bus_power = !sim.active_ship() || !sim.active_ship()->systems() || sim.active_ship()->systems()->instruments_powered();
            sim.mfd.update(frame);
            const bool down = ui_mouse_down();
            const bool pressed = down && !drag.mfd_mouse_prev;
            bool consumed = false;
            if (pressed)
                consumed = panel_click(sim, cp, ox, oy, scale, static_cast<float>(sl::mouse_x()), static_cast<float>(sl::mouse_y()));
            else if (down && panel_slider >= 0 && panel_slider < static_cast<int>(cp.controls.size()))
            {
                panel_slider_set(sim, cp.controls[static_cast<std::size_t>(panel_slider)], oy, scale, static_cast<float>(sl::mouse_y()));
                if (panel_slider_twin >= 0 && panel_slider_twin < static_cast<int>(cp.controls.size()))
                    panel_slider_set(sim, cp.controls[static_cast<std::size_t>(panel_slider_twin)], oy, scale, static_cast<float>(sl::mouse_y()));
            }
            if (!down)
                panel_slider = panel_slider_twin = -1;
            const float mx = static_cast<float>(sl::mouse_x()), my = static_cast<float>(sl::mouse_y());
            const bool inside = mx >= ox && mx <= ox + cp.panel_width * scale && my >= oy && my <= oy + cp.panel_height * scale;
            ui_drag(0, inside, down, pressed, consumed, drag.panel_offset[0], drag.panel_offset[1]);
            // Only a 40 px sliver has to stay on screen, so the panel can be pushed almost out of view in any direction.
            const float pw = cp.panel_width * scale, ph = cp.panel_height * scale, keep = 40.0f;
            const float home_x = (screen_w - pw) * 0.5f, home_y = screen_h - ph;
            drag.panel_offset[0] = std::clamp(drag.panel_offset[0], keep - pw - home_x, screen_w - keep - home_x);
            drag.panel_offset[1] = std::clamp(drag.panel_offset[1], keep - ph - home_y, screen_h - keep - home_y);
            if (panel_slider >= 0)
                drag.ui_drag_id = -1;
            drag.mfd_mouse_prev = down;
            draw_panel(sim, cp, cp.panel_image.empty() ? "" : (std::filesystem::path(ship->spec().directory) / cp.panel_image).string(), ox, oy, scale);
            if (!down && inside && props_ref->get_bool("/sim/settings/tooltips") && !(sl::gui_available() && ImGui::GetIO().WantCaptureMouse))
                draw_tooltip(panel_tooltip(sim, cp, ox, oy, scale, mx, my), screen_w, screen_h);
    }

    void App::draw_cockpit_hud_mfds(bool dashboard)
    {
    sl::PropertyTree &props = *props_ref;
            const std::string root = "/ships/ship[" + std::to_string(props.get_int("/sim/active-ship")) + "]";
            const std::vector<kosmos::Instrument> &panel = ship->spec().cockpit.panel;
            const int cell = 150;
            const int panel_y = dashboard ? static_cast<int>(screen_h - std::min(260.0f, screen_h * 0.3f) - 100.0f - 62.0f) : screen_h - 120;
            const int left = screen_w / 2 - static_cast<int>(panel.size()) * cell / 2;
            for (std::size_t i = 0; i < panel.size(); ++i)
            {
                std::string path = panel[i].property;
                const std::size_t token = path.find("{ship}");
                if (token != std::string::npos)
                    path.replace(token, 6, root);
                char value[64];
                std::snprintf(value, sizeof value, panel[i].format.c_str(), props.get_double(path) * panel[i].scale);
                const int x = left + static_cast<int>(i) * cell;
                sl::rectfill(sl::screen, x, panel_y, x + cell - 6, panel_y + 42, {10, 24, 20});
                sl::rect(sl::screen, x, panel_y, x + cell - 6, panel_y + 42, {60, 160, 110});
                sl::gprintf(x + 6, panel_y + 4, {110, 200, 150}, "%s", panel[i].label.c_str());
                sl::gprintf(x + 6, panel_y + 22, {200, 255, 220}, "%s", value);
            }
    }

    void App::draw_mfd_overlay(bool dashboard, bool cockpit_mfd_overlay)
    {
            const bool show_hud = cockpit_mfd_overlay || mfd_display == 2;
            const float size = show_hud ? std::min({480.0f, screen_h * 0.62f, screen_w * 0.36f}) : std::min(360.0f, screen_h * 0.4f);
            kosmos::MfdLayout left, right;
            left.size = right.size = size;
            left.hud = right.hud = show_hud;
            left.y = right.y = show_hud ? 140.0f : screen_h - size - 90.0f;
            left.x = show_hud ? 20.0f : screen_w / 2.0f - size - 70.0f;
            right.x = show_hud ? screen_w - size - 20.0f : screen_w / 2.0f + 70.0f;
            sim.mfd.bus_power = !sim.active_ship() || !sim.active_ship()->systems() || sim.active_ship()->systems()->instruments_powered();
            sim.mfd.update(frame);
            const bool down = ui_mouse_down();
            const bool pressed = down && !drag.mfd_mouse_prev;
            place_mfd(0, left);
            place_mfd(1, right);
            // A see-through display's buttons are its outlined labels; a side that is not shown gets an empty layout so it cannot be hit.
            kosmos::MfdLayout hit_left = left, hit_right = right;
            if (!(hud_sides & 1))
                hit_left.size = 0.0f;
            if (!(hud_sides & 2))
                hit_right.size = 0.0f;
            const bool consumed = sim.mfd.handle_mouse(hit_left, hit_right, pressed, sl::mouse_x(), sl::mouse_y());
            // The close button hides that display; F5 brings both back.
            for (int side = 0; side < 2; ++side)
                if (sim.mfd.unit(side).closed)
                {
                    sim.mfd.unit(side).closed = false;
                    hud_sides &= ~(1 << side);
                }
            if (hud_sides & 1)
                drag_mfd(0, left, down, pressed, consumed);
            if (hud_sides & 2)
                drag_mfd(1, right, down, pressed, consumed);
            drag.mfd_mouse_prev = down;
            if (hud_sides & 1)
                sim.mfd.draw(0, left);
            if (hud_sides & 2)
                sim.mfd.draw(1, right);
    }

    // The on-screen display, MFDs and messages.
    void App::draw_hud()
    {
    kosmos::Universe &universe = *universe_ref;
    sl::PropertyTree &props = *props_ref;
    const kosmos::Body &soi_body = *soi_body_ref;
        // HUD.
        const std::string soi_name = soi_body.name;
        if (show_info)
            sl::gprintf(16, 12, {240, 240, 240}, "KOSMOS   %s   T+%s   %s   FOV %.0f   HUD %s", ship->name.c_str(), format_time(universe.time()).c_str(), kosmos::format_utc(universe.julian_date()).c_str(), view_fov, hud.mode() == sl::HudMode::off ? "off" : sl::hud_mode_name(hud.mode()));
        if (ship->crashed())
            sl::gprintf_center(screen_h / 2 - 20, {255, 90, 80}, ship->systems() && ship->systems()->crew_lost() ? "*** CREW LOST - press Shift+R to reset ***" : "*** VESSEL LOST - press Shift+R to reset ***");
        else if (ship->systems() && !ship->systems()->alarms().empty() && ((sl::time_ms() / 450) % 4 != 0 || ship->systems()->mode() == kosmos::LifeSupportMode::off))
            sl::gprintf_center(96, {255, 80, 70}, "!! %s !!", ship->systems()->alarm_text().c_str());
        if (show_info)
        {
        draw_flight_readout(soi_name);
        }
        const std::vector<std::string> &messages = sim.messages();
        for (std::size_t i = 0; i < messages.size() && !options.clean; ++i)
            sl::gprintf(16, screen_h - 60 - static_cast<int>(messages.size() - i) * 16, {255, 200, 120}, "%s", messages[i].c_str());
        const bool dashboard = in_cockpit && !in_cockpit3d && ship->spec().cockpit.mfds && cockpit_mfd == 0;
        const bool panel2d = dashboard && !ship->spec().cockpit.panels_2d().empty();
        const bool cockpit_mfd_overlay = in_cockpit && !in_cockpit3d && ship->spec().cockpit.mfds && cockpit_mfd != 0;
        if (in_cockpit3d)
        {
        draw_cockpit3d_overlay();
        }
        if (panel2d)
        {
        draw_panel2d();
        }
        else if (dashboard)
        {
            const float size = std::min(260.0f, screen_h * 0.3f);
            const float top = screen_h - size - 100.0f;
            sl::rectfill(sl::screen, 0, top - 70.0f, static_cast<float>(screen_w), static_cast<float>(screen_h), {14, 16, 20, 235});
            sl::rect(sl::screen, 0, top - 70.0f, static_cast<float>(screen_w), static_cast<float>(screen_h), {70, 78, 90, 255}, 2.0f);
            kosmos::MfdLayout left, right;
            left.size = right.size = size;
            left.y = right.y = top;
            left.x = screen_w / 2.0f - size - 70.0f;
            right.x = screen_w / 2.0f + 70.0f;
            sim.mfd.bus_power = !sim.active_ship() || !sim.active_ship()->systems() || sim.active_ship()->systems()->instruments_powered();
            sim.mfd.update(frame);
            const bool down = ui_mouse_down();
            const bool pressed = down && !drag.mfd_mouse_prev;
            place_mfd(0, left);
            place_mfd(1, right);
            const bool consumed = sim.mfd.handle_mouse(left, right, pressed, sl::mouse_x(), sl::mouse_y());
            drag_mfd(0, left, down, pressed, consumed);
            drag_mfd(1, right, down, pressed, consumed);
            drag.mfd_mouse_prev = down;
            sim.mfd.draw(0, left);
            sim.mfd.draw(1, right);
        }
        if (in_cockpit && !in_cockpit3d && !panel2d)
        {
        draw_cockpit_hud_mfds(dashboard);
        }
        if ((cockpit_mfd_overlay ? cockpit_mfd == 1 : mfd_display > 0) && !dashboard && !in_cockpit3d && !map_mode && !galaxy_mode)
        {
        draw_mfd_overlay(dashboard, cockpit_mfd_overlay);
        }
        // The weather line is a notice: it shows for a few seconds when the METAR (or station) changes.
        {
            const std::string key = props.get_string("/environment/station") + "|" + props.get_string("/environment/metar");
            if (key != memory.weather_seen)
            {
                memory.weather_seen = key;
                memory.weather_until = std::chrono::steady_clock::now() + std::chrono::seconds(8);
            }
        }
        if (!options.hide_info && std::chrono::steady_clock::now() < memory.weather_until && props.get_bool("/sim/settings/weather", true) && !props.get_string("/environment/station").empty())
            sl::gprintf_center(screen_h - 40, {170, 205, 235}, "Wind %03.0f/%.0f m/s  rain %.0f%%  cloud %.0f%%  vis %.1f km   %s%s",
                               props.get_double("/environment/wind-from"), props.get_double("/environment/wind-speed"), props.get_double("/environment/rain") * 100.0,
                               props.get_double("/environment/cloud-cover") * 100.0, props.get_double("/environment/visibility") / 1000.0,
                               props.get_string("/environment/station").c_str(), props.get_bool("/environment/live") ? " (live)" : "");
        if (!options.hide_info && props.get_bool("/sim/settings/clock"))
            sl::gprintf_center(screen_h - 22, {235, 235, 200}, "Warp x%g%s%s   %s   T+%s", sim.current_warp(), ship->on_rails() ? " (rails)" : "",
                               ship->on_rails() && props.get_bool("/sim/settings/nonspherical-gravity") ? "  GRAVITY FIELD SUSPENDED" : "",
                               kosmos::format_utc(universe.julian_date()).c_str(), format_time(universe.time()).c_str());
        if (galaxy_mode)
            gview.draw(sim.galaxy, sim.system_index());
    }
} // namespace kosmos::app
