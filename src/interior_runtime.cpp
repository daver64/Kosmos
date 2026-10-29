#include "interior_runtime.h"

#include "ship.h"

#include <algorithm>
#include <cmath>

namespace kosmos
{
    namespace
    {
        constexpr double door_reach = 1.5, door_speed = 2.0, door_flow_cm2 = 20000.0, hatch_vent_cm2 = 100.0;

        void resolve(Ship &building)
        {
            const Interior &room = *building.spec().interior;
            InteriorState &st = building.interior_state;
            const Systems *sys = building.systems();
            st.vessel_of_room.assign(static_cast<std::size_t>(room.room_count()), -1);
            st.door_open.resize(room.doors().size(), 0.0);
            if (sys)
                for (int r = 0; r < room.room_count(); ++r)
                    for (int v = 0; v < sys->vessel_count(); ++v)
                        if (!room.room_name(r).empty() && sys->vessels()[static_cast<std::size_t>(v)].spec.name == room.room_name(r))
                            st.vessel_of_room[static_cast<std::size_t>(r)] = v;
            st.resolved = true;
        }
    } // namespace

    double room_pressure_kpa(const Ship &building, int room)
    {
        const InteriorState &st = building.interior_state;
        const Systems *sys = building.systems();
        if (!sys || room < 0 || static_cast<std::size_t>(room) >= st.vessel_of_room.size() || st.vessel_of_room[static_cast<std::size_t>(room)] < 0)
            return -1.0;
        return sys->vessels()[static_cast<std::size_t>(st.vessel_of_room[static_cast<std::size_t>(room)])].pressure_kpa();
    }

    void update_interior(Ship &building, const sl::dvec3 *person, double dt)
    {
        if (!building.spec().interior)
            return;
        const Interior &room = *building.spec().interior;
        InteriorState &st = building.interior_state;
        if (!st.resolved)
            resolve(building);
        Systems *sys = building.systems();

        auto vessel = [&](int r) { return r >= 0 && static_cast<std::size_t>(r) < st.vessel_of_room.size() ? st.vessel_of_room[static_cast<std::size_t>(r)] : -1; };
        int airlock_room = -1;
        for (int r = 0; r < room.room_count() && !room.airlock_name().empty(); ++r)
            if (room.room_name(r) == room.airlock_name())
                airlock_room = r;
        const int airlock = vessel(airlock_room);

        if (sys && airlock >= 0)
        {
            const bool pumping = st.airlock == InteriorState::Airlock::normal;
            for (SystemComponent &c : sys->components())
                if (c.type == "regulator" && c.vessel == room.airlock_name())
                    c.enabled = pumping;
            sys->set_opening(airlock, 1, st.airlock == InteriorState::Airlock::venting ? hatch_vent_cm2 : 0.0);
        }

        for (std::size_t i = 0; i < room.doors().size(); ++i)
        {
            const Interior::Door &door = room.doors()[i];
            const int va = vessel(door.room_a), vb = vessel(door.room_b);
            bool locked = false;
            if (sys && va >= 0 && vb >= 0)
            {
                const double pa = sys->vessels()[static_cast<std::size_t>(va)].pressure_kpa(), pb = sys->vessels()[static_cast<std::size_t>(vb)].pressure_kpa();
                locked = std::fabs(pa - pb) > airlock_safe_kpa;
            }
            // The airlock is shut to the rest of the building while it is pumped down or empty.
            if (airlock >= 0 && (va == airlock || vb == airlock) && st.airlock != InteriorState::Airlock::normal)
                locked = true;
            double &open = st.door_open[i];
            bool near = false, in_way = false;
            if (person)
            {
                const double dx = person->x - door.centre.x, dz = person->z - door.centre.z;
                near = std::hypot(dx, dz) < door_reach && std::fabs(person->y - door.centre.y) < 2.2;
                in_way = std::fabs(dx) < 0.55 && std::fabs(dz) < 0.55 && std::fabs(person->y - door.centre.y) < 2.2;
            }
            const double target = !locked && near ? 1.0 : 0.0;
            if (target < open && in_way)
                continue; // never close on someone standing in the gap
            open = std::clamp(open + std::clamp(target - open, -door_speed * dt, door_speed * dt), 0.0, 1.0);
            if (sys && va >= 0 && vb >= 0)
                sys->set_link(va, vb, 100 + static_cast<int>(i), open > 0.3 ? door_flow_cm2 : 0.0);
        }
    }
} // namespace kosmos
