#include "selftest.h"

#include "ordnance.h"
#include "ship_weapons.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace kosmos
{
    namespace
    {
        constexpr double km = 1000.0;

        /** A headless check of what a person can carry and fire, and what blasts do to ships and people. Returns 0 when all of it holds. */
        int run_arsenal_test(Sim &sim, Ship *ship)
        {
            int failures = 0;
            const auto check = [&](bool ok, const char *what) {
                std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
                failures += ok ? 0 : 1;
            };
            kosmos::Arsenal arsenal;
            kosmos::Astronaut person;
            kosmos::ArsenalContext ctx;
            ctx.sim = &sim;
            ctx.eva = &person;
            ctx.terrain = [](int) -> const sl::Terrain * { return nullptr; };
            const double dt = 0.05;
            // The planet turns under the ship, so the world and the person must advance together.
            const auto step = [&]() {
                sim.step(dt);
                person.carry(sim.universe, nullptr, dt);
                arsenal.update(ctx, dt, dt);
            };
            const auto run = [&](double seconds) {
                for (double t = 0.0; t < seconds; t += dt)
                    step();
            };
            const auto slot_of = [&](const char *id) {
                for (std::size_t i = 0; i < arsenal.pack().size(); ++i)
                    if (arsenal.pack()[i].id == id)
                        return static_cast<int>(i);
                return -1;
            };

            const int body = ship->telemetry().soi;
            // With no terrain loaded the ground is the bare sphere, so stand the ship on it.
            ship->place_on_surface(body, glm::normalize(ship->origin() - sim.universe.position(body)), 90.0, 0.0);
            sim.step(dt);
            const sl::dvec3 spot = ship->origin();
            sl::dvec3 look = glm::normalize(glm::cross(glm::normalize(spot - sim.universe.position(body)), sl::dvec3(0, 1, 0)));
            person.leave(sim.universe, body, spot + look * 6.0, ship->body().velocity, look, nullptr, true);
            person.active = true;
            const sl::dvec3 up = person.local_up();
            const auto aim = [&](const sl::dvec3 &direction) { person.orientation = glm::quatLookAt(glm::normalize(direction), up); };

            arsenal.equip_person(sim, ship);
            check(slot_of("tricorder") >= 0 && slot_of("pistol") >= 0, "standard kit in the pack");
            check(slot_of("plasma_rifle") >= 0 || ship->spec().locker.empty(), "ship locker added to the pack");

            // Pistol: an instant ray into the ground leaves an impact effect.
            arsenal.select(slot_of("pistol"));
            aim(look - up);
            const std::size_t before = arsenal.live_effects();
            arsenal.use_held(ctx, true, true);
            check(arsenal.live_effects() > before, "pistol shot makes muzzle flash and impact effects");
            run(1.0);

            // A held-down trigger fires at the weapon's rate, not every frame.
            arsenal.select(slot_of("pistol"));
            int shots = 0;
            std::size_t effects = arsenal.live_effects();
            for (double t = 0.0; t < 1.0; t += dt)
            {
                arsenal.use_held(ctx, false, true);
                step();
                if (arsenal.live_effects() > effects)
                    ++shots;
                effects = arsenal.live_effects();
            }
            check(shots >= 1 && shots <= 12, "pistol is rate limited");

            // Plasma rifle: a bolt flies, then ends on the ground.
            if (slot_of("plasma_rifle") >= 0)
            {
                run(1.0);
                arsenal.select(slot_of("plasma_rifle"));
                aim(look - up * 0.2);
                arsenal.use_held(ctx, true, true);
                check(arsenal.rounds_in_flight() == 1, "plasma rifle launches a bolt");
                run(4.0);
                check(arsenal.rounds_in_flight() == 0, "plasma bolt ends on impact or at range");
            }

            // A thrown charge: one fewer in the pack, a fuse, then a blast that hurts a person and a ship beside it.
            const int charge = slot_of("frag_charge");
            if (charge >= 0)
            {
                const int count = arsenal.pack()[static_cast<std::size_t>(charge)].count;
                arsenal.select(charge);
                aim(-up);
                run(1.0);
                const double ship_health = ship->health();
                arsenal.use_held(ctx, true, true);
                check(arsenal.pack()[static_cast<std::size_t>(charge)].count == count - 1, "charge leaves the pack when thrown");
                check(arsenal.rounds_in_flight() == 1 && arsenal.detonations() == 0, "charge waits out its fuse");
                run(6.0);
                check(arsenal.detonations() == 1 && arsenal.rounds_in_flight() == 0, "charge detonates after the fuse");
                check(person.suit.health < 1.0, "blast hurts the person next to it");
                check(ship->health() < ship_health, "blast damages a ship within its radius");
            }

            // A bomb dropped from a rack removes mass and eventually detonates.
            if (!ship->spec().hardpoints.empty())
            {
                const int detonations = arsenal.detonations();
                const double mass = ship->telemetry().mass;
                check(arsenal.release(ctx, *ship, 0), "rack releases a bomb");
                step();
                check(ship->telemetry().mass < mass, "payload mass falls when a bomb is released");
                run(10.0);
                check(arsenal.detonations() == detonations + 1, "released bomb detonates");
            }
            std::printf("arsenal selftest: %d failure(s)\n", failures);
            return failures ? 1 : 0;
        }

        /** A headless check of ship weapons: a beam and flak from one ship at another, shields, damage and the light cap. Returns 0 when all of it holds. */
        int run_weapons_test(Sim &sim, Ship *ship)
        {
            int failures = 0;
            const auto check = [&](bool ok, const char *what) {
                std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
                failures += ok ? 0 : 1;
            };
            Arsenal arsenal;
            Astronaut nobody;
            ArsenalContext ctx;
            ctx.sim = &sim;
            ctx.eva = &nobody;
            ctx.terrain = [](int) -> const sl::Terrain * { return nullptr; };
            ShipWeapons weapons;
            const double dt = 0.05;
            const auto run = [&](double seconds) {
                for (double t = 0.0; t < seconds; t += dt)
                {
                    sim.step(dt);
                    weapons.update(ctx, arsenal, dt, dt);
                }
            };
            check(!ship->spec().weapons.empty(), "the flown ship has weapon mounts");
            check(sim.weapon_specs.find("beam-red") && sim.weapon_specs.find("beam-violet") && sim.weapon_specs.find("spark-red") && sim.weapon_specs.find("spark-violet"), "four beam and four sparkle weapons load");
            Ship *target = sim.spawn(ship->spec());
            const sl::dvec3 forward = glm::normalize(ship->body().direction_to_world(sl::dvec3(0, 0, -1)));
            target->body().position = ship->body().position + forward * 400.0;
            target->body().velocity = ship->body().velocity;
            target->body().angular_velocity = sl::dvec3(0.0);
            ship->body().angular_velocity = sl::dvec3(0.0);
            const double capacity = target->spec().shield.capacity;
            check(capacity > 0.0 && target->shield_level() >= capacity - 1.0e-9, "target starts with a full shield");

            sim.props.set_double("/controls/weapons/primary/fire", 1.0);
            run(0.8);
            check(weapons.live_beams() > 0, "beam is drawn while firing");
            check(weapons.hits() > 0, "beam hits the target");
            check(target->shield_level() < capacity, "shield drains first");
            check(target->health() > 0.999, "hull is untouched while the shield holds");
            check(weapons.shield_hits() > 0, "shield flashes are counted");
            check(weapons.live_sparks() > 0, "hits throw sparks");
            std::vector<PlacedLight> lights;
            weapons.collect_lights(sim.universe, ship->origin(), 4, lights);
            check(!lights.empty() && lights.size() <= 4, "weapon lights are made and capped at four");
            lights.clear();
            weapons.collect_lights(sim.universe, ship->origin(), 1, lights);
            check(lights.size() <= 1, "a lower cap is respected");

            target->absorb(10.0);
            check(target->shield_level() <= 0.0, "shield can be emptied");
            run(3.0);
            check(target->health() < 1.0, "hull takes damage once the shield is down");

            sim.props.set_double("/controls/weapons/primary/fire", 0.0);
            sim.props.set_double("/controls/weapons/secondary/fire", 1.0);
            run(0.6);
            check(weapons.shots_fired() > 0 && weapons.live_bolts() > 0, "sparkle volley is in flight");
            sim.props.set_double("/controls/weapons/secondary/fire", 0.0);
            run(1.5);
            check(weapons.live_sparks() > 0 || weapons.live_bolts() == 0, "sparkle shots end in sparks");
            run(10.0);
            check(weapons.live_bolts() == 0 && weapons.live_beams() == 0, "everything ends after the pulse and volley");

            // Another player's ship, for show: it fires the same effects but a ship in the way takes no damage.
            ShipWeapons::GhostShooter ghost;
            ghost.id = 1;
            ghost.spec = &ship->spec();
            ghost.body = ship->telemetry().soi;
            ghost.position = ship->body().position - forward * 400.0;
            ghost.velocity = ship->body().velocity - sim.universe.world.body(ghost.body)->velocity;
            ghost.orientation = ship->body().orientation;
            ghost.fire = 3;
            const double health = ship->health(), shield = ship->shield_level();
            sim.props.set_double("/controls/weapons/secondary/fire", 0.0);
            std::size_t seen = 0;
            for (double t = 0.0; t < 1.0; t += dt)
            {
                sim.step(dt);
                weapons.update(ctx, arsenal, dt, dt);
                weapons.update_ghosts(ctx, arsenal, {ghost}, dt);
                seen = std::max(seen, weapons.live_beams() + weapons.live_bolts());
            }
            check(seen > 0, "a ghost ship's triggers make shots");
            check(ship->health() >= health && ship->shield_level() >= shield, "a ghost ship's shots do no damage");
            return failures ? 1 : 0;
        }
    } // namespace

    int run_selftest(Sim &sim, Ship *ship, const SelftestOptions &options)
    {
        if (options.weapons)
            return run_weapons_test(sim, ship);
        if (options.arsenal)
            return run_arsenal_test(sim, ship);
        const double dt = std::getenv("KOSMOS_SELFTEST_DT") ? std::atof(std::getenv("KOSMOS_SELFTEST_DT")) : 0.05;
        double next_report = 0.0;
        kosmos::Arsenal arsenal;
        kosmos::Astronaut nobody;
        kosmos::ArsenalContext arsenal_ctx;
        arsenal_ctx.sim = &sim;
        arsenal_ctx.eva = &nobody;
        arsenal_ctx.terrain = [](int) -> const sl::Terrain * { return nullptr; };
        bool dropped = false;
        for (double t = 0.0; t < options.seconds; t += dt)
        {
            if (options.drop >= 0.0)
            {
                if (!dropped && t >= options.drop)
                {
                    dropped = true;
                    const int index = options.drop_atom ? static_cast<int>(ship->spec().hardpoints.size()) - 1 : -1;
                    std::printf("release: %d (mass %.0f)\n", arsenal.release(arsenal_ctx, *ship, index) ? 1 : 0, ship->telemetry().mass);
                }
                arsenal.update(arsenal_ctx, dt, dt);
                if (dropped && std::fmod(t, 2.0) < dt)
                    std::printf("   arsenal: rounds %zu effects %zu detonations %d health %.2f mass %.0f\n", arsenal.rounds_in_flight(), arsenal.live_effects(), arsenal.detonations(), ship->health(), ship->telemetry().mass);
            }
            if (options.warp > 0.0 && sim.props.get_string("/autopilot/status") == "Coasting to apoapsis")
                sim.props.set_int("/sim/warp-index", static_cast<int>(options.warp));
            if (ship->spec().aircraft() && options.throttle >= 0.0)
            {
                // A scripted flight: rotate at 40 m/s, then hold a gentle climb.
                const kosmos::Telemetry &now = ship->telemetry();
                sim.props.set_double("/controls/throttle", options.throttle);
                sim.props.set_double("/controls/pitch", now.surface_speed > 40.0 && now.pitch < 8.0 ? 0.4 : (now.pitch > 12.0 ? -0.2 : 0.0));
            }
            if (!ship->spec().aircraft() && options.throttle >= 0.0)
                sim.props.set_double("/controls/throttle", options.throttle);
            if (options.gear_up && ship->telemetry().altitude > 30.0)
                sim.props.set_double("/controls/gear", 0.0);
            sim.step(dt);
            const kosmos::Telemetry &tm = ship->telemetry();
            const double report_every = ship->spec().aircraft() ? 2.0 : 10.0;
            if (sim.universe.time() >= next_report)
            {
                next_report = sim.universe.time() + report_every;
                if (ship->spec().aircraft())
                    std::printf("t=%5.0f ias=%5.1f alt=%6.1f vs=%5.1f pitch=%5.1f aoa=%5.1f slip=%5.1f hdg=%5.1f landed=%d fuel=%4.0f gear=%.2f%s\n", sim.universe.time(), tm.surface_speed,
                                tm.altitude, tm.vertical_speed, tm.pitch, tm.alpha, tm.sideslip, tm.heading, tm.landed ? 1 : 0, tm.stage_fuel, ship->gear_position(), ship->crashed() ? " CRASHED" : "");
                if (ship->spec().aircraft() && sim.props.get_bool("/nav/nav[0]/valid"))
                    std::printf("   nav0: loc %.2f gs %.2f dist %.0f\n", sim.props.get_double("/nav/nav[0]/ils/localizer"), sim.props.get_double("/nav/nav[0]/ils/glideslope"), sim.props.get_double("/nav/nav[0]/distance"));
                if (!ship->spec().aircraft())
                std::printf("t=%6.0f %-24s alt=%8.1f km speed=%7.1f vs=%7.1f ap=%8.1f pe=%8.1f pitch=%5.1f stage=%d fuel=%8.0f m=%7.0f warp=%g%s\n",
                            sim.universe.time(), sim.props.get_string("/autopilot/status").c_str(), tm.altitude / km, tm.surface_speed,
                            tm.vertical_speed, tm.apoapsis / km, tm.periapsis / km, tm.pitch, ship->active_stage(), tm.stage_fuel,
                            tm.mass, sim.current_warp(), ship->crashed() ? " CRASHED" : "");
                if (sim.props.get_string("/environment/station") != "")
                    std::printf("   wx: %s wind %.1f from %.0f rain %.2f cloud %.2f vis %.0f wet %.2f | %s\n", sim.props.get_string("/environment/station").c_str(),
                                sim.props.get_double("/environment/wind-speed"), sim.props.get_double("/environment/wind-from"), sim.props.get_double("/environment/rain"),
                                sim.props.get_double("/environment/cloud-cover"), sim.props.get_double("/environment/visibility"),
                                sim.props.get_double("/environment/wetness"), sim.props.get_string("/environment/metar").c_str());
                if (sim.props.get_bool("/dock/valid"))
                    std::printf("   dock: range %.2f closing %.2f docked %d\n", sim.props.get_double("/dock/range"),
                                sim.props.get_double("/dock/closing-speed"), sim.props.get_bool("/dock/docked") ? 1 : 0);
            }
            if (ship->crashed())
                break;
        }
        const kosmos::Telemetry &tm = ship->telemetry();
        std::printf("final: alt %.1f km, apoapsis %.1f km, periapsis %.1f km, ecc %.4f, crashed %d, soi %s\n", tm.altitude / km,
                    tm.apoapsis / km, tm.periapsis / km, tm.eccentricity, ship->crashed(), sim.universe.bodies[static_cast<std::size_t>(tm.soi)].name.c_str());
        return 0;
    }
} // namespace kosmos
