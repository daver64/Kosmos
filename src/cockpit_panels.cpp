#include "cockpit_panels.h"

#include <map>

namespace kosmos::app
{
    kosmos::MfdLayout mfd_target_layout()
    {
        kosmos::MfdLayout layout;
        layout.size = mfd_target_frame;
        layout.x = (mfd_target_size - mfd_target_frame) * 0.5f;
        layout.y = 22.0f;
        return layout;
    }

    CockpitScreen cockpit_screen(const kosmos::Cockpit &cp, int side)
    {
        return {glm::vec3(cp.eye) + glm::vec3(side == 0 ? -0.17f : 0.17f, -0.14f, -0.7f), 0.24f, 0.24f};
    }

    glm::mat3 cockpit_screen_basis()
    {
        const glm::vec3 up(0.0f, std::cos(screen_tilt), -std::sin(screen_tilt));
        const glm::vec3 normal(0.0f, std::sin(screen_tilt), std::cos(screen_tilt));
        return glm::mat3(glm::vec3(1.0f, 0.0f, 0.0f), up, normal);
    }

    kosmos::MfdLayout panel_layout(const kosmos::Cockpit::PanelInstrument &inst, float ox, float oy, float scale)
    {
        kosmos::MfdLayout layout;
        layout.x = ox + inst.x * scale;
        layout.y = oy + inst.y * scale;
        layout.size = inst.size * scale;
        layout.bare = !inst.frame;
        return layout;
    }

    bool control_is_numeric(const std::string &v)
    {
        return !v.empty() && (std::isdigit(static_cast<unsigned char>(v[0])) || v[0] == '-' || v[0] == '.');
    }

    bool control_lit(kosmos::Sim &sim, const kosmos::Cockpit::PanelControl &c)
    {
        if (c.property.empty())
            return false;
        if (c.type == "toggle")
            return sim.props.get_double(c.property) > 0.5;
        if (c.value == "level-horizon")
            return sim.props.get_string("/controls/sas") == "attitude" && std::fabs(sim.props.get_double("/controls/target-pitch")) < 0.5;
        if (control_is_numeric(c.value))
            return std::fabs(sim.props.get_double(c.property == "/controls/pod-tilt" ? "/controls/pod-tilt-left" : c.property) - std::atof(c.value.c_str())) < 0.01;
        if (c.property == "/controls/rcs-mode")
            return sim.props.get_string(c.property, "rot") == c.value;
        return sim.props.get_string(c.property) == c.value;
    }

    // Lamps, slider handles and the like are drawn over the panel bitmap, which carries the artwork.
    // Knobs and switch levers are baked into the artwork at one setting, so they are covered first and redrawn to match the property.
    void draw_panel_cover(const kosmos::Cockpit::PanelControl &c, float ox, float oy, float scale)
    {
        if (c.type != "button")
            return;
        if (c.style == "knob")
            sl::circlefill(sl::screen, ox + c.cx * scale, oy + c.cy * scale, (c.r + 2.0f) * scale, {20, 21, 25, 255});
        else if (c.style == "lever")
            sl::rectfill(sl::screen, ox + c.x * scale, oy + c.y * scale, ox + (c.x + c.w) * scale, oy + (c.y + c.h) * scale, {33, 24, 24, 255});
    }

    void draw_panel_pointer(kosmos::Sim &sim, const kosmos::Cockpit::PanelControl &c, float ox, float oy, float scale)
    {
        if (!control_lit(sim, c))
            return;
        if (c.style == "knob")
        {
            const float cx = ox + c.cx * scale, cy = oy + c.cy * scale, a = c.angle * 3.14159265f / 180.0f;
            sl::circle(sl::screen, cx, cy, c.r * scale, {60, 62, 70, 255}, 1.0f);
            sl::line(sl::screen, cx, cy, cx + std::sin(a) * c.r * scale * 0.95f, cy - std::cos(a) * c.r * scale * 0.95f, {240, 240, 240, 255}, std::max(2.0f, 3.0f * scale));
        }
        else
        {
            const float x = ox + c.x * scale, y = oy + c.y * scale, w = c.w * scale, h = c.h * scale;
            sl::rectfill(sl::screen, x, y, x + w, y + h, {206, 52, 52, 255});
            const float mid = y + h * 0.5f;
            sl::rectfill(sl::screen, x, mid - 1.5f * scale, x + w, mid + 1.5f * scale, {255, 82, 74, 255});
        }
    }

