#include "scenario_start.h"
#include "settings.h"

namespace kosmos::app
{
    // A flat patch of dry land for the pad, found by scanning the planet's height function.
    sl::dvec3 choose_pad(const sl::Terrain &terrain, double &height, const sl::dvec3 &to_sun)
    {
        sl::dvec3 best(1.0, 0.0, 0.0);
        double best_score = 1.0e30;
        height = 0.0;
        const double step = 600.0 / 6.371e6;
        for (int lat = -30; lat <= 30; lat += 2)
            for (int lon = 0; lon < 360; lon += 2)
            {
                const double la = lat * pi / 180.0, lo = lon * pi / 180.0;
                const sl::dvec3 d(std::cos(la) * std::cos(lo), std::sin(la), std::cos(la) * std::sin(lo));
                if (glm::dot(d, to_sun) < 0.9)
                    continue;
                const double h = terrain.height_at_direction(d);
                if (h < 60.0 || h > 600.0)
                    continue;
                const sl::dvec3 east = glm::normalize(glm::cross(sl::dvec3(0, 1, 0), d));
                const sl::dvec3 north = glm::cross(d, east);
                double worst = 0.0;
                for (const sl::dvec3 &o : {east, -east, north, -north})
                    worst = std::max(worst, std::fabs(terrain.height_at_direction(glm::normalize(d + o * step)) - h));
                if (worst < best_score)
                {
                    best_score = worst;
                    best = d;
                    height = h;
                }
            }
        return best;
    }

    sl::dvec3 direction_from_lat_lon(const kosmos::Universe &universe, int body, double lat_deg, double lon_deg)
    {
        const double la = lat_deg * pi / 180.0, lo = lon_deg * pi / 180.0;
        const double bx = std::cos(la) * std::cos(lo), by = std::sin(la), bz = -std::cos(la) * std::sin(lo);
        return universe.orientation(body) * sl::dvec3(bx, by, bz);
    }

    // The launch pad is found at run time, so its radio beacons are registered where it ends up.
    void register_pad_beacons(Sim &sim, int body, const sl::dvec3 &direction)
    {
        kosmos::Transmitter pad;
        pad.body = sim.universe.bodies[static_cast<std::size_t>(body)].name;
        kosmos::surface_lat_lon(sim.universe, body, direction, pad.latitude, pad.longitude);
        pad.type = "vtol";
        pad.id = "launch-pad";
        pad.name = "Launch Pad";
        pad.frequency = 92.10;
        pad.range = 3.0e5;
        pad.heading = 90.0;
        sim.nav.add_beacon(pad);
        pad.type = "vor";
        pad.id = "launch-vor";
        pad.name = "Launch VOR";
        pad.frequency = 112.00;
        pad.range = 2.0e6;
        sim.nav.add_beacon(pad);
    }

    static int star_body(Sim &sim)
    {
        for (std::size_t i = 0; i < sim.universe.bodies.size(); ++i)
            if (sim.universe.bodies[i].kind == kosmos::BodyKind::star && sim.universe.bodies[i].parent < 0)
                return static_cast<int>(i);
        return -1;
    }

    Ship *portal_ship(Sim &sim)
    {
        for (int i = 0; i < sim.ship_count(); ++i)
            if (sim.ship(i)->spec().portal_radius > 0.0)
                return sim.ship(i);
        return nullptr;
    }

    sl::dvec3 portal_axis(const Ship &portal)
    {
        return portal.body().orientation * sl::dvec3(0.0, 0.0, 1.0);
    }

    // The system's quantum portal orbits its star beyond the middle of the planets, so reaching it is a real flight.
    void spawn_portal(Sim &sim)
    {
        if (portal_ship(sim))
            return;
        const kosmos::ShipSpec *spec = sim.find_ship_spec("Quantum Portal");
        const int star = star_body(sim);
        if (!spec || star < 0)
            return;
        const sl::dvec3 centre = sim.universe.world.body(star)->position;
        double outermost = 0.0;
        for (std::size_t i = 0; i < sim.universe.bodies.size(); ++i)
            if (sim.universe.bodies[i].parent == star && sim.universe.bodies[i].kind == kosmos::BodyKind::planet)
                outermost = std::max(outermost, glm::length(sim.universe.world.body(static_cast<int>(i))->position - centre));
        const double radius = sim.universe.bodies[static_cast<std::size_t>(star)].radius;
        const double orbit = std::max(outermost * 0.43, radius * 20.0);
        Ship *portal = sim.spawn(*spec);
        portal->place_in_orbit(star, orbit - radius, 0.0, 0.0);
        // The disc faces along the radius, so a ship approaching from outside the system meets it face on.
        const sl::dvec3 out = glm::normalize(portal->body().position - centre);
        const sl::dvec3 z(0.0, 0.0, 1.0);
        const sl::dvec3 turn = glm::cross(z, out);
        const double sine = glm::length(turn);
        portal->body().orientation = sine < 1.0e-9 ? sl::dquat(1.0, 0.0, 0.0, 0.0) : glm::angleAxis(std::atan2(sine, glm::dot(z, out)), turn / sine);
        portal->body().angular_velocity = sl::dvec3(0.0);
    }

