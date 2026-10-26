#pragma once

#include "draw.h"
#include "lua_runtime.h"
#include "proptree.h"

#include <sol/sol.hpp>

#include <memory>
#include <string>
#include <vector>

namespace kosmos
{
    /** Logical size of an MFD screen; mode scripts draw in these units and the renderer scales them. */
    constexpr int mfd_size = 400;

    /** One recorded drawing command from a mode script. */
    struct MfdCommand
    {
        enum class Kind { line, rect, fillrect, circle, disc, tri, text } kind = Kind::line;
        float a = 0, b = 0, c = 0, d = 0, e = 0, f = 0;
        sl::Colour colour{255, 255, 255, 255};
        std::string text;
        int align = 0; // 0 left, 1 centre, 2 right
    };

    /** The drawing surface handed to a mode script's draw(self, g). Records commands in mfd_size units. */
    struct MfdCanvas
    {
        std::vector<MfdCommand> commands;
        sl::Colour colour{120, 255, 140, 255};
        void set_colour(int r, int g, int b) { colour = {static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b), 255}; }
        void push(MfdCommand::Kind k, float a, float b, float c, float d, float e = 0, float f = 0);
        void text(float x, float y, const std::string &s, sol::optional<int> align);
        void arc(float cx, float cy, float r, float a0, float a1);
    };

    /** A mode registered by an addon script: its Lua table and the folder it came from. */
    struct MfdMode
    {
        std::string id, name, addon;
        sol::table table;
    };

    /** One physical MFD (left or right): a selected mode with its own state table, power and button grid. */
    struct MfdUnit
    {
        bool power = true;
        bool selecting = false;
        int mode = -1;
        sol::table self;
        int select_scroll = 0;
    };

    /** Where an MFD is drawn on screen and how the mouse hits its buttons. */
    struct MfdLayout
    {
        float x = 0, y = 0, size = 400;
        bool hud = false; // no background or frame, shapes and text only
    };

    class MfdSystem
    {
    public:
        MfdSystem() = default;
        void init(sl::LuaRuntime &lua, sl::PropertyTree &props);
        /** Load a mode script from an addon folder; returns false and sets error on failure. */
        bool register_mode(const std::string &addon, const std::string &id, const std::string &path, std::string &error);
        const std::vector<MfdMode> &modes() const { return modes_; }
        int mode_by_id(const std::string &id) const;

        MfdUnit &unit(int side) { return units_[side]; }
        void set_mode(int side, int mode);
        /** Press button 0..11 (left column top to bottom, then right column), 12 PWR, 13 SEL, 14 MNU. */
        void press(int side, int button);
        /** Run each powered unit's update(self, dt). */
        void update(double dt);
        /** Draw one unit; returns the screen rectangle of each button in `buttons` (15 entries x,y,w,h). */
        void draw(int side, const MfdLayout &layout, std::vector<float> *buttons = nullptr);
        /** Draw the unit with its frame into an offscreen target (for 3D cockpit screens). */
        void draw_to_target(int side, sl::Bitmap *target, const MfdLayout &layout);
        /** Return the button under a screen point, or -1. */
        int hit_test(int side, const MfdLayout &layout, float px, float py) const;
        /** Draw the stock MFD pair as 2D panels on the screen bottom. Returns true if the mouse was used. */
        bool handle_mouse(const MfdLayout &left, const MfdLayout &right, bool clicked, int mx, int my);
        /** False when the active ship's electrical bus is browned out: every MFD acts as if switched off. */
        bool bus_power = true;
        const std::string &last_error() const { return error_; }

    private:
        void draw_unit(int side, const MfdLayout &layout, std::vector<float> *buttons);
        void render(const MfdCanvas &canvas, const MfdLayout &layout);
        void draw_select(MfdUnit &unit, MfdCanvas &g);
        void draw_frame(const MfdLayout &layout, const MfdUnit &unit, const MfdMode *mode);

        sl::LuaRuntime *lua_ = nullptr;
        std::vector<MfdMode> modes_;
        MfdUnit units_[2];
        std::string error_;
    };
} // namespace kosmos
