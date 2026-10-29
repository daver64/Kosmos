#pragma once

#include "interior.h"

namespace kosmos
{
    class Ship;

    constexpr double airlock_safe_kpa = 5.0;

    /**
     * Runs the doors and the airlock of a building: doors slide open for a person who walks up to them, unless the air on the two sides
     * differs by more than a few kPa; an open door joins the two rooms' air. `person` is where the walker stands (interior metres), or null.
     */
    void update_interior(Ship &building, const sl::dvec3 *person, double dt);

    /** Pressure in kPa of the named room's vessel, or a negative number when it has none. */
    double room_pressure_kpa(const Ship &building, int room);
} // namespace kosmos