    // Needles, bars and numbers drawn over a panel's dials; the artwork has only the scales.
    bool draw_panel_overlay(kosmos::Sim &sim, const kosmos::Cockpit::PanelControl &c, float ox, float oy, float scale)
    {
        if (c.type == "needle")
        {
            const float v = c.max != 0.0f ? static_cast<float>(std::clamp(sim.props.get_double(c.property) * c.scale / c.max, 0.0, 1.0)) : 0.0f;
            const float a = (c.a0 + (c.a1 - c.a0) * v) * 3.14159265f / 180.0f;
            const float cx = ox + c.cx * scale, cy = oy + c.cy * scale, len = c.r * scale;
            sl::line(sl::screen, cx - std::sin(a) * len * 0.15f, cy + std::cos(a) * len * 0.15f, cx + std::sin(a) * len, cy - std::cos(a) * len, {255, 200, 60, 255}, std::max(2.0f, 2.5f * scale));
            sl::circlefill(sl::screen, cx, cy, std::max(2.0f, 3.0f * scale), {255, 200, 60, 255});
            return true;
        }
        if (c.type == "bar")
        {
            const float v = c.max != c.min ? static_cast<float>(std::clamp((sim.props.get_double(c.property) * c.scale - c.min) / (c.max - c.min), 0.0, 1.0)) : 0.0f;
            const sl::Colour col = c.alarm && v < 0.1f ? sl::Colour{255, 90, 70, 255} : sl::Colour{70, 255, 110, 255};
            const float x = ox + c.x * scale, y = oy + c.y * scale, w = c.w * scale, h = c.h * scale;
            if (v > 0.0f && h > w)
                sl::rectfill(sl::screen, x, y + h * (1.0f - v), x + w, y + h, col);
            else if (v > 0.0f)
                sl::rectfill(sl::screen, x, y, x + w * v, y + h, col);
            return true;
        }
        if (c.type == "number")
        {
            char text[48];
            std::snprintf(text, sizeof text, c.format.c_str(), std::clamp(sim.props.get_double(c.property) * c.scale + c.offset, static_cast<double>(c.lo), static_cast<double>(c.hi)));
            static std::map<int, sl::Font *> fonts;
            const int point = std::max(6, static_cast<int>(std::round(c.h > 0.0f && c.h < 40.0f ? c.h * scale * 0.9f : 12.0f * scale)));
            auto font = fonts.find(point);
            if (font == fonts.end())
                font = fonts.emplace(point, sl::open_monospace_font(point)).first;
            const float x = ox + c.x * scale, y = oy + c.y * scale, w = c.w * scale;
            // Numbers sit on the panel artwork, so the translucent text backing is switched off for them.
            const sl::TextPanelStyle backing = sl::text_panel_style();
            sl::TextPanelStyle bare = backing;
            bare.enabled = false;
            sl::set_text_panel_style(bare);
            sl::textout(font->second, static_cast<int>(x + (w - sl::text_length(font->second, text)) * 0.5f), static_cast<int>(y), {110, 255, 140, 255}, text);
            sl::set_text_panel_style(backing);
            return true;
        }
        return false;
    }