    void place_at_portal(Sim &sim, Ship &ship, double distance, double speed, bool facing_away)
    {
        Ship *portal = portal_ship(sim);
        if (!portal)
            return;
        const int star = star_body(sim);
        const sl::FlightBody &sb = *sim.universe.world.body(star);
        const sl::dvec3 axis = portal_axis(*portal);
        const sl::dquat facing = facing_away ? portal->body().orientation * glm::angleAxis(3.14159265358979, sl::dvec3(0.0, 1.0, 0.0)) : portal->body().orientation;
        ship.place_relative(star, portal->body().position - sb.position + axis * distance, portal->body().velocity - sb.velocity + axis * speed, facing);
    }

    // Every "building" object of every base becomes a fixed, enterable ship standing at its place.
    void spawn_buildings(Sim &sim)
    {
        for (const kosmos::Base &base : sim.bases)
        {
            const int body = sim.universe.find(base.body);
            if (body < 0)
                continue;
            const double radius = sim.universe.bodies[static_cast<std::size_t>(body)].radius;
            for (const kosmos::BaseObject &o : base.objects)
            {
                if (o.type != "building")
                    continue;
                const kosmos::ShipSpec *spec = sim.find_building_spec(o.building);
                if (!spec)
                {
                    std::fprintf(stderr, "kosmos: base %s: unknown building \"%s\"\n", base.id.c_str(), o.building.c_str());
                    continue;
                }
                for (int i = 0; i < o.count; ++i)
                {
                    Ship *building = sim.spawn(*spec);
                    if (o.count > 1)
                        building->name += " " + std::to_string(i + 1);
                    building->place_on_surface(body, kosmos::base_point(base, radius, o.x + o.step_x * i, o.z + o.step_z * i), base.heading + o.heading, base.height + o.y);
                }
            }
        }
    }

    namespace
    {
        // False (with a message on stderr) if a ship's escape target is not a sibling of the body it starts from.
        bool escape_targets_valid(Sim &sim, const kosmos::Scenario &sc)
        {
            for (const kosmos::ScenarioShip &entry : sc.ships)
            {
                if (entry.landed || entry.escape_to.empty())
                    continue;
                const int origin = sim.universe.find(entry.body);
                if (origin < 0)
                    continue;
                const int target = sim.universe.find(entry.escape_to);
                const int parent = sim.universe.bodies[static_cast<std::size_t>(origin)].parent;
                if (target < 0 || target == origin || parent < 0 ||
                    sim.universe.bodies[static_cast<std::size_t>(target)].parent != parent)
                {
                    std::fprintf(stderr, "kosmos: scenario %s: invalid escape target \"%s\" from %s; target must orbit the same body\n",
                                 sc.id.c_str(), entry.escape_to.c_str(), entry.body.c_str());
                    return false;
                }
            }
            return true;
        }

        void apply_ship_systems(Ship &ship, const kosmos::ScenarioShip &entry)
        {
            kosmos::Systems *systems = ship.systems();
            if (!systems)
                return;
            for (const auto &tank : entry.tanks)
                systems->set_tank_fraction(tank.first, tank.second);
            if (entry.battery >= 0.0)
                systems->set_battery_fraction(entry.battery);
            if (entry.rtg_age_years > 0.0)
                systems->add_rtg_age(entry.rtg_age_years);
            if (entry.leak_cm2 > 0.0)
                systems->breach(0, entry.leak_cm2);
        }

        void place_in_space(Sim &sim, Ship &ship, const kosmos::ScenarioShip &entry, int body)
        {
            if (!entry.escape_to.empty())
                ship.place_on_escape(body, entry.altitude_km * km, entry.phase, sim.universe.find(entry.escape_to), entry.escape_days);
            else if (entry.apoapsis_km > 0.0)
                ship.place_on_ellipse(body, entry.altitude_km * km, entry.apoapsis_km * km, entry.phase, entry.toward.empty() ? -1 : sim.universe.find(entry.toward));
            else
                ship.place_in_orbit(body, entry.altitude_km * km, entry.inclination, entry.phase);
            if (entry.has_state)
            {
                const auto &r = entry.state_position;
                const auto &v = entry.state_velocity;
                const auto &q = entry.state_orientation;
                ship.place_relative(body, sl::dvec3(r[0], r[1], r[2]), sl::dvec3(v[0], v[1], v[2]), sl::dquat(q[0], q[1], q[2], q[3]));
            }
        }

