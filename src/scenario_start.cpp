#include "scenario_start.h"

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

    // Returns the ship to fly, or nullptr (with a message on stderr) if the scenario cannot be built.
    Ship *apply_scenario(Sim &sim, const kosmos::Scenario &sc, const TerrainSet &terrains)
    {
        sim.clear_ships();
        sim.props.set_string("/sim/weather-force", sc.weather);
        if (!sc.life_support.empty())
            sim.life_support = kosmos::parse_life_support(sc.life_support, sim.life_support);
        const int home = sim.universe.home_id();
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
            const sl::Terrain *terrain = terrains.find(body);
            Ship *ship = sim.spawn(*spec);
            spawned[entry_index] = sim.ship_count() - 1;
            if (!entry.label.empty())
                ship->name = entry.label;
            if (kosmos::Systems *systems = ship->systems())
            {
                for (const auto &tank : entry.tanks)
                    systems->set_tank_fraction(tank.first, tank.second);
                if (entry.battery >= 0.0)
                    systems->set_battery_fraction(entry.battery);
                if (entry.leak_cm2 > 0.0)
                    systems->breach(0, entry.leak_cm2);
            }
            if (!entry.landed)
            {
                if (entry.apoapsis_km > 0.0)
                    ship->place_on_ellipse(body, entry.altitude_km * km, entry.apoapsis_km * km, entry.phase, entry.toward.empty() ? -1 : sim.universe.find(entry.toward));
                else
                    ship->place_in_orbit(body, entry.altitude_km * km, entry.inclination, entry.phase);
            }
            else
            {
                double height = 0.0;
                sl::dvec3 dir;
                const kosmos::Base *base = nullptr;
                for (const kosmos::Base &candidate : sim.bases)
                    if (!entry.base.empty() && candidate.id == entry.base && sim.universe.find(candidate.body) == body)
                        base = &candidate;
                if (!entry.base.empty() && !base)
                    std::fprintf(stderr, "kosmos: scenario %s: unknown base \"%s\" on %s\n", sc.id.c_str(), entry.base.c_str(), entry.body.c_str());
                if (base)
                {
                    double heading = 0.0;
                    const double radius = sim.universe.bodies[static_cast<std::size_t>(body)].radius;
                    if (entry.runway >= 0 && entry.runway < static_cast<int>(base->runways.size()))
                        kosmos::base_runway_start(*base, base->runways[static_cast<std::size_t>(entry.runway)], entry.reverse, radius, dir, heading, entry.along);
                    else
                        kosmos::base_pad(*base, radius, dir, heading), heading += ship->spec().pad_turn;
                    height = base->height;
                    if (entry.runway < 0 || entry.runway >= static_cast<int>(base->runways.size()))
                        height += kosmos::base_pad_deck(*base);
                    if (entry.altitude > 0.0)
                        ship->place_airborne(body, dir, heading, height, entry.altitude, entry.speed);
                    else
                        ship->place_on_surface(body, dir, heading, height);
                    continue;
                }
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
                ship->place_on_surface(body, dir, entry.heading, height);
            }
        }
        for (std::size_t i = 0; i < sc.ships.size(); ++i)
        {
            const kosmos::ScenarioShip &entry = sc.ships[i];
            if (entry.dock_to < 0 || entry.dock_to >= static_cast<int>(i) || spawned[i] < 0 || spawned[static_cast<std::size_t>(entry.dock_to)] < 0)
                continue;
            const int leader = spawned[static_cast<std::size_t>(entry.dock_to)];
            if (entry.dock_port < static_cast<int>(sim.ship(leader)->spec().ports.size()) && entry.own_port < static_cast<int>(sim.ship(spawned[i])->spec().ports.size()))
                sim.nav.link(leader, entry.dock_port, spawned[i], entry.own_port);
        }
        if (sim.ship_count() == 0)
            return nullptr;
        sim.activate(std::min(sc.active, sim.ship_count() - 1));
        spawn_buildings(sim);
        sim.props.set_string("/sim/target", sc.target);
        for (int i = 0; i < kosmos::NavSystem::receivers; ++i)
            sim.props.set_double("/nav/nav[" + std::to_string(i) + "]/frequency", i < static_cast<int>(sc.nav.size()) ? sc.nav[static_cast<std::size_t>(i)] : 0.0);
        sim.props.set_double("/controls/throttle", sc.throttle);
        const std::size_t flown_entry = static_cast<std::size_t>(std::min<int>(sc.active, static_cast<int>(sc.ships.size()) - 1));
        sim.props.set_double("/controls/gear", sc.ships[flown_entry].landed && sc.ships[flown_entry].altitude > 0.0 ? 0.0 : 1.0);
        sim.props.set_double("/controls/hover", 0.0);
        sim.props.set_string("/controls/sas", "stability");
        sim.props.set_bool("/autopilot/enabled", sc.autopilot);
        sim.props.set_string("/sim/scenario-mfd-left", sc.mfd_left);
        sim.props.set_string("/sim/enter-building", sc.enter_building);
        sim.props.set_string("/sim/scenario-mfd-right", sc.mfd_right);
        sim.props.set_int("/sim/warp-index", 0);
        return sim.active_ship();
    }

    // Swings every ship about the planet's axis until the active one is on the Sun side, keeping the fleet's layout intact.
    void rotate_fleet_to_sun(Sim &sim, Ship *flown)
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
        const double angle = std::atan2(glm::dot(axis, glm::cross(rp, sp)), glm::dot(rp, sp));
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

    Ship *start_scenario(Sim &sim, const Options &options, const TerrainSet &terrains)
    {
        if (!options.scenario.empty())
            if (const kosmos::Scenario *sc = sim.find_scenario(options.scenario))
                if (Ship *flown = apply_scenario(sim, *sc, terrains))
                {
                    if (options.sunlit)
                        rotate_fleet_to_sun(sim, flown);
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
            ship->place_in_orbit(target, options.orbit_altitude > 0.0 ? options.orbit_altitude * km : arrival_altitude > 0.0 ? arrival_altitude : b.radius * 0.5, 0.0, phase);
        }
        else if (options.orbit_start)
        {
            ship->place_in_orbit(earth, 200.0 * km, 0.0);
            // Every other addon ship shares the orbit a few kilometres apart, so there is something to fly to and switch to.
            double phase = 0.02;
            for (const kosmos::Addon &addon : sim.addons())
                for (const kosmos::ShipSpec &other : addon.ships)
                    if (other.name != spec->name)
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
        sim.activate(options.ship_index);
        sim.props.set_double("/controls/throttle", 0.0);
        sim.props.set_double("/controls/hover", 0.0);
        sim.props.set_string("/controls/sas", "stability");
        sim.props.set_bool("/autopilot/enabled", options.autopilot);
        sim.props.set_int("/sim/warp-index", 0);
        return sim.active_ship();
    }
} // namespace kosmos::app
