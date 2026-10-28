#pragma once

#include <string>
#include <vector>

namespace kosmos
{
    /** How a ship weapon looks and lights its surroundings. Colours are rgb 0-255. */
    struct WeaponLook
    {
        int colour[3] = {80, 200, 255};
        int core[3] = {255, 255, 255};
        /** Width of the bright core of a beam or bolt, metres. The halo is `glow` times wider. */
        double width = 0.5;
        double glow = 4.0;
        /** Length of a bolt's streak, metres. */
        double length = 14.0;
        /** 0 to 1: how many sparks the weapon sheds along its path and at an impact. */
        double sparkle = 0.5;
        /** Brightness and reach (metres) of the light a shot casts on nearby hulls and ground. */
        double light = 1.0, light_range = 200.0;
    };

    /**
     * A weapon a ship can mount (see `weapons` in ship.json), read from `<addon>/weapons/*.json`.
     * `fire` is "pulse" (an instant ray drawn as a short-lived streak), "beam" (a ray that flares and fades out, then recharges), "bolt" (a glowing projectile)
     * or "flak" (a burst of small bolts that scatter sparks where they end)
     * or "volley" (`count` bolts fired one after another from a single trigger pull).
     */
    struct WeaponSpec
    {
        std::string id, name, fire = "bolt";
        /** Shots (or bursts) per second for pulse, bolt and flak. */
        double rate = 2.0;
        /** Damage per hit as a fraction of a ship's health; a beam does this much per second. */
        double ship_damage = 0.01;
        double range = 5000.0, speed = 2000.0;
        /** Random aim error in radians. */
        double spread = 0.002;
        /** Flak: bolts per burst and the half-angle of the burst, in radians. */
        int count = 8;
        double cone = 0.06;
        /** Pulse: seconds the streak stays visible. */
        double duration = 0.25;
        /** Beam: `duration` is how long one pulse lasts (it flares, then fades to nothing); `cooldown` is the recharge before the next. */
        double cooldown = 2.0;
        /** Volley: seconds between the `count` shots of one trigger pull. */
        double interval = 0.06;
        /** Flak: seconds a burst bolt flies before it bursts; 0 uses range over speed. */
        double lifetime = 0.0;
        /** "spark", "plasma" or "explosion": what a hit leaves besides sparks and a flare. */
        std::string impact = "spark";
        double impact_scale = 1.0;
        WeaponLook look;
    };

    /** The ship weapons loaded from every enabled addon. Specs stay at fixed addresses once loaded. */
    class WeaponLibrary
    {
    public:
        /** Read every `<addon>/weapons/*.json`; each file holds one object or a list under "weapons". */
        void load(const std::vector<std::string> &addon_directories, std::vector<std::string> &problems);
        const WeaponSpec *find(const std::string &id) const;
        const std::vector<WeaponSpec> &all() const { return items_; }

    private:
        std::vector<WeaponSpec> items_;
    };
} // namespace kosmos