        void place_at_base(Sim &sim, Ship &ship, const kosmos::ScenarioShip &entry, const kosmos::Base &base, int body)
        {
            sl::dvec3 dir;
            double heading = 0.0;
            const double radius = sim.universe.bodies[static_cast<std::size_t>(body)].radius;
            if (entry.runway >= 0 && entry.runway < static_cast<int>(base.runways.size()))
                kosmos::base_runway_start(base, base.runways[static_cast<std::size_t>(entry.runway)], entry.reverse, radius, dir, heading, entry.along);
            else
                kosmos::base_pad(base, radius, dir, heading), heading += ship.spec().pad_turn;
            double height = base.height;
            if (entry.runway < 0 || entry.runway >= static_cast<int>(base.runways.size()))
                height += kosmos::base_pad_deck(base);
            if (entry.altitude > 0.0)
                ship.place_airborne(body, dir, heading, height, entry.altitude, entry.speed);
            else
                ship.place_on_surface(body, dir, heading, height);
        }

        void place_on_ground(Sim &sim, const std::string &scenario_id, Ship &ship, const kosmos::ScenarioShip &entry, int body, const sl::Terrain *terrain)
        {
            const kosmos::Base *base = nullptr;
            for (const kosmos::Base &candidate : sim.bases)
                if (!entry.base.empty() && candidate.id == entry.base && sim.universe.find(candidate.body) == body)
                    base = &candidate;
            if (!entry.base.empty() && !base)
                std::fprintf(stderr, "kosmos: scenario %s: unknown base \"%s\" on %s\n", scenario_id.c_str(), entry.base.c_str(), entry.body.c_str());
            if (base)
            {
                place_at_base(sim, ship, entry, *base, body);
                return;
            }
            double height = 0.0;
            sl::dvec3 dir;
            const int home = sim.universe.home_id();
            if (entry.pad && body == home && terrain)
            {
                dir = choose_pad(*terrain, height, glm::normalize(-sim.universe.position(home)));
                register_pad_beacons(sim, home, dir);
            }
            else
            {
                dir = direction_from_lat_lon(sim.universe, body, entry.latitude, entry.longitude);
                if (terrain)
                    height = terrain->height_at_direction(dir);
            }
            if (entry.altitude > 0.0)
                ship.place_airborne(body, dir, entry.heading, height, entry.altitude, entry.speed);
            else
                ship.place_on_surface(body, dir, entry.heading, height);
        }

        void link_docked_ships(Sim &sim, const kosmos::Scenario &sc, const std::vector<int> &spawned)
        {
            for (std::size_t i = 0; i < sc.ships.size(); ++i)
            {
                const kosmos::ScenarioShip &entry = sc.ships[i];
                if (entry.dock_to < 0 || entry.dock_to >= static_cast<int>(i) || spawned[i] < 0 || spawned[static_cast<std::size_t>(entry.dock_to)] < 0)
                    continue;
                const int leader = spawned[static_cast<std::size_t>(entry.dock_to)];
                if (entry.dock_port < static_cast<int>(sim.ship(leader)->spec().ports.size()) && entry.own_port < static_cast<int>(sim.ship(spawned[i])->spec().ports.size()))
                    sim.nav.link(leader, entry.dock_port, spawned[i], entry.own_port);
            }
        }

        void apply_scenario_settings(Sim &sim, const kosmos::Scenario &sc)
        {
            sim.props.set_string("/sim/target", sc.target);
            for (int i = 0; i < kosmos::NavSystem::receivers; ++i)
                sim.props.set_double("/nav/nav[" + std::to_string(i) + "]/frequency", i < static_cast<int>(sc.nav.size()) ? sc.nav[static_cast<std::size_t>(i)] : 0.0);
            sim.props.set_double("/controls/throttle", sc.throttle);
            const std::size_t flown_entry = static_cast<std::size_t>(std::min<int>(sc.active, static_cast<int>(sc.ships.size()) - 1));
            sim.props.set_double("/controls/gear", sc.ships[flown_entry].landed && sc.ships[flown_entry].altitude <= 0.0 ? 1.0 : 0.0);
            sim.props.set_double("/controls/hover", 0.0);
            sim.props.set_string("/controls/sas", "stability");
            sim.props.set_bool("/autopilot/enabled", sc.autopilot);
            sim.props.set_string("/sim/scenario-mfd-left", sc.mfd_left);
            sim.props.set_string("/sim/enter-building", sc.enter_building);
            sim.props.set_string("/sim/scenario-mfd-right", sc.mfd_right);
            sim.props.set_int("/sim/warp-index", 0);
            apply_sets(sim.props, sc.sets);
        }
    } // namespace

