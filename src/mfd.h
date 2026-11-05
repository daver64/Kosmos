#pragma once

#include "draw.h"
#include "lua_runtime.h"
#include "proptree.h"

#include <sol/sol.hpp>

#include <functional>
#include <map>
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
        enum class Kind { line, rect, fillrect, circle, disc, tri, text, image } kind = Kind::line;
        float a = 0, b = 0, c = 0, d = 0, e = 0, f = 0;
        sl::Colour colour{255, 255, 255, 255};
        std::string text;
        int align = 0; // 0 left, 1 centre, 2 right
        /** Image commands: the source region in pixels (all zero for the whole bitmap). The path is in `text`, centre and size in a..d, angle in e. */
        int src[4] = {0, 0, 0, 0};
    };

    /** The drawing surface handed to a mode script's draw(self, g). Records commands in mfd_size units. */
    struct MfdCanvas
    {
        std::vector<MfdCommand> commands;
        sl::Colour colour{120, 255, 140, 255};
        /** The folder of the mode being drawn; image() names are relative to it. */
        std::string directory;
        void image(const std::string &file, float cx, float cy, float w, float h, sol::optional<float> angle, sol::optional<int> sx, sol::optional<int> sy, sol::optional<int> sw, sol::optional<int> sh);
        void set_colour(int r, int g, int b) { colour = {static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b), 255}; }
        void push(MfdCommand::Kind k, float a, float b, float c, float d, float e = 0, float f = 0);
        void text(float x, float y, const std::string &s, sol::optional<int> align);
        void arc(float cx, float cy, float r, float a0, float a1);
    };

    /** A mode registered by an addon script: its Lua table and the folder it came from. */
    struct MfdMode
    {
        std::string id, name, addon, directory;
        /** Where the mode makes sense: "any", "atmosphere" (aircraft instruments) or "space" (orbital instruments). */
        std::string domain = "any";
        /** A short button label for the see-through displays, and the ship addon folders that offer the mode (empty: every ship). */
        std::string short_name;
        std::vector<std::string> ships;
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
        bool closed = false; // the close button was pressed; the app hides the display
        float flash[16]{}; // 0-14 the bezel buttons, 15 close // seconds left of the press highlight on each button
    };

    /** Where an MFD is drawn on screen and how the mouse hits its buttons. */
    struct MfdLayout
    {
        float x = 0, y = 0, size = 400;
        bool hud = false; // no background or frame, shapes and text only; the side buttons are the outlined labels
        bool no_buttons = false; // the bezel buttons are on a 3D model instead: none drawn or hit-tested
        bool bare = false; // a panel gauge: a plain backing square instead of the MFD frame and buttons
        std::vector<float> button_y; // screen y of each button's centre when the artwork spaces them unevenly; empty means six even rows
    };

    class MfdSystem
    {
    public:
        MfdSystem() = default;
        void init(sl::LuaRuntime &lua, sl::PropertyTree &props);
        /** Load a mode script from an addon folder; returns false and sets error on failure. */
        bool register_mode(const std::string &addon, const std::string &id, const std::string &path, std::string &error, const std::string &domain = "any", const std::vector<std::string> &ships = {}, const std::string &short_name = "");
        /** Adds a page drawn by C++ code (a native addon's) instead of a Lua script. Image names are relative to @p directory. */
        void register_native(const std::string &addon, const std::string &id, const std::string &name, const std::string &directory, std::function<void(MfdCanvas &)> draw);
        const std::vector<MfdMode> &modes() const { return modes_; }
        int mode_by_id(const std::string &id) const;
        /** The flown ship's regime: "atmosphere" hides the "space" modes, "space" hides the "atmosphere" ones, anything else hides none. */
        void set_domain(const std::string &domain);
        /** Mode ids the current ship does not offer, on top of the domain filter. */
        void set_hidden(const std::vector<std::string> &ids);
        /** The addon folder name of the flown ship; modes restricted to certain ships are only offered on those. */
        void set_ship(const std::string &folder) { ship_folder_ = folder; }
        /** Whether a mode can be chosen by the player: the domain filter and the hidden list both apply. */
        bool allowed(int mode) const;
        /** Whether a mode can be shown at all (a panel can place a hidden mode on a fixed instrument). */
        bool domain_allowed(int mode) const;
        bool domain_allowed(const std::string &id) const;
        bool allowed(const std::string &id) const;

        /** Units are numbered from 0; the stock display uses 0 and 1, and a cockpit panel can add more. */
        void set_unit_count(int count);
        int unit_count() const { return static_cast<int>(units_.size()); }
        MfdUnit &unit(int side) { return units_[static_cast<std::size_t>(side)]; }
        void set_mode(int side, int mode);
        /** Press button 0..11 (left column top to bottom, then right column), 12 PWR, 13 SEL, 14 MNU, 15 close. */
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
        /** A cached bitmap from disk, or null. */
        sl::Bitmap *bitmap(const std::string &path);
        /** Registers a bitmap drawn by image("live:<name>"): a picture the game renders (the Instrument MFD's camera). The caller keeps ownership. */
        void set_live_bitmap(const std::string &name, sl::Bitmap *bitmap) { live_bitmaps_[name] = bitmap; }

    private:
        void draw_hud_buttons(const MfdUnit &u, const MfdLayout &layout) const;
        void hud_menu_box(const MfdLayout &layout, float &x, float &y, float &w, float &h) const;
        void close_box(const MfdLayout &layout, float &x, float &y, float &w, float &h) const;
        void draw_cross(float x, float y, float w, float h, float s) const;
        bool hud_button_active(const MfdUnit &u, int i) const;
        void hud_button_box(const MfdLayout &layout, int i, float &x, float &y, float &w, float &h) const;
        void draw_unit(int side, const MfdLayout &layout, std::vector<float> *buttons);
        void render(const MfdCanvas &canvas, const MfdLayout &layout);
        void draw_select(MfdUnit &unit, MfdCanvas &g, MfdCanvas &labels, const MfdLayout &layout);
        /** The mode-list index a side button picks, -1 for the page buttons (shown when more than 12 modes are offered). */
        int select_slot(const MfdUnit &unit, int button) const;
        /** The square a mode draws into: the whole layout, or inside the button margins of a see-through display. */
        MfdLayout content_layout(const MfdLayout &layout) const;
        void draw_frame(const MfdLayout &layout, const MfdUnit &unit, const MfdMode *mode);
        int visible_count() const;
        /** The n-th mode the current domain allows, as an index into modes_ (-1 when out of range). */
        int visible_mode(int n) const;
        std::map<std::string, sl::Bitmap *> bitmaps_;
        std::map<std::string, sl::Bitmap *> live_bitmaps_;

        sl::LuaRuntime *lua_ = nullptr;
        std::vector<MfdMode> modes_;
        std::string domain_ = "any";
        std::vector<std::string> hidden_;
        std::string ship_folder_;
        std::vector<MfdUnit> units_ = std::vector<MfdUnit>(2);
        std::string error_;
    };
} // namespace kosmos
