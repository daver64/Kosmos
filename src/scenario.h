#pragma once

#include <string>
#include <vector>

namespace kosmos
{
    /** One vessel in a scenario: either in a circular orbit or standing on a surface. */
    struct ScenarioShip
    {
        std::string ship;  // ship spec name, from any addon
        std::string label; // optional replacement display name
        std::string body;  // Earth, Moon, ...
        bool landed = false;
        // Orbit.
        double altitude_km = 200.0, inclination = 0.0, phase = 0.0;
        // Optional: an ellipse instead of a circle. altitude_km is then the periapsis, phase the true anomaly, and when
        // `toward` names a body the orbit lies in that body's orbital plane with its apoapsis aimed where the body will be.
        double apoapsis_km = 0.0;
        std::string toward;
        // Surface: either latitude/longitude, or the home planet's launch pad when pad is set.
        bool pad = false;
        std::string base; // a base id: stand on that base's pad instead
        int runway = -1;  // with a base: start on this runway (index) instead of the pad
        bool reverse = false;
        double along = 40.0;     // metres past the runway threshold (negative: before it)
        double altitude = 0.0;   // metres above the ground; above zero starts airborne
        double speed = 0.0;      // airspeed in m/s when airborne
        double latitude = 0.0, longitude = 0.0, heading = 90.0;
    };

    /**
     * A starting situation, like an Orbiter scenario: which ships, where, and which one you fly.
     * Files are JSON in data/scenarios/ or listed under "scenarios" in an addon.json.
     */
    struct Scenario
    {
        std::string id, name, description, path;
        int active = 0;
        /** Initial /sim/target (a ship or body name) and NAV1/NAV2 frequencies; empty or 0 leaves them unset. */
        std::string target;
        std::vector<double> nav;
        std::vector<ScenarioShip> ships;
    };

    /** The scenario as the JSON text load_scenario reads back. */
    std::string scenario_to_json(const Scenario &scenario);
    /** Write a scenario to `path`, creating folders as needed. */
    bool save_scenario(const std::string &path, const Scenario &scenario, std::string &error);
    /** Parse scenario JSON text; `path` names the file for the id and for error messages. */
    bool parse_scenario(const std::string &text, const std::string &path, Scenario &out, std::string &error);
    bool load_scenario(const std::string &path, Scenario &out, std::string &error);
    /** Every *.json in a folder, sorted by file name. Unreadable files are reported through `errors`. */
    std::vector<Scenario> load_scenarios_in(const std::string &folder, std::vector<std::string> &errors);
} // namespace kosmos
