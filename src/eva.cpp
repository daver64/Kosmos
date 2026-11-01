#include "eva.h"

#include "sl.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace kosmos
{
    namespace
    {
        sl::Font *hud_font = nullptr, *hud_font_small = nullptr;
    }

    namespace
    {
        constexpr double walk_speed = 3.5, run_speed = 9.0, crouch_speed = 1.5, ground_accel = 40.0, jump_speed = 4.4;
        constexpr double pack_accel = 2.0, pack_burn = 1.0 / 250.0;
        // Air in this range of density (kg/m^3) is breathable without a suit's help.
        constexpr double breathable_min = 0.5, breathable_max = 3.0;
        constexpr double upright_density = 0.05, upright_height = 50.0;

        double ground_radius(const sl::Terrain *terrain, double body_radius, const sl::dvec3 &position)
        {
            const double d = glm::length(position);
            if (!terrain || d < 1.0)
                return body_radius;
            return body_radius + terrain->height_at_direction(position / d);
        }

        sl::dvec3 safe_normalize(const sl::dvec3 &v, const sl::dvec3 &fallback)
        {
            const double l = glm::length(v);
            return l > 1e-9 ? v / l : fallback;
        }
    } // namespace

    sl::dvec3 Astronaut::local_up() const
    {
        return safe_normalize(position, sl::dvec3(0.0, 1.0, 0.0));
    }

    sl::dvec3 Astronaut::eye_world(const Universe &universe) const
    {
        const sl::dvec3 up = local_up();
        return universe.position(body) + position + up * (grounded || upright ? eye_height - 0.7 * crouch : 0.0);
    }

    void Astronaut::leave(const Universe &universe, int body_id, const sl::dvec3 &world_position, const sl::dvec3 &world_velocity,
                          const sl::dvec3 &look, const sl::Terrain *terrain, bool landed)
    {
        active = true;
        body = body_id;
        const sl::FlightBody &pb = *universe.world.body(body);
        position = world_position - pb.position;
        velocity = world_velocity - pb.velocity;
        const double ground = ground_radius(terrain, universe.bodies[static_cast<std::size_t>(body)].radius, position);
        const double d = glm::length(position);
        // A spot on the ground is placed onto it; one in space stays where it is.
        if (landed || d - ground < 3.0)
        {
            position = position / d * ground;
            velocity = glm::cross(pb.spin_axis * pb.spin_rate, position);
            grounded = true;
        }
        else
            grounded = false;
        const sl::dvec3 up = local_up();
        const sl::dvec3 fwd = safe_normalize(look - up * glm::dot(look, up), sl::dvec3(1.0, 0.0, 0.0));
        const sl::dvec3 right = glm::normalize(glm::cross(fwd, up));
        orientation = glm::normalize(glm::quat_cast(sl::dmat3(right, glm::cross(right, fwd), -fwd)));
        pack = !grounded;
        suit.thrusting = 0.0;
        height = 0.0;
        speed = 0.0;
        update_suit(universe, 0.0);
    }

    void Astronaut::turn(const EvaInput &input)
    {
        const sl::dvec3 up = local_up();
        if (!upright)
        {
            const sl::dvec3 right = orientation * sl::dvec3(1.0, 0.0, 0.0);
            orientation = glm::normalize(glm::angleAxis(input.yaw, view_up()) * glm::angleAxis(input.pitch, right) * glm::angleAxis(input.roll, -forward()) * orientation);
            return;
        }
        // Upright: turn about the vertical, tilt the view up and down only as far as straight up or down, and keep the horizon level.
        sl::dvec3 fwd = forward();
        fwd = glm::angleAxis(input.yaw, up) * fwd;
        double pitch = std::asin(std::clamp(glm::dot(fwd, up), -1.0, 1.0)) + input.pitch;
        pitch = std::clamp(pitch, -1.5, 1.5);
        const sl::dvec3 flat = safe_normalize(fwd - up * glm::dot(fwd, up), safe_normalize(glm::cross(up, orientation * sl::dvec3(1.0, 0.0, 0.0)), sl::dvec3(1.0, 0.0, 0.0)));
        fwd = flat * std::cos(pitch) + up * std::sin(pitch);
        const sl::dvec3 right = glm::normalize(glm::cross(fwd, up));
        orientation = glm::normalize(glm::quat_cast(sl::dmat3(right, glm::cross(right, fwd), -fwd)));
    }

    void Astronaut::update_suit(const Universe &universe, double dt)
    {
        const sl::FlightBody &pb = *universe.world.body(body);
        suit.ambient_density = universe.world.air_density_at(pb.position + position);
        suit.environment = suit.ambient_density < breathable_min || suit.ambient_density > breathable_max;
        suit.jetpack = suit.environment;
        if (!suit.environment)
        {
            suit.pressure = 1.0;
            suit.failed = false;
            return;
        }
        suit.oxygen = std::max(0.0, suit.oxygen - dt / suit.oxygen_seconds);
        suit.battery = std::max(0.0, suit.battery - dt / suit.battery_seconds);
        if (suit.oxygen <= 0.0)
            suit.pressure = std::max(0.0, suit.pressure - dt / 60.0);
        else
            suit.pressure = std::min(1.0, suit.pressure + dt / 10.0);
        suit.failed = suit.pressure <= 0.0;
    }

    void Astronaut::recharge(double dt)
    {
        suit.oxygen = std::min(1.0, suit.oxygen + dt / 20.0);
        suit.battery = std::min(1.0, suit.battery + dt / 30.0);
        suit.propellant = std::min(1.0, suit.propellant + dt / 30.0);
        if (suit.health > 0.0)
            suit.health = std::min(1.0, suit.health + dt / 60.0);
        if (suit.oxygen > 0.0)
            suit.pressure = std::min(1.0, suit.pressure + dt / 5.0);
    }

    void Astronaut::carry(const Universe &universe, const sl::Terrain *terrain, double dt)
    {
        if (!active || body < 0 || !grounded || dt <= 0.0)
            return;
        const sl::FlightBody &pb = *universe.world.body(body);
        const Body &planet = universe.bodies[static_cast<std::size_t>(body)];
        const sl::dvec3 omega = pb.spin_axis * pb.spin_rate;
        const double w = glm::length(omega);
        if (w > 1e-12)
            position = glm::angleAxis(std::fmod(w * dt, 2.0 * 3.14159265358979323846), omega / w) * position;
        position = local_up() * ground_radius(terrain, planet.radius, position);
        velocity = glm::cross(omega, position);
        update_suit(universe, dt);
    }

    void Astronaut::step(const Universe &universe, const sl::Terrain *terrain, const EvaInput &input, double dt, const Solids &solids)
    {
        if (!active || body < 0)
            return;
        const sl::FlightBody &pb = *universe.world.body(body);
        const Body &planet = universe.bodies[static_cast<std::size_t>(body)];
        const sl::dvec3 omega = pb.spin_axis * pb.spin_rate;
        suit.thrusting = 0.0;
        crouch += ((grounded && input.crouch ? 1.0 : 0.0) - crouch) * std::min(1.0, dt * 10.0);

        const int steps = std::max(1, static_cast<int>(std::ceil(dt / 0.02)));
        const double h = dt / steps;
        for (int s = 0; s < steps; ++s)
        {
            const double d = glm::length(position);
            const sl::dvec3 up = position / d;
            const double gravity = planet.gm / (d * d);
            const double rho = universe.world.air_density_at(pb.position + position);
            const double ground = ground_radius(terrain, planet.radius, position);
            height = d - ground;
            sl::dvec3 surface = velocity - glm::cross(omega, position);
            if (!grounded && height <= 0.02 && glm::dot(surface, up) <= 0.1)
                grounded = true;

            // Walking direction follows the way the view faces, along the ground.
            const sl::dvec3 fwd = forward();
            const sl::dvec3 flat = safe_normalize(fwd - up * glm::dot(fwd, up), safe_normalize(glm::cross(up, orientation * sl::dvec3(1.0, 0.0, 0.0)), sl::dvec3(1.0, 0.0, 0.0)));
            const sl::dvec3 side = glm::cross(flat, up);
            sl::dvec3 wish = flat * input.forward + side * input.right;
            if (glm::length(wish) > 1.0)
                wish = glm::normalize(wish);

            if (grounded)
            {
                const double up_speed = std::max(0.0, glm::dot(surface, up));
                sl::dvec3 horizontal = surface - up * glm::dot(surface, up);
                const sl::dvec3 delta = wish * (crouch > 0.5 ? crouch_speed : input.run ? run_speed : walk_speed) - horizontal;
                const double limit = ground_accel * h, length = glm::length(delta);
                horizontal += length > limit ? delta * (limit / length) : delta;
                const bool jumping = input.jump && up_speed < 0.5;
                surface = horizontal + up * (jumping ? jump_speed : 0.0);

                // Turn with the planet while carried over the ground.
                const double omega_length = glm::length(omega);
                sl::dvec3 next = position + surface * h;
                if (omega_length > 1e-12)
                    next = glm::angleAxis(omega_length * h * spin_scale, omega / omega_length) * next;
                const double next_ground = ground_radius(terrain, planet.radius, next), next_d = glm::length(next);
                const double travel = glm::length(horizontal) * h;
                if (!jumping && next_ground - next_d > 0.15 + 1.2 * travel)
                {
                    // Too steep to climb: stand still against it.
                    next = omega_length > 1e-12 ? glm::angleAxis(omega_length * h * spin_scale, omega / omega_length) * position : position;
                    horizontal = sl::dvec3(0.0);
                    surface = sl::dvec3(0.0);
                }
                else if (!jumping && next_d - next_ground < 0.4)
                    next = next / next_d * next_ground;
                else
                    grounded = false;
                if (solids)
                {
                    const sl::dvec3 fixed = solids(position, next, 0.0);
                    if (glm::length(fixed - next) > 1e-9)
                    {
                        const double nd = glm::length(fixed), g = ground_radius(terrain, planet.radius, fixed);
                        next = fixed / nd * g;
                    }
                }
                position = next;
                velocity = surface + glm::cross(omega, position);
                continue;
            }

            // In the air or in space: inertial motion under gravity, with drag in an atmosphere.
            // Advance by the time the planet actually turned this frame, or the walker drifts against the quantised ground.
            const double ha = h * spin_scale;
            sl::dvec3 accel = -up * gravity;
            const sl::dvec3 relative = velocity - glm::cross(omega, position);
            accel -= relative * glm::length(relative) * (9.81 / (55.0 * 55.0)) * (rho / 1.225);
            if (rho > 0.3)
                accel += wish * 3.0;
            const double stick = std::sqrt(input.forward * input.forward + input.right * input.right + input.up * input.up);
            if (suit.jetpack && pack && suit.propellant > 0.0 && suit.battery > 0.0 && !suit.failed && stick > 0.0)
            {
                const sl::dvec3 push = forward() * input.forward + orientation * sl::dvec3(1.0, 0.0, 0.0) * input.right + view_up() * input.up;
                accel += glm::normalize(push) * pack_accel * std::min(1.0, stick);
                suit.propellant = std::max(0.0, suit.propellant - pack_burn * std::min(1.0, stick) * ha);
                suit.thrusting = std::min(1.0, stick);
            }
            velocity += accel * ha;
            const sl::dvec3 before = position;
            position += velocity * ha;
            if (solids)
            {
                const double above = glm::length(before) - ground_radius(terrain, planet.radius, before);
                const sl::dvec3 fixed = solids(before, position, above);
                const sl::dvec3 push = fixed - position;
                if (glm::length(push) > 1e-9)
                {
                    // Only the motion relative to the ground is stopped by a wall, not the planet's spin.
                    const sl::dvec3 spin = glm::cross(omega, before), normal = glm::normalize(push);
                    velocity -= normal * std::min(0.0, glm::dot(velocity - spin, normal));
                    position = fixed;
                }
            }

            const double new_d = glm::length(position), new_ground = ground_radius(terrain, planet.radius, position);
            if (new_d < new_ground)
            {
                const sl::dvec3 new_up = position / new_d;
                const double impact = -glm::dot(velocity - glm::cross(omega, position), new_up);
                position = new_up * new_ground;
                velocity = glm::cross(omega, position);
                grounded = true;
                if (impact > 20.0)
                    suit.pressure = std::max(0.0, suit.pressure - (impact - 20.0) * 0.05);
            }
        }

        height = glm::length(position) - ground_radius(terrain, planet.radius, position);
        const double rho = universe.world.air_density_at(pb.position + position);
        upright = grounded || height < upright_height || rho > upright_density;
        speed = glm::length(velocity - glm::cross(omega, position));
        turn(input);
        update_suit(universe, dt);
    }

    void draw_suit_hud(const Astronaut &a, const Universe &universe, int screen_w, int screen_h, double ship_distance, double ship_speed, const char *ship_name)
    {
        // Fonts need the window, so they are opened on the first frame the suit HUD is drawn.
        if (!hud_font)
        {
            hud_font = sl::open_sans_font(20);
            hud_font_small = sl::open_sans_font(15);
        }
        sl::Font *font = hud_font, *small = hud_font_small;
        const sl::Colour green{90, 255, 150, 255}, amber{255, 200, 70, 255}, red{255, 80, 70, 255}, dim{40, 90, 60, 200};
        const Suit &s = a.suit;
        const int margin = 24, bar_w = 150, bar_h = 12;
        const int x0 = margin, y0 = screen_h - margin - 176;
        char ship_line[160] = "";
        if (ship_name && ship_distance >= 0.0)
            std::snprintf(ship_line, sizeof ship_line, ship_distance < 30.0 ? "%s  %.0f m, %.1f m/s - Enter to board" : "%s  %.0f m, %.1f m/s", ship_name, ship_distance, ship_speed);
        // The panel is as wide as its widest line, but never off the screen.
        const int panel_w = std::min(std::max(bar_w + 300, sl::text_length(small, ship_line) + 24), screen_w - 2 * margin);
        sl::rectfill(sl::screen, static_cast<float>(x0 - 12), static_cast<float>(y0 - 28), static_cast<float>(x0 - 12 + panel_w), static_cast<float>(y0 + 172), {0, 16, 8, 120});
        sl::textout(font, x0, y0 - 26, green, s.environment ? "SUIT  LIFE SUPPORT" : "SUIT  AMBIENT AIR OK");
        auto bar = [&](int row, const char *label, double level, bool low_is_bad = true)
        {
            const int y = y0 + row * 24;
            const sl::Colour colour = !low_is_bad ? green : level < 0.15 ? red : level < 0.35 ? amber : green;
            sl::textout(small, x0, y, green, label);
            sl::rect(sl::screen, static_cast<float>(x0 + 64), static_cast<float>(y + 3), static_cast<float>(x0 + 64 + bar_w), static_cast<float>(y + 3 + bar_h), dim, 1.0f);
            sl::rectfill(sl::screen, static_cast<float>(x0 + 64), static_cast<float>(y + 3), static_cast<float>(x0 + 64 + bar_w * std::clamp(level, 0.0, 1.0)), static_cast<float>(y + 3 + bar_h), colour);
            char text[16];
            std::snprintf(text, sizeof text, "%3.0f%%", level * 100.0);
            sl::textout(small, x0 + 64 + bar_w + 8, y, colour, text);
        };
        bar(0, "O2", s.oxygen);
        bar(1, "POWER", s.battery);
        bar(2, "PRESS", s.pressure);
        if (s.jetpack)
        {
            bar(3, "RCS", s.propellant);
            sl::textout(small, x0, y0 + 100, a.pack ? amber : dim, a.pack ? "PACK ON  (F)" : "PACK OFF (F)");
        }
        else
            sl::textout(small, x0, y0 + 100, dim, "No manoeuvring pack in air");

        // Readouts on the right.
        const sl::dvec3 up = a.local_up();
        const sl::dvec3 fwd = a.forward();
        const sl::dvec3 east = safe_normalize(glm::cross(universe.pole(a.body), up), sl::dvec3(1.0, 0.0, 0.0));
        const sl::dvec3 north = glm::cross(up, east);
        double heading = std::atan2(glm::dot(fwd, east), glm::dot(fwd, north)) * 180.0 / 3.14159265358979323846;
        if (heading < 0.0)
            heading += 360.0;
        const double vertical_raw = glm::dot(a.velocity - glm::cross(universe.pole(a.body) * universe.world.body(a.body)->spin_rate, a.position), up);
        // Standing on the ground the height and vertical speed jitter by millimetres; show them as zero.
        const bool on_ground = a.height < 0.2;
        const double vertical = on_ground && std::fabs(vertical_raw) < 0.3 ? 0.0 : vertical_raw;
        const int rx = x0 + bar_w + 130;
        char line[96];
        std::snprintf(line, sizeof line, "HDG %03.0f", heading);
        sl::textout(small, rx, y0, green, line);
        std::snprintf(line, sizeof line, "ALT %.1f m", on_ground ? 0.0 : a.height);
        sl::textout(small, rx, y0 + 24, green, line);
        std::snprintf(line, sizeof line, "SPD %.1f m/s", a.speed);
        sl::textout(small, rx, y0 + 48, green, line);
        std::snprintf(line, sizeof line, "VS  %+.1f m/s", vertical);
        sl::textout(small, rx, y0 + 72, green, line);
        const Body &body = universe.bodies[static_cast<std::size_t>(a.body)];
        const double g = body.gm / std::max(1.0, glm::dot(a.position, a.position));
        std::snprintf(line, sizeof line, "%s   %.2f g", body.name.c_str(), g / 9.80665);
        sl::textout(small, x0, y0 + 124, green, line);

        if (ship_line[0])
            sl::textout(small, x0, y0 + 148, ship_distance < 30.0 ? amber : green, ship_line);
        if (s.environment && s.failed)
            sl::textout(font, (screen_w - sl::text_length(font, "SUIT PRESSURE LOST")) / 2, screen_h / 4, red, "SUIT PRESSURE LOST");
        else if (s.environment && (s.oxygen < 0.15 || s.battery < 0.1))
            {
            const char *warning = s.oxygen < 0.15 ? "LOW OXYGEN" : "LOW POWER";
            sl::textout(font, (screen_w - sl::text_length(font, warning)) / 2, screen_h / 4, red, warning);
        }

        // Reticle.
        const float cx = screen_w * 0.5f, cy = screen_h * 0.5f;
        sl::line(sl::screen, cx - 8, cy, cx - 3, cy, green);
        sl::line(sl::screen, cx + 3, cy, cx + 8, cy, green);
        sl::line(sl::screen, cx, cy - 8, cx, cy - 3, green);
        sl::line(sl::screen, cx, cy + 3, cx, cy + 8, green);
    }
} // namespace kosmos
