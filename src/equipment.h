#pragma once

#include "reentry.h"
#include "universe.h"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace kosmos
{
    enum class EquipKind
    {
        /** A handheld sensor: tricorder, geology scanner, survey gear. */
        instrument,
        /** Fired by a person: pistol, rifle, plasma gun. */
        weapon,
        /** Thrown or placed by a person and detonated after a fuse. */
        explosive,
        /** Carried on a ship's hardpoint and released to fall: bombs. */
        ordnance
    };

    /** One simple solid of a held item's stand-in model, in view space (x right, y up, -z away from the eye). */
    struct ViewShape
    {
        enum Type
        {
            box,
            cylinder,
            sphere
        };
        Type type = box;
        glm::vec3 position{0.0f};
        /** Box: width, height, depth. Cylinder: diameter, diameter, length along Z. Sphere: diameter. */
        glm::vec3 size{0.1f};
        std::array<std::uint8_t, 3> colour{128, 128, 128};
        /** Drawn unlit and bright, as a screen or energy cell. */
        bool glow = false;
        /** Shows the instrument readout as a live text screen (glow items only). */
        bool screen = false;
    };

    /** What a piece of equipment is and does, from an addon's equipment/NAME.json. */
    struct EquipmentSpec
    {
        std::string id, name, description;
        EquipKind kind = EquipKind::instrument;
        double mass = 1.0;
        /** Part of every person's default kit. */
        bool standard = false;
        std::vector<ViewShape> shapes;

        /** "hitscan" (an instant ray), "bolt" (a fast projectile) or "" for none. Weapons only. */
        std::string fire = "";
        /** Shots per second. */
        double rate = 2.0;
        /** Damage per hit as a fraction of a person's health (1 is a kill); against ships and buildings it is scaled by `ship_damage`. */
        double damage = 0.2;
        double ship_damage = 0.002;
        double range = 300.0;
        /** Bolt speed in m/s. */
        double speed = 200.0;
        /** Random aim error in radians. */
        double spread = 0.01;
        /** Rounds carried; negative is unlimited. */
        int ammo = -1;
        /** "plasma" or "" : the look of the muzzle flash and the bolt. */
        std::string effect;

        /** For an explosive or ordnance: "impact", "timer" or "airburst". */
        std::string fuse = "impact";
        double fuse_time = 3.0;
        /** Height above the ground of an airburst, in metres. */
        double burst_height = 500.0;
        /** Props library model for a dropped item ("bomb" or "fatman"); sizes are metres. */
        std::string prop;
        double length = 3.0, radius = 0.25;
        /** Drag area (Cd times cross-section) in m^2; a bomb falls faster than a feather. */
        double drag_area = 0.08;
        /** Blast: lethal radius in metres, with damage fading to nothing at the edge. */
        double blast_radius = 20.0;
        /** Damage at the centre, as a fraction of a ship's health. */
        double blast_damage = 0.6;
        /** "explosion" or "mushroom". */
        std::string blast_effect = "explosion";
        /** Yield factor for "mushroom": 1 is about 20 kilotons. */
        double yield = 1.0;
        /** Radius, in metres, within which the flash of a nuclear burst blinds and the thermal pulse burns. */
        double thermal_radius = 0.0;
        /** Heating while falling through air (the "thermal" block); a round whose damage reaches 1 burns up without detonating. */
        ThermalSpec thermal;
        /** A ship class released in its place: the item becomes a vessel stepped by the physics world, like a jettisoned stage. */
        std::string ship;

        /** "tricorder", "geology" or "survey" for an instrument. */
        std::string sensor;
    };

    /** The equipment loaded from every enabled addon. Specs stay at fixed addresses once loaded. */
    class EquipmentLibrary
    {
    public:
        /** Read every `<addon>/equipment/NAME.json`; each file holds one object or a list under "equipment". */
        void load(const std::vector<std::string> &addon_directories, std::vector<std::string> &problems);
        const EquipmentSpec *find(const std::string &id) const;
        const std::vector<EquipmentSpec> &all() const { return items_; }

    private:
        std::vector<EquipmentSpec> items_;
    };

    /** How much of a blast's damage reaches `distance` metres from the centre: 1 at the centre, fading linearly to 0 at `radius`. */
    inline double blast_fraction(double distance, double radius)
    {
        return radius <= 0.0 ? 0.0 : 1.0 - std::clamp(distance / radius, 0.0, 1.0);
    }

    /** An entry in somebody's pockets or on a rack. */
    struct Stack
    {
        std::string id;
        /** How many are left; unlimited weapons ignore this. */
        int count = 1;
    };
} // namespace kosmos