    // Returns the ship to fly, or nullptr (with a message on stderr) if the scenario cannot be built.
    Ship *apply_scenario(Sim &sim, const kosmos::Scenario &sc, const TerrainSet &terrains)
    {
        if (!escape_targets_valid(sim, sc))
            return nullptr;
        sim.clear_ships();
        sim.props.set_string("/sim/weather-force", sc.weather);
        if (!sc.life_support.empty())
            sim.life_support = kosmos::parse_life_support(sc.life_support, sim.life_support);
        std::vector<int> spawned(sc.ships.size(), -1);
        for (std::size_t entry_index = 0; entry_index < sc.ships.size(); ++entry_index)
        {
            const kosmos::ScenarioShip &entry = sc.ships[entry_index];
            const kosmos::ShipSpec *spec = sim.find_ship_spec(entry.ship);
            const int body = sim.universe.find(entry.body);
            if (!spec || body < 0)
            {
                std::fprintf(stderr, "kosmos: scenario %s: unknown %s \"%s\"\n", sc.id.c_str(), spec ? "body" : "ship", spec ? entry.body.c_str() : entry.ship.c_str());
                continue;
            }
            Ship *ship = sim.spawn(*spec);
            spawned[entry_index] = sim.ship_count() - 1;
            if (!entry.label.empty())
                ship->name = entry.label;
            apply_ship_systems(*ship, entry);
            if (!entry.landed || entry.altitude > 0.0 || entry.dock_to >= 0)
                ship->set_gear_position(0.0);
            if (!entry.landed)
                place_in_space(sim, *ship, entry, body);
            else
                place_on_ground(sim, sc.id, *ship, entry, body, terrains.find(body));
        }
        link_docked_ships(sim, sc, spawned);
        if (sim.ship_count() == 0)
            return nullptr;
        sim.activate(std::min(sc.active, sim.ship_count() - 1));
        spawn_buildings(sim);
        spawn_portal(sim);
        for (std::size_t entry_index = 0; entry_index < sc.ships.size(); ++entry_index)
            if (sc.ships[entry_index].portal_m > 0.0 && spawned[entry_index] >= 0)
                place_at_portal(sim, *sim.ship(spawned[entry_index]), sc.ships[entry_index].portal_m, 0.0, false);
        apply_scenario_settings(sim, sc);
        return sim.active_ship();
    }

    // Swings every ship about the planet's axis until the active one is on the Sun side, keeping the fleet's layout intact.
    void rotate_fleet_to_sun(Sim &sim, Ship *flown, double extra_degrees = 0.0)
    {
        const sl::dvec3 here = flown->body().position;
        const int soi = sim.universe.soi_body(here);
        const int sun = sim.universe.find("Sun");
        if (soi < 0 || sun < 0)
            return;
        const sl::FlightBody &planet = *sim.universe.world.body(soi);
        const sl::dvec3 r = here - planet.position, s = sim.universe.world.body(sun)->position - planet.position;
        const sl::dvec3 axis = sim.universe.pole(soi);
        const sl::dvec3 rp = r - axis * glm::dot(r, axis), sp = s - axis * glm::dot(s, axis);
        const double angle = std::atan2(glm::dot(axis, glm::cross(rp, sp)), glm::dot(rp, sp)) - extra_degrees * 3.14159265358979 / 180.0;
        const sl::dquat spin = glm::angleAxis(angle, axis);
        for (int i = 0; i < sim.ship_count(); ++i)
        {
            if (sim.ship(i)->spec().building())
                continue;
            sl::FlightBody &b = sim.ship(i)->body();
            b.position = planet.position + spin * (b.position - planet.position);
            b.velocity = planet.velocity + spin * (b.velocity - planet.velocity);
            b.orientation = spin * b.orientation;
        }
    }

