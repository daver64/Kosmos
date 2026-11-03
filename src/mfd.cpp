#include "mfd.h"

#include "font.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace kosmos
{
    namespace
    {
        constexpr float pi_f = 3.14159265f;

        std::map<int, sl::Font *> mfd_fonts;

        sl::Font *font_for(int point)
        {
            point = std::max(8, point);
            auto it = mfd_fonts.find(point);
            if (it == mfd_fonts.end())
                it = mfd_fonts.emplace(point, sl::open_monospace_font(point)).first;
            return it->second;
        }

        const char *const button_names[3] = {"PWR", "SEL", "MNU"};
    } // namespace

    void MfdCanvas::push(MfdCommand::Kind k, float a, float b, float c, float d, float e, float f)
    {
        MfdCommand cmd;
        cmd.kind = k;
        cmd.a = a;
        cmd.b = b;
        cmd.c = c;
        cmd.d = d;
        cmd.e = e;
        cmd.f = f;
        cmd.colour = colour;
        commands.push_back(std::move(cmd));
    }

    void MfdCanvas::text(float x, float y, const std::string &s, sol::optional<int> align)
    {
        MfdCommand cmd;
        cmd.kind = MfdCommand::Kind::text;
        cmd.a = x;
        cmd.b = y;
        cmd.colour = colour;
        cmd.text = s;
        cmd.align = align.value_or(0);
        commands.push_back(std::move(cmd));
    }

    void MfdCanvas::image(const std::string &file, float cx, float cy, float w, float h, sol::optional<float> angle, sol::optional<int> sx, sol::optional<int> sy, sol::optional<int> sw, sol::optional<int> sh)
    {
        MfdCommand cmd;
        cmd.kind = MfdCommand::Kind::image;
        cmd.a = cx;
        cmd.b = cy;
        cmd.c = w;
        cmd.d = h;
        cmd.e = angle.value_or(0.0f);
        cmd.text = (std::filesystem::path(directory) / file).string();
        cmd.src[0] = sx.value_or(0);
        cmd.src[1] = sy.value_or(0);
        cmd.src[2] = sw.value_or(0);
        cmd.src[3] = sh.value_or(0);
        commands.push_back(std::move(cmd));
    }

    sl::Bitmap *MfdSystem::bitmap(const std::string &path)
    {
        auto found = bitmaps_.find(path);
        if (found == bitmaps_.end())
        {
            sl::Bitmap *loaded = sl::load_bitmap(path);
            if (!loaded)
                error_ = "cannot load image " + path;
            found = bitmaps_.emplace(path, loaded).first;
        }
        return found->second;
    }

    void MfdCanvas::arc(float cx, float cy, float r, float a0, float a1)
    {
        // Angles are degrees clockwise from 12 o'clock.
        const int steps = std::max(4, static_cast<int>(std::fabs(a1 - a0) / 6.0f));
        float px = cx + r * std::sin(a0 * pi_f / 180.0f), py = cy - r * std::cos(a0 * pi_f / 180.0f);
        for (int i = 1; i <= steps; ++i)
        {
            const float a = (a0 + (a1 - a0) * i / steps) * pi_f / 180.0f;
            const float x = cx + r * std::sin(a), y = cy - r * std::cos(a);
            push(MfdCommand::Kind::line, px, py, x, y);
            px = x;
            py = y;
        }
    }

    void MfdSystem::init(sl::LuaRuntime &lua, sl::PropertyTree &)
    {
        lua_ = &lua;
        sol::state &state = lua.state();
        state.new_usertype<MfdCanvas>(
            "MfdCanvas", sol::no_constructor,
            "colour", &MfdCanvas::set_colour,
            "line", [](MfdCanvas &g, float x1, float y1, float x2, float y2) { g.push(MfdCommand::Kind::line, x1, y1, x2, y2); },
            "rect", [](MfdCanvas &g, float x1, float y1, float x2, float y2) { g.push(MfdCommand::Kind::rect, x1, y1, x2, y2); },
            "fillrect", [](MfdCanvas &g, float x1, float y1, float x2, float y2) { g.push(MfdCommand::Kind::fillrect, x1, y1, x2, y2); },
            "circle", [](MfdCanvas &g, float x, float y, float r) { g.push(MfdCommand::Kind::circle, x, y, r, 0); },
            "disc", [](MfdCanvas &g, float x, float y, float r) { g.push(MfdCommand::Kind::disc, x, y, r, 0); },
            "tri", [](MfdCanvas &g, float x1, float y1, float x2, float y2, float x3, float y3) { g.push(MfdCommand::Kind::tri, x1, y1, x2, y2, x3, y3); },
            "arc", &MfdCanvas::arc,
            "image", &MfdCanvas::image,
            "text", &MfdCanvas::text);
    }

    bool MfdSystem::register_mode(const std::string &addon, const std::string &id, const std::string &path, std::string &error, const std::string &domain)
    {
        std::ifstream in(path);
        if (!in)
        {
            error = "cannot open " + path;
            return false;
        }
        std::stringstream code;
        code << in.rdbuf();
        sol::protected_function_result r = lua_->state().safe_script(code.str(), sol::script_pass_on_error, path);
        if (!r.valid())
        {
            sol::error e = r;
            error = path + ": " + e.what();
            return false;
        }
        sol::object result = r;
        if (!result.is<sol::table>())
        {
            error = path + ": script must return a table";
            return false;
        }
        MfdMode mode;
        mode.table = result.as<sol::table>();
        mode.id = id;
        mode.directory = std::filesystem::path(path).parent_path().string();
        mode.addon = addon;
        mode.domain = domain;
        mode.name = mode.table.get_or<std::string>("name", id);
        modes_.push_back(std::move(mode));
        return true;
    }

    void MfdSystem::register_native(const std::string &addon, const std::string &id, const std::string &name, const std::string &directory, std::function<void(MfdCanvas &)> draw)
    {
        MfdMode mode;
        mode.table = lua_->state().create_table();
        mode.table["name"] = name;
        mode.table["draw"] = [draw = std::move(draw)](sol::table, MfdCanvas &g) { draw(g); };
        mode.id = id;
        mode.name = name;
        mode.addon = addon;
        mode.directory = directory;
        modes_.push_back(std::move(mode));
    }

    int MfdSystem::mode_by_id(const std::string &id) const
    {
        for (std::size_t i = 0; i < modes_.size(); ++i)
            if (modes_[i].id == id)
                return static_cast<int>(i);
        return -1;
    }

    bool MfdSystem::allowed(int mode) const
    {
        if (mode < 0 || mode >= static_cast<int>(modes_.size()))
            return false;
        const std::string &d = modes_[static_cast<std::size_t>(mode)].domain;
        if (std::find(hidden_.begin(), hidden_.end(), modes_[static_cast<std::size_t>(mode)].id) != hidden_.end())
            return false;
        return domain_allowed(mode);
    }

    bool MfdSystem::domain_allowed(int mode) const
    {
        if (mode < 0 || mode >= static_cast<int>(modes_.size()))
            return false;
        const std::string &d = modes_[static_cast<std::size_t>(mode)].domain;
        return !((domain_ == "atmosphere" && d == "space") || (domain_ == "space" && d == "atmosphere"));
    }

    bool MfdSystem::domain_allowed(const std::string &id) const
    {
        const int mode = mode_by_id(id);
        return mode < 0 || domain_allowed(mode);
    }

    bool MfdSystem::allowed(const std::string &id) const
    {
        const int mode = mode_by_id(id);
        return mode < 0 || allowed(mode);
    }

    void MfdSystem::set_hidden(const std::vector<std::string> &ids)
    {
        hidden_ = ids;
    }

    void MfdSystem::set_domain(const std::string &domain)
    {
        domain_ = domain;
        const int fallback = mode_by_id("surface");
        for (std::size_t i = 0; i < units_.size(); ++i)
            if (units_[i].mode >= 0 && !domain_allowed(units_[i].mode))
                set_mode(static_cast<int>(i), fallback);
    }

    int MfdSystem::visible_count() const
    {
        int n = 0;
        for (int i = 0; i < static_cast<int>(modes_.size()); ++i)
            n += allowed(i) ? 1 : 0;
        return n;
    }

    int MfdSystem::visible_mode(int n) const
    {
        for (int i = 0; i < static_cast<int>(modes_.size()); ++i)
            if (allowed(i) && n-- == 0)
                return i;
        return -1;
    }

    void MfdSystem::set_unit_count(int count)
    {
        units_.resize(static_cast<std::size_t>(std::max(count, 2)));
    }

    void MfdSystem::set_mode(int side, int mode)
    {
        if (side < 0 || side >= unit_count())
            return;
        MfdUnit &u = unit(side);
        if (!domain_allowed(mode))
            return;
        u.mode = mode;
        u.selecting = false;
        u.self = lua_->state().create_table();
        sol::protected_function init = modes_[static_cast<std::size_t>(mode)].table["init"];
        if (init.valid())
        {
            lua_->execute(""); // restarts the instruction budget, which direct calls into Lua do not do
            sol::protected_function_result r = init(u.self);
            if (!r.valid())
            {
                sol::error e = r;
                error_ = modes_[static_cast<std::size_t>(mode)].id + ": " + e.what();
            }
        }
    }

    void MfdSystem::press(int side, int button)
    {
        MfdUnit &u = unit(side);
        u.flash[std::clamp(button, 0, 15)] = 0.3f;
        if (button == 15)
        {
            u.closed = true;
            return;
        }
        if (button == 12)
        {
            u.power = !u.power;
            return;
        }
        if (!u.power || !bus_power)
            return;
        if (button == 13 || button == 14)
        {
            u.selecting = !u.selecting;
            u.select_scroll = 0;
            return;
        }
        if (u.selecting)
        {
            // In the mode list the side buttons pick the n-th mode.
            if (button >= 12)
                return;
            const int n = select_slot(u, button);
            if (n >= 0)
                set_mode(side, visible_mode(n));
            else if (button == 5)
                u.select_scroll = std::max(0, u.select_scroll - 10);
            else if (u.select_scroll + 10 < visible_count())
                u.select_scroll += 10;
            return;
        }
        if (u.mode < 0)
            return;
        sol::protected_function fn = modes_[static_cast<std::size_t>(u.mode)].table["button"];
        if (!fn.valid())
            return;
        lua_->execute(""); // restarts the instruction budget, which direct calls into Lua do not do
        sol::protected_function_result r = fn(u.self, button + 1);
        if (!r.valid())
        {
            sol::error e = r;
            error_ = e.what();
        }
    }

    void MfdSystem::update(double dt)
    {
        for (MfdUnit &u : units_)
        {
            for (float &f : u.flash)
                f = std::max(0.0f, f - static_cast<float>(dt));
            if (!u.power || !bus_power || u.mode < 0 || u.selecting)
                continue;
            sol::protected_function fn = modes_[static_cast<std::size_t>(u.mode)].table["update"];
            if (!fn.valid())
                continue;
            lua_->execute(""); // restarts the instruction budget, which direct calls into Lua do not do
            sol::protected_function_result r = fn(u.self, dt);
            if (!r.valid())
            {
                sol::error e = r;
                error_ = e.what();
            }
        }
    }

    void MfdSystem::render(const MfdCanvas &canvas, const MfdLayout &layout)
    {
        const float s = layout.size / mfd_size;
        const auto X = [&](float v) { return layout.x + v * s; };
        const auto Y = [&](float v) { return layout.y + v * s; };
        sl::Font *font = font_for(static_cast<int>(std::round(11.0f * s * 1.2f)));
        for (const MfdCommand &c : canvas.commands)
        {
            switch (c.kind)
            {
            case MfdCommand::Kind::line:
                sl::line(sl::screen, X(c.a), Y(c.b), X(c.c), Y(c.d), c.colour);
                break;
            case MfdCommand::Kind::rect:
                sl::rect(sl::screen, X(c.a), Y(c.b), X(c.c), Y(c.d), c.colour);
                break;
            case MfdCommand::Kind::fillrect:
                sl::rectfill(sl::screen, X(c.a), Y(c.b), X(c.c), Y(c.d), c.colour);
                break;
            case MfdCommand::Kind::circle:
                sl::circle(sl::screen, X(c.a), Y(c.b), c.c * s, c.colour);
                break;
            case MfdCommand::Kind::disc:
                sl::circlefill(sl::screen, X(c.a), Y(c.b), c.c * s, c.colour);
                break;
            case MfdCommand::Kind::tri:
                sl::trianglefill(sl::screen, X(c.a), Y(c.b), X(c.c), Y(c.d), X(c.e), Y(c.f), c.colour);
                break;
            case MfdCommand::Kind::image:
                if (sl::Bitmap *bmp = bitmap(c.text))
                {
                    const int sw = c.src[2] > 0 ? c.src[2] : bmp->width, sh = c.src[3] > 0 ? c.src[3] : bmp->height;
                    const float w = c.c * s, h = c.d * s;
                    sl::draw_sprite_region_rotated(bmp, c.src[0], c.src[1], sw, sh, X(c.a) - w / 2, Y(c.b) - h / 2, w, h, w / 2, h / 2, c.e);
                }
                break;
            case MfdCommand::Kind::text:
            {
                int x = static_cast<int>(X(c.a));
                const int w = sl::text_length(font, c.text);
                if (c.align == 1)
                    x -= w / 2;
                else if (c.align == 2)
                    x -= w;
                sl::gprintf(x, static_cast<int>(Y(c.b)) - sl::text_height(font) / 2, font, c.colour, "%s", c.text.c_str());
                break;
            }
            }
        }
    }

    int MfdSystem::select_slot(const MfdUnit &unit, int button) const
    {
        const int count = visible_count();
        int n = button;
        if (count > 12)
        {
            if (button == 5 || button == 11)
                return -1;
            n = button < 5 ? button : button - 1;
        }
        n += unit.select_scroll;
        return n < count ? n : -2;
    }

    void MfdSystem::draw_select(MfdUnit &unit, MfdCanvas &g, MfdCanvas &labels)
    {
        g.set_colour(255, 240, 140);
        g.text(200, 20, "SELECT MODE", 1);
        labels.set_colour(120, 255, 140);
        const int count = visible_count();
        for (int i = 0; i < 12; ++i)
        {
            const float y = (i % 6 + 0.5f) * mfd_size / 6.0f;
            const int n = select_slot(unit, i);
            std::string name;
            if (n == -1)
                name = i == 5 ? "< PREV" : "NEXT >";
            else if (n >= 0)
                name = modes_[static_cast<std::size_t>(visible_mode(n))].name;
            if (!name.empty())
                labels.text(i < 6 ? 4.0f : mfd_size - 4.0f, y, name, i < 6 ? 0 : 2);
        }
        if (count > 12)
        {
            g.set_colour(150, 160, 150);
            g.text(200, 380, "page " + std::to_string(unit.select_scroll / 10 + 1) + "/" + std::to_string((count + 9) / 10), 1);
        }
    }

    MfdLayout MfdSystem::content_layout(const MfdLayout &layout) const
    {
        if (!layout.hud)
            return layout;
        MfdLayout inner = layout;
        const float margin = 50.0f * layout.size / mfd_size;
        inner.x += margin;
        inner.y += margin;
        inner.size -= 2.0f * margin;
        return inner;
    }

    void MfdSystem::draw_frame(const MfdLayout &layout, const MfdUnit &unit, const MfdMode *)
    {
        const float s = layout.size / mfd_size;
        const float bw = 22.0f * s, bh = 18.0f * s;
        const float l = layout.x, t = layout.y, r = layout.x + layout.size, b = layout.y + layout.size;
        const float top = layout.no_buttons ? 10.0f * s : 26.0f * s;
        sl::rectfill(sl::screen, l - bw - 14 * s, t - top, r + bw + 14 * s, b + bh + 24 * s, {22, 24, 30, 255});
        sl::rectfill(sl::screen, l, t, r, b, {2, 6, 4, 255});
        sl::rect(sl::screen, l - 1, t - 1, r + 1, b + 1, {80, 90, 100, 255}, 2.0f);
        if (layout.no_buttons)
            return;
        {
            float cx, cy, cw, ch;
            close_box(layout, cx, cy, cw, ch);
            const float f = std::clamp(unit.flash[15] / 0.3f, 0.0f, 1.0f);
            sl::rectfill(sl::screen, cx, cy, cx + cw, cy + ch, {static_cast<Uint8>(70 + 120 * f), 74, 84, 255});
            draw_cross(cx, cy, cw, ch, s);
        }
        for (int i = 0; i < 12; ++i)
        {
            const float y = t + (i % 6 + 0.5f) * layout.size / 6.0f;
            const float x = i < 6 ? l - bw - 4 * s : r + 4 * s;
            sl::rectfill(sl::screen, x, y - bh / 2, x + bw, y + bh / 2, {70, 74, 84, 255});
        }
        for (int i = 0; i < 3; ++i)
        {
            const float x = l + (i + 0.5f) * layout.size / 3.0f - bw;
            const float y = b + 8 * s;
            const bool lit = i == 0 && unit.power && bus_power;
            sl::rectfill(sl::screen, x, y, x + 2 * bw, y + bh, lit ? sl::Colour{60, 150, 70, 255} : sl::Colour{70, 74, 84, 255});
            sl::gprintf(static_cast<int>(x + 6 * s), static_cast<int>(y + 2 * s), font_for(static_cast<int>(11 * s * 1.2f)), {230, 230, 230}, "%s", button_names[i]);
        }
    }

    void MfdSystem::draw(int side, const MfdLayout &layout, std::vector<float> *buttons)
    {
        // MFD text is drawn straight onto its screen, so the HUD's backing panels must not apply.
        const sl::TextPanelStyle saved = sl::text_panel_style();
        sl::TextPanelStyle off = saved;
        off.enabled = false;
        sl::set_text_panel_style(off);
        draw_unit(side, layout, buttons);
        sl::set_text_panel_style(saved);
    }

    void MfdSystem::draw_to_target(int side, sl::Bitmap *target, const MfdLayout &layout)
    {
        if (!sl::begin_render_target(target))
            return;
        sl::clear_render_target({18, 20, 26, 255});
        draw(side, layout);
        sl::end_render_target();
    }

    void MfdSystem::draw_unit(int side, const MfdLayout &layout, std::vector<float> *buttons)
    {
        MfdUnit &u = unit(side);
        if (layout.bare)
        {
            const float s = layout.size / mfd_size;
            sl::rectfill(sl::screen, layout.x, layout.y, layout.x + layout.size, layout.y + layout.size, {10, 12, 16, 255});
            sl::rect(sl::screen, layout.x, layout.y, layout.x + layout.size, layout.y + layout.size, {70, 78, 90, 255}, std::max(1.0f, 2.0f * s));
        }
        else if (!layout.hud)
            draw_frame(layout, u, nullptr);
        if (buttons)
            buttons->clear();
        if (!u.power || !bus_power)
            return;
        MfdCanvas g, lab;
        g.set_colour(120, 255, 140);
        const MfdMode *mode = u.mode >= 0 ? &modes_[static_cast<std::size_t>(u.mode)] : nullptr;
        if (mode)
            g.directory = mode->directory;
        if (u.selecting || !mode)
            draw_select(u, g, lab);
        else
        {
            sol::protected_function fn = mode->table["draw"];
            if (fn.valid())
            {
                lua_->execute(""); // restarts the instruction budget, which direct calls into Lua do not do
                sol::protected_function_result r = fn(u.self, std::ref(g), mfd_size, mfd_size, layout.bare);
                if (!r.valid())
                {
                    sol::error e = r;
                    error_ = e.what();
                }
            }
            if (sol::optional<sol::table> names = mode->table["buttons"])
            {
                lab.set_colour(200, 210, 220);
                for (int i = 0; i < 12; ++i)
                {
                    const std::string label = names->get_or<std::string>(i + 1, "");
                    if (label.empty())
                        continue;
                    const float y = (i % 6 + 0.5f) * mfd_size / 6.0f;
                    lab.text(i < 6 ? 4.0f : mfd_size - 4.0f, y, label, i < 6 ? 0 : 2);
                }
            }
        }
        render(g, content_layout(layout));
        render(lab, layout);
        if (layout.hud)
            draw_hud_buttons(u, layout);
    }

    // The see-through display has no bezel, so each labelled button gets a green box that flashes red when pressed.
    void MfdSystem::draw_hud_buttons(const MfdUnit &u, const MfdLayout &layout) const
    {
        const float s = layout.size / mfd_size;
        for (int i = 0; i < 12; ++i)
        {
            if (!hud_button_active(u, i))
                continue;
            float x, y, w, h;
            hud_button_box(layout, i, x, y, w, h);
            const float f = std::clamp(u.flash[i] / 0.3f, 0.0f, 1.0f);
            const sl::Colour edge{static_cast<Uint8>(70 + 185 * f), static_cast<Uint8>(230 - 170 * f), static_cast<Uint8>(110 - 50 * f), 255};
            if (f > 0.0f)
                sl::rectfill(sl::screen, x, y, x + w, y + h, {255, 60, 40, static_cast<Uint8>(110 * f)});
            sl::rect(sl::screen, x, y, x + w, y + h, edge, std::max(1.0f, 1.5f * s));
        }
        // Menu (top left) and close (top right) sit in the margin above the content.
        float x, y, w, h;
        const sl::Colour edge{70, 230, 110, 255};
        if (u.power && bus_power)
        {
            hud_menu_box(layout, x, y, w, h);
            const float f = std::clamp(u.flash[14] / 0.3f, 0.0f, 1.0f);
            if (f > 0.0f)
                sl::rectfill(sl::screen, x, y, x + w, y + h, {255, 60, 40, static_cast<Uint8>(110 * f)});
            sl::rect(sl::screen, x, y, x + w, y + h, edge, std::max(1.0f, 1.5f * s));
            sl::gprintf(static_cast<int>(x + 6 * s), static_cast<int>(y + h / 2 - sl::text_height(font_for(static_cast<int>(11 * s * 1.2f))) / 2), font_for(static_cast<int>(11 * s * 1.2f)), {120, 255, 140}, "MNU");
        }
        close_box(layout, x, y, w, h);
        sl::rect(sl::screen, x, y, x + w, y + h, edge, std::max(1.0f, 1.5f * s));
        draw_cross(x, y, w, h, s);
    }

    void MfdSystem::hud_menu_box(const MfdLayout &layout, float &x, float &y, float &w, float &h) const
    {
        const float s = layout.size / mfd_size;
        w = 50.0f * s;
        h = 26.0f * s;
        x = layout.x;
        y = layout.y + 8.0f * s;
    }

    // The close button: a small square at the top right (in the bezel above a framed display, in the margin of a see-through one).
    void MfdSystem::close_box(const MfdLayout &layout, float &x, float &y, float &w, float &h) const
    {
        const float s = layout.size / mfd_size;
        w = 26.0f * s;
        h = 22.0f * s;
        x = layout.x + layout.size - w;
        y = layout.hud ? layout.y + 8.0f * s : layout.y - 24.0f * s;
    }

    void MfdSystem::draw_cross(float x, float y, float w, float h, float s) const
    {
        const float m = 6.0f * s;
        const sl::Colour c{235, 235, 235, 255};
        sl::line(sl::screen, x + m, y + m, x + w - m, y + h - m, c, std::max(1.0f, 1.5f * s));
        sl::line(sl::screen, x + m, y + h - m, x + w - m, y + m, c, std::max(1.0f, 1.5f * s));
    }

    bool MfdSystem::hud_button_active(const MfdUnit &u, int i) const
    {
        if (!u.power || !bus_power)
            return false;
        if (u.selecting || u.mode < 0)
            return true;
        sol::optional<sol::table> labels = modes_[static_cast<std::size_t>(u.mode)].table["buttons"];
        return labels && !labels->get_or<std::string>(i + 1, "").empty();
    }

    void MfdSystem::hud_button_box(const MfdLayout &layout, int i, float &x, float &y, float &w, float &h) const
    {
        const float s = layout.size / mfd_size;
        w = 50.0f * s;
        h = layout.size / 6.0f * 0.6f;
        x = i < 6 ? layout.x : layout.x + layout.size - w;
        y = layout.y + (i % 6 + 0.5f) * layout.size / 6.0f - h / 2;
    }

    int MfdSystem::hit_test(int side, const MfdLayout &layout, float px, float py) const
    {
        float cx, cy, cw, ch;
        if (layout.hud || !(layout.bare || layout.no_buttons))
        {
            close_box(layout, cx, cy, cw, ch);
            if (px >= cx && px <= cx + cw && py >= cy && py <= cy + ch)
                return 15;
        }
        if (layout.hud)
        {
            const MfdUnit &mu = units_[static_cast<std::size_t>(side)];
            if (mu.power && bus_power)
            {
                hud_menu_box(layout, cx, cy, cw, ch);
                if (px >= cx && px <= cx + cw && py >= cy && py <= cy + ch)
                    return 14;
            }
            for (int i = 0; i < 12; ++i)
            {
                float x, y, w, h;
                hud_button_box(layout, i, x, y, w, h);
                if (hud_button_active(units_[static_cast<std::size_t>(side)], i) && px >= x && px <= x + w && py >= y && py <= y + h)
                    return i;
            }
            return -1;
        }
        if (layout.bare || layout.no_buttons)
            return -1;
        const float s = layout.size / mfd_size;
        const float bw = 22.0f * s, bh = 18.0f * s;
        const float l = layout.x, t = layout.y, r = layout.x + layout.size, b = layout.y + layout.size;
        for (int i = 0; i < 12; ++i)
        {
            const float y = t + (i % 6 + 0.5f) * layout.size / 6.0f;
            const float x = i < 6 ? l - bw - 4 * s : r + 4 * s;
            if (px >= x && px <= x + bw && py >= y - bh / 2 && py <= y + bh / 2)
                return i;
        }
        for (int i = 0; i < 3; ++i)
        {
            const float x = l + (i + 0.5f) * layout.size / 3.0f - bw;
            const float y = b + 8 * s;
            if (px >= x && px <= x + 2 * bw && py >= y && py <= y + bh)
                return 12 + i;
        }
        return -1;
    }

    bool MfdSystem::handle_mouse(const MfdLayout &left, const MfdLayout &right, bool clicked, int mx, int my)
    {
        if (!clicked)
            return false;
        const MfdLayout *layouts[2] = {&left, &right};
        for (int side = 0; side < 2; ++side)
        {
            const int hit = hit_test(side, *layouts[side], static_cast<float>(mx), static_cast<float>(my));
            if (hit >= 0)
            {
                press(side, hit);
                return true;
            }
        }
        return false;
    }
} // namespace kosmos
