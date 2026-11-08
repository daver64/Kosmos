#pragma once

#include <array>
#include <memory>
#include <utility>
#include <string>
#include <vector>

namespace kosmos
{
    struct ShipCarryState;
    /** One vessel in a scenario: either in a circular orbit or standing on a surface. */
    struct ScenarioShip
    {
        std::string ship;  // ship spec name, from any addon
        std::string label; // optional replacement display name
        std::string skin;  // optional skin folder of the ship (see ship_skins)
        std::string body;  // Earth, Moon, ...
        /** Initially point the nose at this body; not used with exact saved state. */
        std::string point_at;
        bool landed = false;
        // Orbit.
        double altitude_km = 200.0, inclination = 0.0, phase = 0.0;
        // Optional: an ellipse instead of a circle. altitude_km is then the periapsis, phase the true anomaly, and when
        // `toward` names a body the orbit lies in that body's orbital plane with its apoapsis aimed where the body will be.
        double apoapsis_km = 0.0;
        std::string toward;
        // When set (with `apoapsis_km` unused), the ship is on the escape hyperbola of a Hohmann departure to this planet; altitude_km is the periapsis and phase the angle past it.
        std::string escape_to;
        // Optional exact state, relative to the body's centre (world axes); orientation is w, x, y, z.
        // The orbit or surface placement is kept as a fallback for older readers.
        bool has_state = false;
        std::array<double, 3> state_position{}, state_velocity{};
        std::array<double, 4> state_orientation{1.0, 0.0, 0.0, 0.0};
        std::array<double, 3> state_angular_velocity{};
        std::shared_ptr<const ShipCarryState> carry_state;
        // With escape_to: the flight time in days, for a Lambert transfer that arrives at the planet then. Zero is a Hohmann transfer.
        double escape_days = 0.0;
        // When above zero the ship starts this many metres in front of the system's quantum portal, facing it. The orbit above is ignored.
        double portal_m = 0.0;
        // Surface: either latitude/longitude, or the home planet's launch pad when pad is set.
        bool pad = false;
        std::string base; // a base id: stand on that base's pad instead
        int runway = -1;  // with a base: start on this runway (index) instead of the pad
        bool reverse = false;
        /** Starts docked: the index of an earlier ship in the scenario, its port, and this ship's port. The ship still needs a position. */
        int dock_to = -1, dock_port = 0, own_port = 0;
        double along = 40.0;     // metres past the runway threshold (negative: before it)
        double altitude = 0.0;   // metres above the ground; above zero starts airborne
        double speed = 0.0;      // airspeed in m/s when airborne
        double latitude = 0.0, longitude = 0.0, heading = 90.0;
        // Systems state at the start (see the ship's "systems"): a cabin leak in cm2, tank fill fractions by gas, and battery charge (-1 keeps full).
        double leak_cm2 = 0.0, battery = -1.0;
        /** Years the RTGs have already aged at the start. */
        double rtg_age_years = 0.0;
        std::vector<std::pair<std::string, double>> tanks;
    };

    /**
     * A starting situation, like an Orbiter scenario: which ships, where, and which one you fly.
     * Files are JSON in scenarios/ or listed under "scenarios" in an addon.json.
     */
    struct Scenario
    {
        std::string id, name, description, path, thumbnail;
        /** Menu folder, from the sub-folders the file sits in ("Saturn", "Saturn/Moons"); empty at the top level. Not stored in the file. */
        std::string group;
        int active = 0;
        /** Initial /sim/target (a ship or body name) and NAV1/NAV2 frequencies; empty or 0 leaves them unset. */
        std::string target;
        /** The star system the scenario takes place in, by name; empty means Sol. Starting the scenario jumps there if need be. */
        std::string system;
        /** "off", "warn" or "fatal": what failing life support does to the crew. Empty keeps the setting. */
        std::string life_support;
        /** A METAR line that sets the weather everywhere (e.g. "XXXX 011200Z 27025G40KT 2000 +RA BKN008 08/07 Q0995"); empty uses the normal weather. */
        std::string weather;
        /** Start in desktop fullscreen. */
        bool fullscreen = false;
        /** Starting throttle (0 to 1), whether the autopilot starts engaged, and MFD mode ids for the left and right units (empty keeps the ship's defaults). */
        double throttle = 0.0;
        bool autopilot = false;
        std::string mfd_left, mfd_right;
        /** Which MFD units start powered (-1 leaves them as the ship has them, 0 off, 1 on), the head-up display mode ("off", "orbit", "surface", "docking"; empty keeps it) and how the MFDs are shown ("panels", "hud" for see-through, or "off"; empty keeps it). */
        int mfd_left_on = -1, mfd_right_on = -1;
        std::string hud, mfd_display;
        /** The starting camera: "chase", "cockpit" (2D panel) or "cockpit3d"; empty keeps the default. */
        std::string view;
        /** Properties set when the scenario starts, as "path=value" (a number, true/false or text); written as an object in the JSON. */
        std::vector<std::string> sets;
        /** Start inside the building of this name (a base's building), looking at its panel. */
        std::string enter_building;
        std::vector<double> nav;
        /** Start date as a Julian date (UTC); 0 starts on the system's default date. date_text keeps the text it was written as, if any. */
        double epoch_jd = 0.0;
        std::string date_text;
        std::vector<ScenarioShip> ships;
    };

    /** The scenario as the JSON text load_scenario reads back. */
    std::string scenario_to_json(const Scenario &scenario);
    /** Write a scenario to `path`, creating folders as needed. */
    bool save_scenario(const std::string &path, const Scenario &scenario, std::string &error);
    /** Parse scenario JSON text; `path` names the file for the id and for error messages. */
    bool parse_scenario(const std::string &text, const std::string &path, Scenario &out, std::string &error);
    bool load_scenario(const std::string &path, Scenario &out, std::string &error);
    /** Every *.json in a folder and its sub-folders, sorted by path (a leading "NN_" is dropped from group names). Unreadable files are reported through `errors`. */
    std::vector<Scenario> load_scenarios_in(const std::string &folder, std::vector<std::string> &errors);
} // namespace kosmos
