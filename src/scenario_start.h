#pragma once

#include "app_common.h"
#include "sim.h"
#include "options.h"

namespace kosmos::app
{
    // A flat patch of dry land for the pad, found by scanning the planet's height function.
    sl::dvec3 choose_pad(const sl::Terrain &terrain, double &height, const sl::dvec3 &to_sun);

    sl::dvec3 direction_from_lat_lon(const kosmos::Universe &universe, int body, double lat_deg, double lon_deg);

    // The launch pad is found at run time, so its radio beacons are registered where it ends up.
    void register_pad_beacons(Sim &sim, int body, const sl::dvec3 &direction);

    // The terrains of the bodies that have one: the home planet and any with Orbiter elevation data.
    struct TerrainSet
    {
        std::vector<std::pair<int, const sl::Terrain *>> items;
        const sl::Terrain *find(int body) const
        {
            for (const auto &item : items)
                if (item.first == body)
                    return item.second;
            return nullptr;
        }
    };

    // Every "building" object of every base becomes a fixed, enterable ship standing at its place.
    void spawn_buildings(Sim &sim);

    // The quantum portal ship of the current system, or nullptr.
    Ship *portal_ship(Sim &sim);
    // The direction the portal's disc faces (its +Z axis in world coordinates).
    sl::dvec3 portal_axis(const Ship &portal);
    // Adds the system's portal in orbit of its star, unless it already has one.
    void spawn_portal(Sim &sim);
    // Puts a ship `distance` metres in front of the portal on its axis, moving `speed` m/s along it, facing the portal (or away from it).
    void place_at_portal(Sim &sim, Ship &ship, double distance, double speed, bool facing_away);

    // Returns the ship to fly, or nullptr (with a message on stderr) if the scenario cannot be built.
    Ship *apply_scenario(Sim &sim, const kosmos::Scenario &sc, const TerrainSet &terrains);

    // Swings every ship about the planet's axis until the active one is on the Sun side, keeping the fleet's layout intact.
    void rotate_fleet_to_sun(Sim &sim, Ship *flown);

    Ship *start_scenario(Sim &sim, const Options &options, const TerrainSet &terrains, const ShipPropellantState *propellant_state = nullptr);
} // namespace kosmos::app
