// Delta-v budgets: a ship's stages from its ship.json, with a payload, and a rule-of-thumb ascent cost with measured overrides.
#pragma once

#include "bodies.h"

#include <map>
#include <string>
#include <vector>

namespace km
{
    struct StageSpec
    {
        std::string name;
        double dry_mass = 0, propellant = 0, thrust = 0, isp = 0, isp_sl = 0;
        double length = 0, radius = 0; // m, as drawn
    };

    /** A volume on a ship that carries payload, from "payload_bays" in ship.json. The bay's axis is the ship's z axis. */
    struct PayloadBay
    {
        std::string name;
        bool round = true;                // cylinder (radius, length) or box (width, height, length)
        double radius = 0, width = 0, height = 0, length = 0;
        double max_mass = 0;              // kg; 0 means no limit
        double position[3] = {0, 0, 0};   // where the bay's centre sits on the ship, for placing the payload in the game
    };

    /** How big a ship is as cargo: a cylinder (diameter, length) or a box (width, height, length). */
    struct PayloadSize
    {
        bool known = false;
        bool round = true;
        double diameter = 0, width = 0, height = 0, length = 0;
    };

    struct ShipSpec
    {
        std::vector<PayloadBay> bays;
        PayloadSize size; // "payload_size" in ship.json, else a cylinder from the stages' radius and length
        std::string name, file;
        std::vector<StageSpec> stages; // in firing order
        double rcs_propellant = 0, rcs_isp = 0;
        double mass() const; // everything, wet
    };

    /** Reads a ship.json. Returns false and sets `err` if it cannot be read or has no stages. */
    bool load_ship(const std::string &path, ShipSpec &out, std::string &err);
    /** Every addons/<id>/ship.json under `root` that has stages, sorted by name. */
    std::vector<ShipSpec> list_ships(const std::string &root);

    struct StageDv
    {
        std::string name;
        double m0 = 0, m1 = 0, dv = 0;
    };

    struct Budget
    {
        std::vector<StageDv> stages;
        double total = 0;
        double payload = 0;
        /** Delta-v still available after the first `used` m/s have been spent, summed over the stages (stages burn in order). */
        double remaining(double used) const { return total - used; }
    };

    /** The rocket equation for each stage in order, with `payload` kilograms carried to the end (vacuum Isp unless `use_sea_level`). */
    Budget budget(const ShipSpec &ship, double payload, bool use_sea_level = false);

    struct Ascent
    {
        double dv = 0;
        double v_orbit = 0, v_rotation = 0, loss = 0;
        bool measured = false;
        std::string note;
    };

    /**
     * Launch from the surface to a circular orbit at `altitude`: orbital speed, less the speed the ground already has toward the orbit's direction
     * (equator speed * cos(latitude), for a prograde launch), plus a gravity and drag loss of k * surface gravity (k in seconds).
     */
    Ascent ascent_estimate(const Body &body, double altitude, double latitude_deg);

    /** Measured launch costs, by "ship|body", which replace the estimate. */
    class AscentTable
    {
      public:
        bool load(const std::string &path);
        bool find(const std::string &ship, const std::string &body, double &dv, std::string &note) const;
        void set(const std::string &ship, const std::string &body, double dv, const std::string &note);

      private:
        struct Entry
        {
            double dv;
            std::string note;
        };
        std::map<std::string, Entry> entries_;
    };

    Ascent ascent(const Body &body, double altitude, double latitude_deg, const std::string &ship, const AscentTable &table);
} // namespace km