    Ship *start_scenario(Sim &sim, const Options &options, const TerrainSet &terrains, const ShipCarryState *carry_state)
    {
        if (!options.scenario.empty())
            if (const kosmos::Scenario *sc = sim.find_scenario(options.scenario))
            {
                kosmos::Scenario adjusted = *sc;
                // --orbit-alt lifts the circular orbits a scenario sets, so a promo shot can show more of the planet.
                if (options.orbit_altitude > 0.0)
                    for (auto &entry : adjusted.ships)
                        if (!entry.landed && entry.escape_to.empty() && entry.apoapsis_km <= 0.0 && entry.altitude <= 0.0 && !entry.has_state)
                            entry.altitude_km = options.orbit_altitude;
                Ship *flown = apply_scenario(sim, adjusted, terrains);
                if (!flown)
                    return nullptr;
                if (options.sunlit)
                    rotate_fleet_to_sun(sim, flown, options.sun_angle);
                return flown;
            }
        sim.clear_ships();
        const kosmos::ShipSpec *spec = sim.find_ship_spec(options.ship_name);
        if (!spec)
            return nullptr;
        Ship *ship = sim.spawn(*spec);
        const int earth = sim.universe.home_id();
        double arrival_altitude = 0.0;
        const int target = options.orbit_body.empty() ? -1 : options.orbit_body == "auto" ? sim.universe.arrival_body(arrival_altitude) : sim.universe.find(options.orbit_body);
        if (target >= 0)
        {
            const kosmos::Body &b = sim.universe.bodies[static_cast<std::size_t>(target)];
            double phase = options.orbit_phase;
            if (options.orbit_half)
            {
                // Choose the orbit point whose radial direction is perpendicular to the Sun, so the planet is half lit from there.
                const int sun = sim.universe.find("Sun");
                if (sun >= 0)
                {
                    const sl::dvec3 s = sim.universe.world.body(sun)->position - sim.universe.world.body(target)->position;
                    phase += std::atan2(s.x, s.z) * 180.0 / 3.14159265358979;
                }
            }
            sim.props.set_double("/controls/gear", 0.0);
            ship->place_in_orbit(target, options.orbit_altitude > 0.0 ? options.orbit_altitude * km : arrival_altitude > 0.0 ? arrival_altitude : b.radius * 0.5, 0.0, phase);
        }
        else if (options.orbit_start)
        {
            sim.props.set_double("/controls/gear", 0.0);
            ship->place_in_orbit(earth, (options.orbit_altitude > 0.0 ? options.orbit_altitude : 200.0) * km, 0.0);
            // Every other addon ship shares the orbit a few kilometres apart, so there is something to fly to and switch to.
            double phase = 0.02;
            for (const kosmos::Addon &addon : sim.addons())
                for (const kosmos::ShipSpec &other : addon.ships)
                    if (!options.alone && other.name != spec->name)
                    {
                        sim.spawn(other)->place_in_orbit(earth, 200.0 * km, 0.0, phase);
                        phase += 0.02;
                    }
        }
        else
        {
            double height = 0.0;
            sl::dvec3 pad(1.0, 0.0, 0.0);
            double heading = 90.0;
            const kosmos::Base *home_base = nullptr;
            for (const kosmos::Base &candidate : sim.bases)
                if (candidate.id == "chippenham" && sim.universe.find(candidate.body) == earth)
                    home_base = &candidate;
            // Rockets start on a launch pad with its tower whenever the home planet has a base.
            if (home_base)
            {
                kosmos::base_pad(*home_base, sim.universe.bodies[static_cast<std::size_t>(earth)].radius, pad, heading);
                heading += ship->spec().pad_turn;
                height = home_base->height + kosmos::base_pad_deck(*home_base);
            }
            else if (const sl::Terrain *terrain = terrains.find(earth))
            {
                pad = choose_pad(*terrain, height, glm::normalize(-sim.universe.position(earth)));
                register_pad_beacons(sim, earth, pad);
            }
            ship->place_on_surface(earth, pad, heading, height);
        }
        if (options.sunlit && (options.orbit_start || target >= 0))
            rotate_fleet_to_sun(sim, ship, options.sun_angle);
        sim.activate(options.ship_index);
        spawn_portal(sim);
        if (options.portal_arrival)
            place_at_portal(sim, *ship, 400.0, 60.0, true);
        if (carry_state)
            ship->restore_carry_state(*carry_state);
        sim.props.set_double("/controls/throttle", 0.0);
        sim.props.set_double("/controls/hover", 0.0);
        sim.props.set_string("/controls/sas", "stability");
        sim.props.set_bool("/autopilot/enabled", options.autopilot);
        sim.props.set_int("/sim/warp-index", 0);
        return sim.active_ship();
    }
} // namespace kosmos::app
