// A mission: a start point, a ship with a payload and an ordered list of legs, each with its delta-v. Missions are saved as ordinary Kosmos
// scenarios (the ship at the start point) with an extra "mission" block that the game ignores and KosmosMission reads back.
#pragma once

#include "budget.h"
#include "payload.h"

#include <string>
#include <vector>

namespace km
{
    enum class LegType
    {
        Ascent,   // surface to a circular orbit
        Transfer, // departure burn towards another orbit or body
        Arrival,  // capture or circularise
        Plane,    // plane change
        Custom
    };

    const char *leg_type_name(LegType t);
    LegType leg_type_from_name(const std::string &s);

    struct Leg
    {
        LegType type = LegType::Custom;
        std::string name;
        std::string body;        // where the leg happens (central body, or departure body for transfers)
        std::string to_body;     // destination body for transfers and arrivals
        double dv = 0;           // m/s
        double duration = 0;     // seconds of flight (coasting) after the burn
        double depart_days = 0;  // days after the mission epoch at which the burn is made
        double altitude = 0;     // m: circular orbit reached or left by this leg
        double dv_extra = 0;     // the arrival half of a transfer when the two burns are one leg
        std::string note;
    };

    struct Start
    {
        std::string body = "Earth";
        std::string base;                // a base id when the mission starts on the ground
        double altitude = 400000;        // m, circular orbit height when starting in orbit
        double inclination = 0;          // degrees
        double latitude = 0;             // degrees, for the ascent estimate
        bool in_orbit() const { return base.empty(); }
    };

    struct Mission
    {
        std::string name = "New mission";
        std::string description;
        Start start;
        std::string ship;       // Ship name (as in the ship picker)
        std::string ship_file;  // path to its ship.json, if known
        double payload = 0;     // kg of loose extra mass carried to the end of the mission
        std::vector<PayloadItem> payloads; // ships and cargo carried, checked against the ship's payload volumes
        double payload_mass() const { return payload + items_mass(payloads); }
        std::vector<Leg> legs;
        double margin = 0.05;   // fraction of reserve held back from the ship's delta-v

        double total_dv() const;
        /** Delta-v still to be paid at the end of each leg's burn, with the ship's budget `available`. */
        std::vector<double> remaining_after(double available) const;
    };

    /** A ledger line for one leg. */
    struct LedgerRow
    {
        std::string name;
        double dv = 0, cumulative = 0, remaining = 0;
        bool short_of = false; // true once the budget runs out
    };

    std::vector<LedgerRow> ledger(const Mission &m, double available);

    /** The scenario JSON text for `m`; `ship_name` and the start are written as the scenario's ship and position. */
    std::string to_scenario_json(const Mission &m, const ShipSpec *launcher = nullptr);
    /** Reads a scenario (or a plain mission file); returns false if it has no "mission" block. */
    bool from_scenario_json(const std::string &text, Mission &out, std::string &err);
    bool save_mission(const std::string &path, const Mission &m, std::string &err, const ShipSpec *launcher = nullptr);
    bool load_mission(const std::string &path, Mission &out, std::string &err);

    /** A plain-text summary and a CSV of the legs. */
    std::string summary_text(const Mission &m, double available);
    std::string summary_csv(const Mission &m, double available);
} // namespace km
