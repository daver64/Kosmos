#pragma once

#include "base.h"
#include "equipment.h"
#include "weapon_specs.h"
#include "galaxy.h"
#include "lua_runtime.h"
#include "mfd.h"
#include "nav.h"
#include "weather_system.h"
#include "landing_forecast.h"
#include "scenario.h"
#include "ship.h"
#include "universe.h"

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace kosmos
{
    /** Parses a UTC ignition date or MJD, rejecting invalid civil dates and times. */
    bool parse_burn_date(const std::string &text, double &jd);

    /** A folder under addons/ with an addon.json manifest, ships and scripts. */
    struct Addon
    {
        std::string id, name, version, directory;
        std::vector<ShipSpec> ships;
        /** Fixed buildings the addon offers to bases (the "buildings" list of addon.json); not flyable. */
        std::vector<ShipSpec> buildings;
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
        // Physics step at warp 4x and below, and the most steps one frame may catch up before the simulation runs slow instead.
        static constexpr double fixed_step = 1.0 / 240.0;
        static constexpr int max_steps_per_frame = 24;
        static double warp_factor(int index);
        static constexpr double max_physics_warp = 4.0;
        /** Highest warp while engines fire in vacuum (physics keeps running, with coarser steps). */
        static constexpr double thrust_warp = 50.0;
        /** Highest warp while every ship rests motionless on the ground (physics keeps running). */
        static constexpr double ground_warp = 100000.0;

        /** `system` is a Galaxy index: 0 loads data/sol.json, anything else is generated. */
        bool init(const std::string &root, std::string &error, int system = 0);
        /** Julian date (UTC) systems start on; 0 keeps each system file's epoch (J2000). */
        double start_epoch_jd = 0.0;
        /** What failing life support does to new ships' crews; a scenario's own setting or --life-support replaces it. */
        LifeSupportMode life_support = LifeSupportMode::warn;
        int system_index() const { return system_; }
        /** Replace the universe with another galaxy star's system and drop every ship. Visual state tied to the old bodies must be rebuilt by the caller. */
        bool change_system(int system, std::string &error);
        /** Create a ship from a spec and publish it as /ships/ship[n]. */
        Ship *spawn(const ShipSpec &spec);
        void clear_ships();
        /** Deletes ship @p index (never the flown one); later ships move down one place, so handles to them change. Returns false when it may not be removed. */
        bool remove_ship(int index);
        int active_index() const { return active_; }

        /** A ship a native program owns: it supplies the controls (alone, or beside the player's when passthrough) and may push on the ship. */
        struct LinkClaim
        {
            int owner = 0;
            std::string addon;
            bool passthrough = false;
            double force[3] = {0, 0, 0}, torque[3] = {0, 0, 0};
        };
        std::map<int, LinkClaim> link_claims;
        void activate(int index);
        /** Refresh target and radio/docking instruments without advancing the simulation. */
        void refresh_navigation();
        /** Stop the landing controller and release its hover throttle. */
        void cancel_autoland(const std::string &reason);
        bool arm_timed_burn(double delay, double duration, double throttle, const std::string &engine, const std::string &sas);
        void cancel_timed_burn(const std::string &reason);
        void cancel_vessel_ascent(const std::string &reason);
        bool vessel_ascent_active() const { return ascent_ship_ >= 0; }
        void release_payload_on_stage();
        /** Consume a pending staging handoff between the displayed and active ships. */
        bool consume_stage_handoff(const Ship *from, const Ship *to);
        Ship *active_ship();
        int ship_count() const { return static_cast<int>(ships_.size()); }
        Ship *ship(int index) { return ships_[static_cast<std::size_t>(index)].get(); }
        const ShipSpec *find_ship_spec(const std::string &name) const;
        /** A building by its name; searches every addon. */
        const ShipSpec *find_building_spec(const std::string &name) const;

        /** Advance by one frame of real time, applying the time warp. */
        void step(double frame_seconds);
        void update_airlock(double frame);
        /** Warp factor in effect after the last step. */
        /** Runs `frame` seconds of wall time as constant physics steps and returns how many ran. Above 4x warp, or paused, it is a single step of the frame. */
        int advance(double frame);
        double current_warp() const { return current_warp_; }

        /** A burn the player could plan for, as the Transfer or OrbitComp MFD (when showing it) or the built-in Hohmann estimate gives it. */
        struct PlannedBurn
        {
            bool valid = false;
            /** Seconds from now to ignition (the burn starts half its length before its middle). */
            double ignition_in = 0.0;
            /** Where it came from: "Transfer MFD", "OrbitComp MFD" or "Hohmann estimate". */
            std::string source;
            /** The reason there is none, when !valid. */
            std::string why;
        };
        PlannedBurn planned_burn();

        /** One Hohmann departure window from the flown ship to the target. */
        struct TransferWindow
        {
            double ignition_in = 0.0, burn_length = 0.0, dv1 = 0.0, flight_time = 0.0;
        };
        /** The next `count` windows with ignition still ahead; empty (and `why` set) when there is no target or no phasing to wait for. */
        std::vector<TransferWindow> transfer_windows(int count, std::string &why);
        /**
         * Moves the clock forward by `seconds` at once. Every ship goes on rails for the jump, so none may be landed, in the air or
         * burning; false (with `why`) when the jump is refused. Nothing but position and date changes: consumables, systems, mission time and nav timers are untouched.
         */
        bool skip_time(double seconds, std::string &why);
        /**
         * Skips to `burn_lead_seconds()` before an ignition `ignition_in` seconds ahead. With `follow`, the plan the MFD shows is
         * re-read a few frames after each skip and the remaining gap skipped in turn, because a planner that assumes circular orbits
         * drifts over a long skip; the date then lands on the plan as it reads at the end.
         */
        bool skip_to_burn(double ignition_in, bool follow, std::string &why);
        /** Seconds before ignition that "set time" lands on (/sim/settings/burn-lead-min, default 5 minutes). */
        double burn_lead_seconds() const;
        /** Frame time the fixed physics steps have not yet used (0 when warped or paused). */
        double step_remainder() const { return step_accumulator_; }
        const std::vector<std::string> &messages() const { return messages_; }
        const std::vector<Addon> &addons() const { return addons_; }
        const std::string &root() const { return root_; }
        const std::vector<Scenario> &scenarios() const { return scenarios_; }
        /** Match a scenario by id (file name without .json), case-insensitively, or by full name. */
        const Scenario *find_scenario(const std::string &name) const;
        /** Adds a scenario that does not live in an installed folder (replacing one with the same id). */
        void add_scenario(Scenario scenario);

        /** Appends to the HUD message list and stderr; also forwarded to log_sink (the Lua console). */
        void log(const std::string &text);
        std::function<void(const std::string &)> log_sink;

        /** Per-user folder (~/.config/kosmos): user scenarios/ and bases/ live here and override shipped ones with the same id. */
        static std::string user_dir();
        /** Re-read the user's scenarios and bases, replacing entries with the same id. Bases are only added or replaced when not yet loaded. */
        void load_user_content();
        /** Re-read every scenario folder (bundled, addons, user), so scenario files edited outside Kosmos show up. */
        void refresh_scenarios();

        Galaxy galaxy;
        Universe universe;
        sl::PropertyTree props;
        sl::LuaRuntime lua;
        MfdSystem mfd;
        /** Instruments, weapons and ordnance from every addon's equipment/ folder. */
        EquipmentLibrary equipment;
        WeaponLibrary weapon_specs;
        NavSystem nav;
        Weather weather;
        /** Built-up surface sites from data/bases/ and addons; their heights are filled in when the terrain is made. */
        std::vector<Base> bases;

    private:
        bool parked_for_warp(std::size_t index) const;
        void scan_addons();
        std::vector<std::string> disabled_addon_ids();
        void load_addon(const std::string &folder, const std::vector<std::string> &disabled_addons, std::vector<Transmitter> &beacons);
        void publish_galaxy();
        Controls read_controls(const std::string &prefix = "/controls", const Ship *ship = nullptr);
        /** Publish /target/... relative to the ship named by /sim/target (a ship or a body). */
        void update_target();
        /** Burn autopilot: waits for the ignition time, holds prograde and burns until the apoapsis reaches the target radius (see /autopilot/burn). */
        void run_burn();
        void run_autoland(double frame);
        void run_soyuz_autoland(double frame, Ship &ship);
        void run_timed_burn();
        void run_vessel_ascent(double dt);
        void load_active_script();

        std::string root_;
        std::vector<Addon> addons_;
        std::vector<Scenario> scenarios_;
        std::vector<std::unique_ptr<Ship>> ships_;
        const Ship *stage_handoff_from_ = nullptr, *stage_handoff_to_ = nullptr;
        int system_ = 0;
        int active_ = 0;
        double current_warp_ = 1.0;
        double step_accumulator_ = 0.0;
        long frame_count_ = 0;
        struct BurnSeek
        {
            bool active = false;
            int passes = 0, wait = 0;
            double previous_gap = 0.0;
            std::string source;
        } seek_;
        void follow_burn_plan();
        bool script_loaded_ = false;
        int ascent_ship_ = -1, ascent_body_ = -1, ascent_stage_ = -1;
        std::string ascent_profile_, ascent_phase_;
        double ascent_heading_ = 90.0, ascent_launch_heading_ = 90.0, ascent_target_ = 200000.0, ascent_pitch_ = 0.0, ascent_start_ = 0.0;
        double ascent_throttle_ = 0.0, ascent_hover_ = 0.0;
        double ascent_gimbal_ = 0.0, ascent_saved_gimbal_ = 0.0;
        bool landing_targeted_ = false, landing_orbital_ = false, landing_forecast_changed_ = false, landing_reserve_warned_ = false;
        int landing_body_ = -1, landing_stage_ = -1;
        double landing_latitude_ = 0.0, landing_longitude_ = 0.0;
        sl::dvec3 landing_direction_{0.0, 1.0, 0.0};
        sl::dvec3 landing_axis_{0.0, 1.0, 0.0};
        std::vector<LandingCandidate> landing_candidates_;
        std::unique_ptr<LandingCandidateForecast> landing_forecast_;
        LandingCandidate landing_candidate_;
        Base landing_base_;
        std::size_t landing_candidate_index_ = 0;
        double landing_plan_time_ = 0.0, landing_ignition_ = 0.0, landing_descent_start_ = 0.0;
        double landing_burn_delivered_ = 0.0, landing_last_time_ = 0.0, landing_last_level_ = 0.0, landing_last_mass_ = 0.0;
        double landing_checked_mass_ = 0.0, landing_checked_fuel_ = 0.0;
        sl::dvec3 landing_plan_position_{0.0}, landing_plan_velocity_{0.0};
        bool soyuz_landing_ = false, soyuz_checked_ = false;
        int soyuz_forecast_pass_ = 0;
        double soyuz_forecast_start_ = 0.0, soyuz_forecast_coast_ = 0.0;
        double soyuz_departure_ = 0.0, soyuz_expected_arrival_ = 0.0, soyuz_expected_entry_ = 0.0;
        double soyuz_search_start_ = 0.0, soyuz_search_wait_ = 0.0, soyuz_search_limit_ = 0.0;
        double soyuz_search_rate_ = 0.0, soyuz_search_reference_ = 0.0, soyuz_search_duration_ = 0.0;
        double soyuz_search_best_distance_ = 0.0, soyuz_search_best_departure_ = 0.0;
        sl::dvec3 soyuz_search_position_{0.0}, soyuz_search_normal_{0.0};
        sl::dvec3 soyuz_search_target_{0.0}, soyuz_search_pole_{0.0};
        std::unique_ptr<SoyuzEntryForecast> soyuz_forecast_;
        /** Whether the active ship was flying on its wings at the last step, to notice a hybrid ship crossing between air and space. */
        bool flying_as_aircraft_ = false;
        std::vector<std::string> messages_;
        std::vector<std::chrono::steady_clock::time_point> message_times_;
    };
} // namespace kosmos