    void draw_panel_control(kosmos::Sim &sim, const kosmos::Cockpit::PanelControl &c, float ox, float oy, float scale)
    {
        if (!c.gate_property.empty())
        {
            const double g = sim.props.get_double(c.gate_property);
            if (g < c.gate_min || g > c.gate_max)
                return;
        }
        if (draw_panel_overlay(sim, c, ox, oy, scale))
            return;
        if (c.type == "button" && (c.style == "knob" || c.style == "lever"))
        {
            draw_panel_pointer(sim, c, ox, oy, scale);
            return;
        }
        const float x = ox + c.x * scale, y = oy + c.y * scale, w = c.w * scale, h = c.h * scale;
        if (c.type == "slider")
        {
            const float v = static_cast<float>(std::clamp(sim.props.get_double(c.property), 0.0, 1.0));
            const float ty = y + h * (1.0f - v);
            sl::rectfill(sl::screen, x, ty, x + w, y + h, {90, 255, 120, 60});
            sl::rectfill(sl::screen, x, ty - 3 * scale, x + w, ty + 3 * scale, {255, 190, 60, 235});
            sl::rect(sl::screen, x, ty - 3 * scale, x + w, ty + 3 * scale, {40, 30, 10, 255}, 1.0f);
        }
        else if (c.type == "label" && c.w > 0.0f)
            sl::rectfill(sl::screen, x, y + h + 2, x + w, y + h + 3, {70, 90, 110, 255});
        else if (c.type == "button" || c.type == "toggle")
        {
            if (!c.label.empty())
                sl::rectfill(sl::screen, x, y, x + w, y + h, {24, 28, 36, 255});
            if (control_lit(sim, c))
            {
                sl::rectfill(sl::screen, x, y, x + w, y + h, {90, 255, 120, 55});
                sl::rect(sl::screen, x, y, x + w, y + h, {120, 255, 140, 255}, 2.0f);
            }
            else if (!c.label.empty())
                sl::rect(sl::screen, x, y, x + w, y + h, {90, 100, 115, 255}, 1.0f);
        }
        else if (c.type == "gauge")
        {
            const float v = c.max > 0.0f ? static_cast<float>(std::clamp(sim.props.get_double(c.property) * c.scale / c.max, 0.0, 1.0)) : 0.0f;
            sl::rectfill(sl::screen, x, y, x + w, y + h, {12, 16, 20, 255});
            sl::rectfill(sl::screen, x, y, x + w * v, y + h, c.alarm && v < 0.25f ? sl::Colour{230, 80, 60, 255} : sl::Colour{70, 200, 110, 255});
            sl::rect(sl::screen, x, y, x + w, y + h, {90, 100, 115, 255}, 1.0f);
            // The reading itself, inside the bar at its right-hand end.
            char reading[32];
            const double raw = sim.props.get_double(c.property) * c.scale;
            std::snprintf(reading, sizeof reading, std::fabs(raw) >= 100.0 ? "%.0f" : "%.1f", raw);
            sl::gprintf(static_cast<int>(x + w - 8.0f * std::strlen(reading) - 4), static_cast<int>(y + h * 0.5f - 7), {235, 245, 240}, "%s", reading);
        }
        else if (c.type == "lamp")
        {
            const bool on = c.value.empty() ? sim.props.get_double(c.property) > 0.5 : control_lit(sim, c);
            const sl::Colour col = on ? (c.alarm ? sl::Colour{255, 70, 50, 255} : sl::Colour{110, 255, 140, 255}) : sl::Colour{40, 44, 50, 255};
            sl::rectfill(sl::screen, x, y, x + w, y + h, col);
            sl::rect(sl::screen, x, y, x + w, y + h, {90, 100, 115, 255}, 1.0f);
        }
        if (c.type == "readout")
        {
            char value[64];
            std::snprintf(value, sizeof value, c.format.c_str(), sim.props.get_double(c.property) * c.scale);
            sl::gprintf(static_cast<int>(x), static_cast<int>(y), {110, 200, 150}, "%s", c.label.c_str());
            sl::gprintf(static_cast<int>(x), static_cast<int>(y + 18), {200, 255, 220}, "%s", value);
        }
        else if (!c.label.empty())
        {
            const bool beside = c.type == "gauge" || c.type == "lamp";
            sl::gprintf(static_cast<int>(beside ? x + w + 10 : x + 10), static_cast<int>(beside ? y + h * 0.5f - 7 : y + h * 0.5f - 7), {210, 220, 235}, "%s", c.label.c_str());
        }
    }

