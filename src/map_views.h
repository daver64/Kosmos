#pragma once

#include "app.h"

namespace kosmos::app
{
    /** Orthographic orrery of the system: the ecliptic is the XZ plane, +Y is north. */
    struct MapView
    {
        int focus = -1; // body id, or -1 for the ship
        double metres_per_pixel = 1.0e9;
        float yaw = 0.0f, pitch = 1.05f;

        void project_point(const sl::dvec3 &rel, float &x, float &y) const
        {
            const double cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
            const double a = cy * rel.x - sy * rel.z;
            const double depth = sy * rel.x + cy * rel.z;
            const double up = rel.y * cp - depth * sp;
            x = static_cast<float>(screen_w * 0.5 + a / metres_per_pixel);
            y = static_cast<float>(screen_h * 0.5 - up / metres_per_pixel);
        }

        void draw_path(const std::vector<sl::dvec3> &path, const sl::dvec3 &origin, const sl::Colour &colour) const
        {
            float px = 0.0f, py = 0.0f;
            bool have = false;
            for (const sl::dvec3 &p : path)
            {
                float x, y;
                project_point(p + origin, x, y);
                const bool ok = std::fabs(x) < 4.0e3f && std::fabs(y) < 4.0e3f;
                if (ok && have)
                    sl::line(sl::screen, px, py, x, y, colour);
                have = ok;
                px = x;
                py = y;
            }
        }

        void draw(kosmos::Universe &universe, Ship &ship, const kosmos::Telemetry &tm) const
        {
            sl::clear_to_colour(sl::screen, {2, 3, 8});
            const int ship_id = ship.body_id();
            const sl::dvec3 focus_pos = focus < 0 ? universe.position(ship_id) : universe.position(focus);
            const sl::dvec3 ship_origin = universe.position(ship_id) - focus_pos;

            for (std::size_t i = 1; i < universe.bodies.size(); ++i)
            {
                const kosmos::Body &b = universe.bodies[i];
                if (b.parent < 0 || b.minor)
                    continue;
                const kosmos::Body &parent = universe.bodies[static_cast<std::size_t>(b.parent)];
                sl::OrbitElements el;
                if (!universe.world.orbit_of(static_cast<int>(i), b.parent, el))
                    continue;
                const double size = el.semi_major_axis / metres_per_pixel;
                if (std::fabs(size) < 3.0 || std::fabs(size) > 1.0e5)
                    continue;
                const sl::dvec3 origin = universe.position(b.parent) - focus_pos;
                const sl::Colour colour = b.kind == kosmos::BodyKind::moon ? sl::Colour{70, 80, 100, 255} : sl::Colour{70, 110, 160, 255};
                draw_path(sl::orbit_path(parent.gm, el, 240, 1.0e13), origin, colour);
            }

            sl::OrbitElements ship_el;
            if (tm.soi >= 0 && universe.world.orbit_of(ship_id, tm.soi, ship_el))
            {
                const kosmos::Body &soi_body = universe.bodies[static_cast<std::size_t>(tm.soi)];
                draw_path(sl::orbit_path(soi_body.gm, ship_el, 240, std::min(soi_body.soi, 1.0e13)), universe.position(tm.soi) - focus_pos,
                          {80, 255, 120, 255});
            }

            for (std::size_t i = 0; i < universe.bodies.size(); ++i)
            {
                const kosmos::Body &b = universe.bodies[i];
                if (b.minor && metres_per_pixel > 5.0e8)
                    continue;
                float x, y;
                project_point(universe.position(static_cast<int>(i)) - focus_pos, x, y);
                if (x < -50 || x > screen_w + 50 || y < -50 || y > screen_h + 50)
                    continue;
                const float radius = std::clamp(static_cast<float>(b.radius / metres_per_pixel), 2.5f, 14.0f);
                const sl::Colour colour{static_cast<std::uint8_t>(b.colour[0] * 255.0f), static_cast<std::uint8_t>(b.colour[1] * 255.0f),
                                        static_cast<std::uint8_t>(b.colour[2] * 255.0f), 255};
                sl::circlefill(sl::screen, x, y, b.minor ? 1.5f : radius, colour);
                if (b.minor)
                    continue;
                // Moons are labelled only once their orbit is wide enough not to crowd the parent.
                if (b.kind == kosmos::BodyKind::moon && b.orbit.semi_major_axis / metres_per_pixel < 24.0)
                    continue;
                sl::gprintf(static_cast<int>(x) + static_cast<int>(radius) + 4, static_cast<int>(y) - 6, {200, 220, 255}, "%s", b.name.c_str());
            }

            float x, y;
            project_point(ship_origin, x, y);
            sl::circle(sl::screen, x, y, 6.0f, {80, 255, 120}, 1.0f);
            sl::gprintf(static_cast<int>(x) + 10, static_cast<int>(y) - 6, {80, 255, 120}, "%s", ship.name.c_str());
            if (ship_el.eccentricity < 1.0 && tm.soi >= 0)
            {
                const sl::dvec3 soi_origin = universe.position(tm.soi) - focus_pos;
                const std::vector<sl::dvec3> path = sl::orbit_path(universe.bodies[static_cast<std::size_t>(tm.soi)].gm, ship_el, 720, 1.0e13);
                auto mark = [&](const char *label, bool nearest) {
                    const sl::dvec3 *best = nullptr;
                    for (const sl::dvec3 &p : path)
                        if (!best || (nearest ? glm::length(p) < glm::length(*best) : glm::length(p) > glm::length(*best)))
                            best = &p;
                    if (!best)
                        return;
                    float ax, ay;
                    project_point(*best + soi_origin, ax, ay);
                    sl::circle(sl::screen, ax, ay, 3.0f, {120, 255, 150}, 1.0f);
                    sl::gprintf(static_cast<int>(ax) + 6, static_cast<int>(ay) - 6, {120, 255, 150}, "%s", label);
                };
                mark("Pe", true);
                mark("Ap", false);
            }

            sl::gprintf(16, 130, {240, 240, 240}, "MAP   focus: %s   scale: %s/px   T+%s", focus < 0 ? ship.name.c_str() : universe.bodies[static_cast<std::size_t>(focus)].name.c_str(),
                        format_distance(metres_per_pixel).c_str(), format_time(universe.time()).c_str());
            sl::gprintf_center(screen_h - 22, {200, 210, 220}, "[Tab] Flight view  [F] Focus  [ [ ] ] Zoom  [R-drag] Rotate  [,/.] Warp");
        }
    };

