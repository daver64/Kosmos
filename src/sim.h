#pragma once

#include "base.h"
#include "galaxy.h"
#include "lua_runtime.h"
#include "mfd.h"
#include "nav.h"
#include "scenario.h"
#include "ship.h"
#include "universe.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace kosmos
{
    /** A folder under addons/ with an addon.json manifest, ships and scripts. */
    struct Addon
    {
        std::string id, name, version, directory;
        std::vector<ShipSpec> ships;
    };

    /**
     * The simulation: a universe, its ships, the property tree that every system reads and writes, and the Lua runtime
     * that scripts use the same tree through.
     */
    class Sim
    {
    public:
        Sim();

        /** Time warp factors selectable with /sim/warp-index. Above max_physics_warp ships must be on rails. */
        static constexpr int warp_levels = 11;
        static double warp_factor(int index);
        static constexpr double max_physics_warp = 4.0;

        /** `system` is a Galaxy index: 0 loads data/sol.json, anything else is generated. */
        bool init(const std::string &root, std::string &error, int system = 0);
        int system_index() const { return system_; }
        /** Replace the universe with another galaxy star's system and drop every ship. Visual state tied to the old bodies must be rebuilt by the caller. */
        bool change_system(int system, std::string &error);
        /** Create a ship from a spec and publish it as /ships/ship[n]. */
        Ship *spawn(const ShipSpec &spec);
        void clear_ships();
        void activate(int index);
        Ship *active_ship();
        int ship_count() const { return static_cast<int>(ships_.size()); }
        Ship *ship(int index) { return ships_[static_cast<std::size_t>(index)].get(); }
        const ShipSpec *find_ship_spec(const std::string &name) const;

        /** Advance by one frame of real time, applying the time warp. */
        void step(double frame_seconds);
        /** Warp factor in effect after the last step. */
        double current_warp() const { return current_warp_; }
        const std::vector<std::string> &messages() const { return messages_; }
        const std::vector<Addon> &addons() const { return addons_; }
        const std::string &root() const { return root_; }
        const std::vector<Scenario> &scenarios() const { return scenarios_; }
        /** Match a scenario by id (file name without .json), case-insensitively, or by full name. */
        const Scenario *find_scenario(const std::string &name) const;

        /** Appends to the HUD message list and stderr; also forwarded to log_sink (the Lua console). */
        void log(const std::string &text);
        std::function<void(const std::string &)> log_sink;

        /** Per-user folder (~/.config/kosmos): user scenarios/ and bases/ live here and override shipped ones with the same id. */
        static std::string user_dir();
        /** Re-read the user's scenarios and bases, replacing entries with the same id. Bases are only added or replaced when not yet loaded. */
        void load_user_content();

        Galaxy galaxy;
        Universe universe;
        sl::PropertyTree props;
        sl::LuaRuntime lua;
        MfdSystem mfd;
        NavSystem nav;
        /** Built-up surface sites from data/bases/ and addons; their heights are filled in when the terrain is made. */
        std::vector<Base> bases;

    private:
        void scan_addons();
        void publish_galaxy();
        Controls read_controls();
        /** Publish /target/* relative to the ship named by /sim/target (a ship or a body). */
        void update_target();

        std::string root_;
        std::vector<Addon> addons_;
        std::vector<Scenario> scenarios_;
        std::vector<std::unique_ptr<Ship>> ships_;
        int system_ = 0;
        int active_ = 0;
        double current_warp_ = 1.0;
        bool script_loaded_ = false;
        std::vector<std::string> messages_;
    };
} // namespace kosmos
