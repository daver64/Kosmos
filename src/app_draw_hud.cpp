#include "app_state.h"

namespace kosmos::app
{
    // The orbit line, HUD and labels over the scene.
    void App::draw_overlays()
    {
    kosmos::Universe &universe = *universe_ref;
    sl::PropertyTree &props = *props_ref;
    const kosmos::Telemetry &tm = *tm_ref;
    sl::FlightBody &fb = *fb_ref;
    const kosmos::Body &soi_body = *soi_body_ref;
        // Orbit line about the sphere-of-influence body, hidden where the body covers it.
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

        // Head-up display.
        if (hud.mode() != sl::HudMode::off && hud_allowed(hud.mode(), ship->spec().hud_modes()) && (in_cockpit || in_interior) && !map_mode && !galaxy_mode)
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
            hud.draw(hs);
        }

        if (in_eva)
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
                if (eva.building == s && interior_panel >= 0)
                    sl::gprintf_center(screen_h / 2 + 70, {120, 255, 150}, "Press Enter to use the panel");
                else if (s == building_at_door() && eva.building == s && !sim.ship(s)->spec().interior->airlock_name().empty())
                {
                    if (in_airlock)
                        sl::gprintf_center(screen_h / 2 + 90, {120, 255, 150}, room_kpa > kosmos::airlock_safe_kpa ? "Press Enter to depressurise and go out" : "Press Enter to go outside");
                }
                else if (s == building_at_door())
                    sl::gprintf_center(screen_h / 2 + 90, {120, 255, 150}, eva.building == s ? "Press Enter to go outside" : "Press Enter to go into %s", sim.ship(s)->name.c_str());
                if (eva.building == s && room_kpa >= 0.0)
                    sl::gprintf_center(screen_h / 2 + 130, {160, 210, 255}, "%s  %.0f kPa", room_label.empty() ? "Room" : room_label.c_str(), room_kpa);
                float x = 0.0f, y = 0.0f;
                if (distance < 1.0 || !project(view_proj, glm::vec3(rel / distance * 1000.0), x, y) || x < 0 || x > screen_w || y < 0 || y > screen_h)
                    continue;
                sl::circle(sl::screen, x, y, 6.0f, {120, 255, 150}, 1.0f);
                sl::gprintf(static_cast<int>(x) + 10, static_cast<int>(y) - 6, {120, 255, 150}, "DOOR %s  %s", sim.ship(s)->name.c_str(), format_distance(distance).c_str());
            }
            kosmos::draw_suit_hud(eva, universe, screen_w, screen_h, near_ship >= 0 ? ship_distance : -1.0, ship_speed, near_ship >= 0 ? sim.ship(near_ship)->name.c_str() : nullptr);
        }
        arsenal.draw_hud(sim, eva, screen_w, screen_h, eye_world);

        // Labels for bodies too small on screen to recognise.
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

    // Engine and wind sound.
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
        sound.set(sound_on ? rocket_level * (0.35 + 0.65 * air) : 0.0, sound_on ? rcs_level : 0.0, sound_on ? wind_level : 0.0);
        {
            // Event cues: one-shots on the edge of a state change. The first frame only records the state.
            static bool seen = false, was_landed = false, was_crashed = false, was_docked = false, was_alarm = false;
            static int was_stage = 0;
            static double alarm_next = 0.0;
            const bool landed_now = tm.landed, crashed_now = ship->crashed(), docked_now = props.get_bool("/dock/docked");
            const bool alarm_now = ship->systems() && !ship->systems()->alarms().empty();
            if (seen && sound_on)
            {
                if (crashed_now && !was_crashed)
                    sound.play("crash", 255);
                else if (landed_now && !was_landed && !crashed_now)
                    sound.play("touchdown", 220);
                if (docked_now != was_docked)
                    sound.play(docked_now ? "docking" : "undocking", 200);
                if (ship->active_stage() != was_stage)
                    sound.play("thump", 230);
                if (alarm_now && (!was_alarm || sl::time_ms() / 1000.0 > alarm_next))
                {
                    sound.play("warning_beep", 160);
                    alarm_next = sl::time_ms() / 1000.0 + 4.0;
                }
            }
            seen = true;
            was_landed = landed_now;
            was_crashed = crashed_now;
            was_docked = docked_now;
            was_alarm = alarm_now;
            was_stage = ship->active_stage();
        }
    }

    // The on-screen display, MFDs and messages.
    void App::draw_hud()
    {
    kosmos::Universe &universe = *universe_ref;
    sl::PropertyTree &props = *props_ref;
    const kosmos::Telemetry &tm = *tm_ref;
    sl::FlightBody &fb = *fb_ref;
    const kosmos::Body &soi_body = *soi_body_ref;
        // HUD.
        const std::string soi_name = soi_body.name;
        if (show_info)
            sl::gprintf(16, 12, {240, 240, 240}, "KOSMOS   %s   T+%s   %s", ship->name.c_str(), format_time(universe.time()).c_str(), kosmos::format_utc(universe.julian_date()).c_str());
        if (ship->crashed())
            sl::gprintf_center(screen_h / 2 - 20, {255, 90, 80}, ship->systems() && ship->systems()->crew_lost() ? "*** CREW LOST - press R to reset ***" : "*** VESSEL LOST - press R to reset ***");
        else if (ship->systems() && !ship->systems()->alarms().empty() && ((sl::time_ms() / 450) % 4 != 0 || ship->systems()->mode() == kosmos::LifeSupportMode::off))
            sl::gprintf_center(96, {255, 80, 70}, "!! %s !!", ship->systems()->alarm_text().c_str());
        if (show_info)
        {
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
                sl::gprintf(16, 106, {255, 220, 150}, "Hover %3.0f%%", props.get_double("/controls/hover") * 100.0);
            sl::gprintf(16, 70, {255, 220, 150}, "Throttle %3.0f%%   Thrust %.0f kN   TWR %.2f   Stage %d/%d fuel %.0f kg   Mass %.1f t", props.get_double("/controls/throttle") * 100.0,
                        tm.thrust / 1000.0, tm.twr, ship->active_stage() + 1, ship->stage_count(), tm.stage_fuel, tm.mass / 1000.0);
            sl::gprintf(16, 88, {220, 220, 220}, "Pitch %.1f  Heading %.1f   Hold: %s   Warp x%g%s", tm.pitch, tm.heading, props.get_string("/controls/sas").c_str(), sim.current_warp(),
                        ship->on_rails() ? " (rails)" : "");
            const std::string status = props.get_string("/autopilot/status");
            if (props.get_bool("/autopilot/enabled") || !status.empty())
                sl::gprintf(16, 106, {120, 220, 255}, "Autopilot: %s", status.c_str());
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
                sl::gprintf(16, hud_end_y + 24, {200, 210, 220}, "%s", "[Z/X] Throttle  [Shift/Ctrl] +/-  [Space] Stage  [WASDQE] Rotate  [Arrows] RCS  [T] Hold  [P] Autopilot");
                sl::gprintf(16, hud_end_y + 42, {200, 210, 220}, "%s", "[,/.] Warp  [R-drag] Look  [ [ ] ] Zoom  [O] Orbit  [Tab] Map  [C] Cockpit/3D  [V] Ship  [F2] Cameras  [F3] Settings  [F4] Properties  [F11] Lua  [F12] Multiplayer  [J] Jump to player  [F5-F9] MFD  [F10] Scenario  [R] Reset");
            }
        }
        const std::vector<std::string> &messages = sim.messages();
        for (std::size_t i = 0; i < messages.size(); ++i)
            sl::gprintf(16, screen_h - 60 - static_cast<int>(messages.size() - i) * 16, {255, 200, 120}, "%s", messages[i].c_str());
        const bool dashboard = in_cockpit && !in_cockpit3d && ship->spec().cockpit.mfds && cockpit_mfd == 0;
        const bool panel2d = dashboard && !ship->spec().cockpit.panels_2d().empty();
        const bool cockpit_mfd_overlay = in_cockpit && !in_cockpit3d && ship->spec().cockpit.mfds && cockpit_mfd != 0;
        if (in_cockpit3d)
        {
            // Pick MFD buttons by casting the mouse ray at the two screen quads.
            const bool down = sl::mouse_button_down(sl::MouseButton::left);
            if (down && !mfd_mouse_prev)
            {
                const kosmos::Cockpit cp = resolved_cockpit(ship->spec().cockpit, props.get_int("/sim/active-ship"));
                const glm::vec3 fwd = glm::normalize(ship_rel);
                const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(up)));
                const glm::vec3 cup = glm::cross(right, fwd);
                const float ty = std::tan(glm::radians(25.0f));
                const float nx = (2.0f * sl::mouse_x() / screen_w - 1.0f) * ty * static_cast<float>(screen_w) / screen_h;
                const float ny = (1.0f - 2.0f * sl::mouse_y() / screen_h) * ty;
                const glm::vec3 dir = glm::normalize(fwd + right * nx + cup * ny);
                const glm::mat3 basis = cockpit_screen_basis();
                const glm::mat3 to_world = glm::mat3_cast(glm::quat(fb.orientation));
                const glm::vec3 origin_local = glm::transpose(to_world) * (-glm::vec3(ship->origin() - eye_world));
                const glm::vec3 dir_local = glm::transpose(to_world) * dir;
                if (!cp.instruments.empty() || !cp.controls.empty())
                {
                    const glm::mat3 frame = interface_frame(ship->spec());
                    const glm::mat3 pb = frame * panel_basis(cp);
                    const glm::vec3 centre = glm::vec3(cp.eye) + frame * glm::vec3(cp.panel_pos);
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
            mfd_mouse_prev = down;
        }
        if (panel2d)
        {
            kosmos::Cockpit cp = ship->spec().cockpit;
            const auto panels = cp.panels_2d();
            const auto &shown = panels[static_cast<std::size_t>(std::clamp(panel_index, 0, static_cast<int>(panels.size()) - 1))];
            cp.instruments = shown.instruments;
            cp.controls = shown.controls;
            const std::string panel_root = "/ships/ship[" + std::to_string(props.get_int("/sim/active-ship")) + "]";
            for (kosmos::Cockpit::PanelControl &pc : cp.controls)
                if (const std::size_t token = pc.property.find("{ship}"); token != std::string::npos)
                    pc.property.replace(token, 6, panel_root);
            cp.panel_width = shown.width;
            cp.panel_height = shown.height;
            cp.panel_image = shown.image;
            const float scale = std::min(screen_w / cp.panel_width, screen_h * 0.55f / cp.panel_height);
            const float ox = (screen_w - cp.panel_width * scale) * 0.5f, oy = screen_h - cp.panel_height * scale;
            sim.mfd.bus_power = !sim.active_ship() || !sim.active_ship()->systems() || sim.active_ship()->systems()->instruments_powered();
            sim.mfd.update(frame);
            const bool down = sl::mouse_button_down(sl::MouseButton::left);
            if (down && !mfd_mouse_prev)
                panel_click(sim, cp, ox, oy, scale, static_cast<float>(sl::mouse_x()), static_cast<float>(sl::mouse_y()));
            else if (down && panel_slider >= 0 && panel_slider < static_cast<int>(cp.controls.size()))
                panel_slider_set(sim, cp.controls[static_cast<std::size_t>(panel_slider)], oy, scale, static_cast<float>(sl::mouse_y()));
            if (!down)
                panel_slider = -1;
            mfd_mouse_prev = down;
            draw_panel(sim, cp, cp.panel_image.empty() ? "" : (std::filesystem::path(ship->spec().directory) / cp.panel_image).string(), ox, oy, scale);
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
            const bool down = sl::mouse_button_down(sl::MouseButton::left);
            sim.mfd.handle_mouse(left, right, down && !mfd_mouse_prev, sl::mouse_x(), sl::mouse_y());
            mfd_mouse_prev = down;
            sim.mfd.draw(0, left);
            sim.mfd.draw(1, right);
        }
        if (in_cockpit && !in_cockpit3d && !panel2d)
        {
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
        if ((cockpit_mfd_overlay ? cockpit_mfd == 1 : mfd_display > 0) && !dashboard && !in_cockpit3d && !map_mode && !galaxy_mode)
        {
            const bool hud = cockpit_mfd_overlay || mfd_display == 2;
            const float size = hud ? 300.0f : std::min(320.0f, screen_h * 0.38f);
            kosmos::MfdLayout left, right;
            left.size = right.size = size;
            left.hud = right.hud = hud;
            left.y = right.y = hud ? 140.0f : screen_h - size - 90.0f;
            left.x = hud ? 20.0f : screen_w / 2.0f - size - 70.0f;
            right.x = hud ? screen_w - size - 20.0f : screen_w / 2.0f + 70.0f;
            sim.mfd.bus_power = !sim.active_ship() || !sim.active_ship()->systems() || sim.active_ship()->systems()->instruments_powered();
            sim.mfd.update(frame);
            const bool down = sl::mouse_button_down(sl::MouseButton::left);
            if (!hud)
                sim.mfd.handle_mouse(left, right, down && !mfd_mouse_prev, sl::mouse_x(), sl::mouse_y());
            mfd_mouse_prev = down;
            sim.mfd.draw(0, left);
            sim.mfd.draw(1, right);
        }
        if (lightning_flash > 0.02 && !map_mode && !galaxy_mode)
        {
            sl::set_blend_mode(sl::BlendMode::additive);
            sl::rectfill(sl::screen, 0.0f, 0.0f, static_cast<float>(screen_w), static_cast<float>(screen_h),
                         {static_cast<std::uint8_t>(std::min(1.0, lightning_flash) * 70.0), static_cast<std::uint8_t>(std::min(1.0, lightning_flash) * 75.0), static_cast<std::uint8_t>(std::min(1.0, lightning_flash) * 95.0), 255});
            sl::set_blend_mode(sl::BlendMode::normal);
        }
        // The weather line is a notice: it shows for a few seconds when the METAR (or station) changes.
        static std::string weather_seen;
        static auto weather_until = std::chrono::steady_clock::time_point{};
        {
            const std::string key = props.get_string("/environment/station") + "|" + props.get_string("/environment/metar");
            if (key != weather_seen)
            {
                weather_seen = key;
                weather_until = std::chrono::steady_clock::now() + std::chrono::seconds(8);
            }
        }
        if (!options.hide_info && std::chrono::steady_clock::now() < weather_until && props.get_bool("/sim/settings/weather", true) && !props.get_string("/environment/station").empty())
            sl::gprintf_center(screen_h - 40, {170, 205, 235}, "Wind %03.0f/%.0f m/s  rain %.0f%%  cloud %.0f%%  vis %.1f km   %s%s",
                               props.get_double("/environment/wind-from"), props.get_double("/environment/wind-speed"), props.get_double("/environment/rain") * 100.0,
                               props.get_double("/environment/cloud-cover") * 100.0, props.get_double("/environment/visibility") / 1000.0,
                               props.get_string("/environment/station").c_str(), props.get_bool("/environment/live") ? " (live)" : "");
        if (!options.hide_info && props.get_bool("/sim/settings/clock"))
            sl::gprintf_center(screen_h - 22, {235, 235, 200}, "Warp x%g%s   %s   T+%s", sim.current_warp(), ship->on_rails() ? " (rails)" : "",
                               kosmos::format_utc(universe.julian_date()).c_str(), format_time(universe.time()).c_str());
        if (galaxy_mode)
            gview.draw(sim.galaxy, sim.system_index());
    }
} // namespace kosmos::app