    /** Top-down chart of the galaxy centred on the current star; the destination is picked from the nearest stars. */
    struct GalaxyView
    {
        double ly_per_pixel = 0.16;
        int selected = -1;
        std::vector<int> candidates;

        void refresh(const kosmos::Galaxy &galaxy, int current)
        {
            candidates = galaxy.nearest(current, 16);
            if (selected < 0 && !candidates.empty())
                selected = candidates.front();
        }

        void step_selection(int direction)
        {
            if (candidates.empty())
                return;
            const auto at = std::find(candidates.begin(), candidates.end(), selected);
            const int i = at == candidates.end() ? 0 : static_cast<int>(at - candidates.begin());
            selected = candidates[static_cast<std::size_t>((i + direction + static_cast<int>(candidates.size())) % static_cast<int>(candidates.size()))];
        }

        void draw(const kosmos::Galaxy &galaxy, int current) const
        {
            const kosmos::Star &here = galaxy.star(current);
            sl::clear_to_colour(sl::screen, {1, 2, 6});
            const float cx = screen_w * 0.5f + 110.0f, cy = screen_h * 0.5f;
            auto to_screen = [&](const kosmos::Star &s, float &x, float &y)
            {
                x = cx + static_cast<float>((s.x - here.x) / ly_per_pixel);
                y = cy + static_cast<float>((s.z - here.z) / ly_per_pixel);
            };
            for (const double ring : {10.0, 20.0, 50.0, 100.0, 1000.0, 10000.0})
            {
                const float r = static_cast<float>(ring / ly_per_pixel);
                if (r > 12.0f && r < 1200.0f)
                {
                    sl::circle(sl::screen, cx, cy, r, {25, 40, 60}, 1.0f);
                    sl::gprintf(static_cast<int>(cx + r * 0.7071f) + 3, static_cast<int>(cy - r * 0.7071f), {60, 90, 120}, "%g ly", ring);
                }
            }
            const bool detailed = ly_per_pixel < 0.8;
            for (int i = 0; i < galaxy.size(); ++i)
            {
                const kosmos::Star &s = galaxy.star(i);
                float x, y;
                to_screen(s, x, y);
                if (x < 0 || y < 0 || x >= screen_w || y >= screen_h)
                    continue;
                const auto c = [&](float k) { return static_cast<std::uint8_t>(std::clamp(k * 255.0f, 0.0f, 255.0f)); };
                const float bright = detailed ? 1.0f : 0.55f;
                const sl::Colour colour{c(s.colour[0] * bright), c(s.colour[1] * bright), c(s.colour[2] * bright), 255};
                if (detailed)
                    sl::circlefill(sl::screen, x, y, s.spectral_class == 'M' ? 2.0f : s.spectral_class == 'K' ? 2.5f : 3.0f + (s.mass > 1.5 ? 1.0f : 0.0f), colour);
                else
                    sl::putpixel(sl::screen, static_cast<int>(x), static_cast<int>(y), colour);
            }
            if (detailed)
                for (int id : candidates)
                {
                    float x, y;
                    to_screen(galaxy.star(id), x, y);
                    sl::gprintf(static_cast<int>(x) + 7, static_cast<int>(y) - 4, id == selected ? sl::Colour{255, 240, 120} : sl::Colour{120, 150, 190}, "%s", galaxy.star(id).name.c_str());
                }
            sl::circle(sl::screen, cx, cy, 7.0f, {80, 255, 120}, 1.0f);
            sl::gprintf(static_cast<int>(cx) + 10, static_cast<int>(cy) + 6, {120, 255, 150}, "%s (here)", here.name.c_str());
            if (selected >= 0)
            {
                float x, y;
                to_screen(galaxy.star(selected), x, y);
                sl::line(sl::screen, cx, cy, x, y, {120, 110, 40});
                sl::circle(sl::screen, x, y, 8.0f, {255, 240, 120}, 1.0f);
                const kosmos::Star &s = galaxy.star(selected);
                sl::rectfill(sl::screen, 8, 8, 330, 168, {8, 16, 28});
                sl::gprintf(16, 16, {255, 240, 120}, "%s", s.name.c_str());
                sl::gprintf(16, 38, {200, 210, 220}, "Class %c   %.0f K   %.2f Msun", s.spectral_class, s.temperature, s.mass);
                sl::gprintf(16, 56, {200, 210, 220}, "Luminosity %.3g Lsun   Radius %.2f Rsun", s.luminosity, s.radius);
                sl::gprintf(16, 74, {200, 210, 220}, "Distance %.1f ly", galaxy.distance_ly(current, selected));
                sl::gprintf(16, 92, {200, 210, 220}, "%d planets%s", galaxy.planet_count(selected), s.real ? "   (known star)" : "");
                sl::gprintf(16, 124, {120, 255, 150}, "[Enter] Jump here and arrive in orbit");
                sl::gprintf(16, 142, {160, 170, 180}, "[Left/Right] Choose   [ [ ] ] Zoom");
            }
            sl::gprintf(16, screen_h - 40, {200, 210, 220}, "GALAXY   %d stars   %s per pixel", galaxy.size(), format_distance(ly_per_pixel * 9.4607e15).c_str());
            sl::gprintf_center(screen_h - 22, {200, 210, 220}, "[N] Back to flight  [Left/Right] Choose star  [ [ ] ] Zoom  [Enter] Jump");
        }
    };
} // namespace kosmos::app