    // Returns the slider under (px, py) so the caller can keep dragging it, or -1.
    int panel_slider_at(const kosmos::Cockpit &cp, float ox, float oy, float scale, float px, float py)
    {
        for (std::size_t i = 0; i < cp.controls.size(); ++i)
        {
            const auto &c = cp.controls[i];
            if (c.type == "slider" && px >= ox + c.x * scale && px <= ox + (c.x + c.w) * scale && py >= oy + c.y * scale && py <= oy + (c.y + c.h) * scale)
                return static_cast<int>(i);
        }
        return -1;
    }

    void panel_slider_set(kosmos::Sim &sim, const kosmos::Cockpit::PanelControl &c, float oy, float scale, float py)
    {
        const double v = std::clamp(1.0 - static_cast<double>((py - oy - c.y * scale) / (c.h * scale)), 0.0, 1.0);
        if (c.property == "/controls/throttle")
            sim.props.set_bool("/autopilot/enabled", false);
        sim.props.set_double(c.property, v);
    }

    void panel_control_press(kosmos::Sim &sim, const kosmos::Cockpit::PanelControl &c)
    {
        if (c.type == "mfd")
        {
            if (c.unit < sim.mfd.unit_count())
                sim.mfd.press(c.unit, c.button);
        }
        else if (c.type == "hud")
            sim.props.set_string("/sim/hud-request", c.value);
        else if (c.type == "toggle")
            sim.props.set_double(c.property, sim.props.get_double(c.property) > 0.5 ? 0.0 : 1.0);
        else if (c.type == "button")
        {
            if (c.property == "/controls/sas")
                sim.props.set_bool("/autopilot/enabled", false);
            if (c.value == "level-horizon")
            {
                const kosmos::Ship *ship = sim.active_ship();
                sim.props.set_double("/controls/target-pitch", 0.0);
                sim.props.set_double("/controls/target-heading", ship ? ship->telemetry().heading : 0.0);
                sim.props.set_string("/controls/sas", "attitude");
            }
            else if (c.property == "/controls/pod-tilt")
            {
                sim.props.set_double("/controls/pod-tilt-left", std::atof(c.value.c_str()));
                sim.props.set_double("/controls/pod-tilt-right", std::atof(c.value.c_str()));
            }
            else if (control_is_numeric(c.value))
                sim.props.set_double(c.property, std::atof(c.value.c_str()));
            else
                sim.props.set_string(c.property, c.value);
        }
    }

    void draw_panel(kosmos::Sim &sim, const kosmos::Cockpit &cp, const std::string &image, float ox, float oy, float scale)
    {
        const float w = cp.panel_width * scale, h = cp.panel_height * scale;
        sl::Bitmap *back = image.empty() ? nullptr : sim.mfd.bitmap(image);
        if (back)
            sl::draw_sprite_region_rotated(back, 0, 0, back->width, back->height, ox, oy, w, h, w / 2, h / 2, 0.0f);
        else
        {
            sl::rectfill(sl::screen, ox, oy, ox + w, oy + h, {24, 26, 32, 255});
            sl::rect(sl::screen, ox, oy, ox + w, oy + h, {70, 78, 90, 255}, 2.0f);
        }
        for (std::size_t i = 0; i < cp.instruments.size() && static_cast<int>(i) < sim.mfd.unit_count(); ++i)
            if (sim.mfd.allowed(cp.instruments[i].mode))
                sim.mfd.draw(static_cast<int>(i), panel_layout(cp.instruments[i], ox, oy, scale));
        for (const kosmos::Cockpit::PanelControl &c : cp.controls)
            draw_panel_cover(c, ox, oy, scale);
        for (const kosmos::Cockpit::PanelControl &c : cp.controls)
            draw_panel_control(sim, c, ox, oy, scale);
    }

    int panel_slider = -1;

