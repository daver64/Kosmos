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
        return !((domain_ == "atmosphere" && d == "space") || (domain_ == "space" && d == "atmosphere"));
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
            if (units_[i].mode >= 0 && !allowed(units_[i].mode))
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
        if (mode < 0 || mode >= static_cast<int>(modes_.size()) || !allowed(mode))
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
            return;
        }
        if (u.selecting)
        {
            // In the mode list the side buttons pick the n-th mode.
            if (button < 12)
                set_mode(side, visible_mode(u.select_scroll + button));
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

    void MfdSystem::draw_select(MfdUnit &unit, MfdCanvas &g)
    {
        g.set_colour(255, 240, 140);
        g.text(200, 20, "SELECT MODE", 1);
        g.set_colour(120, 255, 140);
        const int count = visible_count();
        for (int i = 0; i < 12 && unit.select_scroll + i < count; ++i)
        {
            const float y = (i % 6 + 0.5f) * mfd_size / 6.0f;
            const MfdMode &m = modes_[static_cast<std::size_t>(visible_mode(unit.select_scroll + i))];
            g.text(i < 6 ? 14.0f : 386.0f, y, m.name, i < 6 ? 0 : 2);
        }
    }

    void MfdSystem::draw_frame(const MfdLayout &layout, const MfdUnit &unit, const MfdMode *)
    {
        const float s = layout.size / mfd_size;
        const float bw = 22.0f * s, bh = 18.0f * s;
        const float l = layout.x, t = layout.y, r = layout.x + layout.size, b = layout.y + layout.size;
        sl::rectfill(sl::screen, l - bw - 14 * s, t - 10 * s, r + bw + 14 * s, b + bh + 24 * s, {22, 24, 30, 255});
        sl::rectfill(sl::screen, l, t, r, b, {2, 6, 4, 255});
        sl::rect(sl::screen, l - 1, t - 1, r + 1, b + 1, {80, 90, 100, 255}, 2.0f);
        if (layout.no_buttons)
            return;
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
        MfdCanvas g;
        g.set_colour(120, 255, 140);
        const MfdMode *mode = u.mode >= 0 ? &modes_[static_cast<std::size_t>(u.mode)] : nullptr;
        if (mode)
            g.directory = mode->directory;
        if (u.selecting || !mode)
            draw_select(u, g);
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
            if (sol::optional<sol::table> labels = mode->table["buttons"])
            {
                g.set_colour(200, 210, 220);
                for (int i = 0; i < 12; ++i)
                {
                    const std::string label = labels->get_or<std::string>(i + 1, "");
                    if (label.empty())
                        continue;
                    const float y = (i % 6 + 0.5f) * mfd_size / 6.0f;
                    g.text(i < 6 ? 4.0f : mfd_size - 4.0f, y, label, i < 6 ? 0 : 2);
                }
            }
        }
        render(g, layout);
    }

    int MfdSystem::hit_test(int, const MfdLayout &layout, float px, float py) const
    {
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