    bool panel_click(kosmos::Sim &sim, const kosmos::Cockpit &cp, float ox, float oy, float scale, float px, float py)
    {
        for (const auto &c : cp.controls)
        {
            if (c.type == "slider" || c.type == "label" || c.type == "readout" || c.type == "gauge" || c.type == "lamp" || c.type == "needle" || c.type == "bar" || c.type == "number" || px < ox + c.x * scale || px > ox + (c.x + c.w) * scale || py < oy + c.y * scale || py > oy + (c.y + c.h) * scale)
                continue;
            panel_control_press(sim, c);
            return true;
        }
        panel_slider = panel_slider_at(cp, ox, oy, scale, px, py);
        if (panel_slider >= 0)
        {
            panel_slider_set(sim, cp.controls[static_cast<std::size_t>(panel_slider)], oy, scale, py);
            return true;
        }
        for (std::size_t i = 0; i < cp.instruments.size() && static_cast<int>(i) < sim.mfd.unit_count(); ++i)
        {
            if (!sim.mfd.allowed(cp.instruments[i].mode))
                continue;
            const int button = sim.mfd.hit_test(static_cast<int>(i), panel_layout(cp.instruments[i], ox, oy, scale), px, py);
            if (button >= 0)
            {
                sim.mfd.press(static_cast<int>(i), button);
                return true;
            }
        }
        return false;
    }

    glm::mat3 panel_basis(const kosmos::Cockpit &cp)
    {
        const float tilt = static_cast<float>(glm::radians(cp.panel_tilt));
        const glm::vec3 up(0.0f, std::cos(tilt), -std::sin(tilt));
        const glm::vec3 normal(0.0f, std::sin(tilt), std::cos(tilt));
        return glm::mat3(glm::vec3(1.0f, 0.0f, 0.0f), up, normal);
    }

    // An upright building looks out along +X with its roof at -Z; this maps a cockpit frame (nose -Z, up +Y) onto that.
    glm::mat3 interface_frame(const kosmos::ShipSpec &spec)
    {
        if (spec.building() && !spec.horizontal)
            return glm::mat3(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(-1.0f, 0.0f, 0.0f));
        return glm::mat3(1.0f);
    }

    void resolve_ship_token(kosmos::Cockpit::PanelControl &pc, const std::string &root)
    {
        for (std::string *path : {&pc.property, &pc.gate_property})
            if (const std::size_t token = path->find("{ship}"); token != std::string::npos)
                path->replace(token, 6, root);
    }

    // The cockpit with "{ship}" in its control properties replaced by the ship's property path.
    kosmos::Cockpit resolved_cockpit(const kosmos::Cockpit &cp, int ship_index)
    {
        kosmos::Cockpit out = cp;
        const std::string root = "/ships/ship[" + std::to_string(ship_index) + "]";
        for (kosmos::Cockpit::PanelControl &pc : out.controls)
            resolve_ship_token(pc, root);
        for (kosmos::Cockpit::Panel2D &panel : out.extra_panels)
            for (kosmos::Cockpit::PanelControl &pc : panel.controls)
                resolve_ship_token(pc, root);
        return out;
    }

    sl::Mesh *make_screen_quad()
    {
        std::vector<sl::Vertex3D> v(4);
        const float xs[4] = {-0.5f, 0.5f, 0.5f, -0.5f}, ys[4] = {0.5f, 0.5f, -0.5f, -0.5f};
        // OpenGL render targets are stored bottom-up; Vulkan and Direct3D ones top-down.
        const bool bottom_up = sl::graphics_backend() == sl::GraphicsBackend::opengl;
        for (int i = 0; i < 4; ++i)
        {
            v[i].position[0] = xs[i];
            v[i].position[1] = ys[i];
            v[i].normal[2] = 1.0f;
            v[i].uv[0] = xs[i] + 0.5f;
            v[i].uv[1] = bottom_up ? ys[i] + 0.5f : 0.5f - ys[i];
        }
        return sl::create_mesh(v, {0, 2, 1, 0, 3, 2});
    }
} // namespace kosmos::app
