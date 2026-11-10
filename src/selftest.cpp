#include "selftest.h"

#include "eva_solids.h"
#include "gui_input.h"
#include "flight_recording.h"
#include "landing_descent.h"
#include "landing_forecast.h"
#include "landing_plane.h"
#include "menu.h"
#include "ordnance.h"
#include "scenario_start.h"
#include "settings.h"
#include "ship_weapons.h"
#include "terrain_warning.h"

#include <imgui.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <glm/gtc/quaternion.hpp>

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
            check(weapons.live_flares() > 0, "hits on a ship flash");
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

    static int run_timed_burn_test(Sim &sim, Ship *ship)
    {
        int failures = 0;
        const auto check = [&](bool ok, const char *what) {
            std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
            failures += ok ? 0 : 1;
        };
        const std::string recording_path = sim.props.get_string("/selftest/flight-recording-path");
        if (!recording_path.empty())
        {
            FlightRecording recorded;
            FlightRecordingFrame first;
            first.scenario = capture_scenario(sim, "Recording test");
            first.throttle = 0.25;
            first.hover = 0.5;
            first.axes[0] = -0.75;
            first.sas = "retrograde";
            first.camera_yaw = 0.3;
            first.camera_pitch = -0.2;
            first.camera_distance = 42.0;
            first.cockpit = true;
            recorded.frames.push_back(first);
            first.elapsed = 1.0;
            first.scenario.epoch_jd += 1.0 / 86400.0;
            first.camera_distance = 100.0;
            recorded.frames.push_back(first);
            std::string error;
            const bool saved = save_flight_recording(recording_path, recorded, error);
            if (!saved)
                std::fprintf(stderr, "flight recording selftest: %s\n", error.c_str());
            FlightRecording loaded;
            const bool opened = saved && load_flight_recording(recording_path, loaded, error);
            if (!opened)
                std::fprintf(stderr, "flight recording selftest: %s\n", error.c_str());
            check(opened && loaded.frames.size() == 2 && loaded.frames[1].elapsed == 1.0 &&
                      loaded.frames[0].throttle == 0.25 && loaded.frames[0].hover == 0.5 &&
                      loaded.frames[0].axes[0] == -0.75 && loaded.frames[0].sas == "retrograde" &&
                      loaded.frames[0].cockpit && loaded.frames[1].camera_distance == 100.0 &&
                      scenario_to_json(loaded.frames[0].scenario) == scenario_to_json(recorded.frames[0].scenario),
                  "flight recording persists frames, controls, camera and scenario");
            recorded.frames[1].elapsed = 0.0;
            check(!save_flight_recording(recording_path, recorded, error), "flight recording rejects non-increasing time");
        }
        const std::string bad_recording = sim.props.get_string("/selftest/invalid-flight-recording");
        if (!bad_recording.empty())
        {
            FlightRecording loaded;
            std::string error;
            check(!load_flight_recording(bad_recording, loaded, error) && !error.empty(),
                  "invalid flight recording is rejected with a reason");
        }
        const sl::dvec3 up(1, 0, 0), east(0, 0, 1);
        const auto ridge = [](const sl::dvec3 &direction) { return direction.z > 0.00015 ? 150.0 : 0.0; };
        check(terrain_warning_time(up, east * 100.0, 1e6, 100.0, 0.0, ridge) == 2.0,
              "sampled terrain warning detects a rising ridge");
        check(terrain_warning_time(up, -east * 100.0, 1e6, 100.0, 0.0, ridge) == 0.0,
              "sampled terrain warning leaves a clear track clear");
        check(terrain_warning_time(up, sl::dvec3(0), 1e6, 10.0, -1.0, ridge) == 10.0,
              "sampled terrain warning detects vertical descent");
        check(sim.props.get_double("/ships/ship[" + std::to_string(sim.active_index()) + "]/terrain-warning-seconds") == -1.0,
              "terrain warning reports unavailable when no terrain is loaded");
        {
            sl::TerrainConfig config;
            config.shape = sl::TerrainShape::sphere;
            config.worker_threads = 0;
            config.radius = sim.universe.bodies[static_cast<std::size_t>(ship->telemetry().soi)].radius;
            auto source = std::make_shared<sl::FunctionSource>([](double, double) { return 0.0; }, 0.0, 0.0);
            sl::Terrain terrain(config, source);
            sl::FlightBody *planet = sim.universe.world.body(ship->telemetry().soi);
            const sl::Terrain *previous = planet->terrain;
            planet->terrain = &terrain;
            check(sim.props.get_double("/ships/ship[" + std::to_string(sim.active_index()) + "]/terrain-warning-seconds") == 0.0,
                  "terrain warning uses available clear terrain");
            planet->terrain = previous;
        }
        sim.step(0.05);
        check(std::abs(capture_scenario(sim, "Date test").epoch_jd - sim.universe.julian_date()) < 1.0e-9,
              "captured scenario retains current date");
        const int orbitcomp_mode = sim.mfd.mode_by_id("orbitcomp");
        check(orbitcomp_mode >= 0, "OrbitComp available");
        if (orbitcomp_mode >= 0)
        {
            const sol::table &table = sim.mfd.modes()[static_cast<std::size_t>(orbitcomp_mode)].table;
            const sol::table hooks = table["_test"];
            sol::protected_function format = hooks["approach_text"];
            const auto approach_case = [&](double distance, const char *kind, double radius,
                                           const char *expected, const char *name) {
                sim.lua.execute("");
                sol::protected_function_result result = format(distance, kind, radius);
                if (!result.valid())
                {
                    const sol::error error = result;
                    std::fprintf(stderr, "orbitcomp approach selftest: %s\n", error.what());
                    check(false, name);
                    return;
                }
                check(result.get<std::string>() == expected, name);
            };
            const double mars_radius = 3389500.0;
            approach_case(mars_radius + 200000, "body", mars_radius, "Alt 200.0 km", "body approach above reference radius");
            approach_case(200000, "body", mars_radius, "Alt -3.2 Mm", "body approach inside reference radius");
            approach_case(mars_radius, "body", mars_radius, "Alt 0 m", "body approach at reference radius");
            approach_case(200000, "ship", mars_radius, "Dist 200.0 km", "ship approach remains separation");
            approach_case(200000, "body", 0, "Ctr 200.0 km (no radius)", "missing body radius is explicit");
        }
        for (const char *id : {"radar-altimeter", "orbit", "surface"})
        {
            const int mode = sim.mfd.mode_by_id(id);
            bool drawn = false, labelled = false;
            if (mode >= 0)
            {
                const sol::table &table = sim.mfd.modes()[static_cast<std::size_t>(mode)].table;
                sol::protected_function draw = table["draw"];
                MfdCanvas canvas;
                sol::protected_function_result result = draw(table, std::ref(canvas), mfd_size, mfd_size);
                drawn = result.valid();
                for (const MfdCommand &command : canvas.commands)
                    if (command.kind == MfdCommand::Kind::text &&
                        command.text.rfind(std::string(id) == "orbit" ? "Orb " : "AGL ", 0) == 0)
                        labelled = true;
            }
            check(drawn && labelled, id);
        }
        const int radar_mode = sim.mfd.mode_by_id("radar-altimeter");
        if (radar_mode >= 0)
        {
            const sol::table &table = sim.mfd.modes()[static_cast<std::size_t>(radar_mode)].table;
            const sol::table hooks = table["_test"];
            sol::protected_function warning = hooks["descent_warning"];
            const auto warning_case = [&](double altitude, double vertical, const char *expected, const char *name) {
                sim.lua.execute("");
                sol::protected_function_result result = warning(altitude, vertical);
                if (!result.valid())
                {
                    const sol::error error = result;
                    std::fprintf(stderr, "radar warning selftest: %s\n", error.what());
                    check(false, name);
                    return;
                }
                const sol::optional<std::string> text = result;
                check(expected ? text && *text == expected : !text, name);
            };
            warning_case(301, -10, nullptr, "terrain warning outside 30 seconds");
            warning_case(300, -10, "GROUND: within 30 seconds", "terrain warning 30 seconds");
            warning_case(100, -10, "GROUND: within 10 seconds", "terrain warning 10 seconds");
            warning_case(0, -10, "GROUND: within 10 seconds", "terrain warning zero clearance");
            warning_case(10, -0.4, "GROUND: within 30 seconds", "terrain warning slow descent");
            warning_case(100, 1, nullptr, "terrain warning clear when climbing");
        }
        const int sync_mode = sim.mfd.mode_by_id("sync");
        check(sync_mode >= 0, "Sync Orbit available");
        if (sync_mode >= 0)
        {
            const sol::table &table = sim.mfd.modes()[static_cast<std::size_t>(sync_mode)].table;
            const sol::table hooks = table["_test"];
            sol::protected_function linear = hooks["linear_approach"];
            const auto approach_case = [&](double rx, double ry, double vx, double vy,
                                           double closing, double lateral, double time, double miss, const char *name) {
                sim.lua.execute("");
                sol::protected_function_result result = linear(rx, ry, 0.0, vx, vy, 0.0);
                if (!result.valid())
                {
                    const sol::error error = result;
                    std::fprintf(stderr, "sync approach selftest: %s\n", error.what());
                    check(false, name);
                    return;
                }
                const sol::table values = result;
                const auto near = [](double a, double b) { return std::abs(a - b) < 1e-6; };
                check(near(values.get<double>("closing"), closing) &&
                      near(values.get<double>("lateral"), lateral) &&
                      near(values.get<double>("time"), time) &&
                      near(values.get<double>("miss"), miss), name);
            };
            approach_case(100, 0, -10, 0, 10, 0, 10, 0, "approach head-on");
            approach_case(100, 0, -10, 10, 10, 10, 5, std::sqrt(5000.0), "approach with sideways drift");
            approach_case(100, 0, 10, 0, -10, 0, 0, 100, "approach separating");
            approach_case(100, 0, 0, 0, 0, 0, 0, 100, "approach stationary");
            sol::protected_function button = table["button"];
            sol::protected_function draw = table["draw"];
            sim.lua.execute("");
            sol::protected_function_result toggled = button(table, 5);
            MfdCanvas canvas;
            sim.lua.execute("");
            sol::protected_function_result drawn = draw(table, std::ref(canvas), mfd_size, mfd_size);
            const bool target_ship = sim.props.get_bool("/target/valid") && sim.props.get_string("/target/kind") == "ship";
            bool expected_text = false;
            for (const MfdCommand &command : canvas.commands)
                if (command.kind == MfdCommand::Kind::text &&
                    command.text.find(target_ship ? "Sideways speed" : "Select a target ship") != std::string::npos)
                    expected_text = true;
            check(toggled.valid() && drawn.valid() && expected_text, "Sync approach display");
            if (target_ship)
            {
                Ship *target = nullptr;
                for (int i = 0; i < sim.ship_count(); ++i)
                    if (sim.ship(i) != ship && sim.ship(i)->name == sim.props.get_string("/sim/target"))
                        target = sim.ship(i);
                check(target != nullptr, "Sync target ship found");
                if (target)
                {
                    const sl::dvec3 position = target->body().position, velocity = target->body().velocity;
                    Controls relative_hold;
                    relative_hold.sas = "target-retrograde";
                    relative_hold.target_velocity_valid = true;
                    relative_hold.target_relative_velocity = sl::dvec3(3, -4, 0);
                    bool valid = false;
                    const sl::dvec3 direction = ship->sas_direction(relative_hold, valid);
                    check(valid && glm::length(direction - sl::dvec3(-0.6, 0.8, 0.0)) < 1e-9,
                          "target retrograde opposes relative velocity");
                    relative_hold.target_relative_velocity = sl::dvec3(0.01, 0, 0);
                    ship->sas_direction(relative_hold, valid);
                    check(!valid, "target retrograde rejects near-zero speed");
                    relative_hold.target_velocity_valid = false;
                    relative_hold.target_relative_velocity = sl::dvec3(3, 0, 0);
                    ship->sas_direction(relative_hold, valid);
                    check(!valid, "target retrograde rejects unavailable target");
                    const double target_speed = sim.props.get_double("/target/relative-speed");
                    sim.props.set_double("/target/relative-speed", 0.01);
                    sim.lua.execute("");
                    sol::protected_function_result action = button(table, 7);
                    check(action.valid() && sim.props.get_string("/controls/sas") != "target-retrograde",
                          "RVEL rejects near-zero relative speed");
                    sim.props.set_double("/target/relative-speed", target_speed);
                    sim.props.set_bool("/target/same-soi", false);
                    sim.lua.execute("");
                    action = button(table, 7);
                    check(action.valid() && sim.props.get_string("/controls/sas") != "target-retrograde",
                          "RVEL rejects a target in a different SOI");
                    sim.props.set_bool("/target/same-soi", true);
                    sim.props.set_bool("/autopilot/timed/armed", true);
                    sim.lua.execute("");
                    action = button(table, 7);
                    check(action.valid() && sim.props.get_string("/controls/sas") != "target-retrograde",
                          "RVEL does not interrupt an autopilot");
                    sim.props.set_bool("/autopilot/timed/armed", false);
                    sim.lua.execute("");
                    action = button(table, 7);
                    check(action.valid() && sim.props.get_string("/controls/sas") == "target-retrograde",
                          "RVEL selects target retrograde");
                    target->body().position = ship->body().position + sl::dvec3(1000, 0, 0);
                    const sl::dvec3 initial_nose = ship->body().direction_to_world({0, 0, -1});
                    sl::dvec3 side = glm::cross(initial_nose, sl::dvec3(0, 1, 0));
                    if (glm::length(side) < 0.1)
                        side = glm::cross(initial_nose, sl::dvec3(1, 0, 0));
                    side = glm::normalize(side);
                    target->body().velocity = ship->body().velocity + side * 10.0;
                    sim.props.set_string("/controls/rcs-mode", "rot");
                    for (int i = 0; i < 300; ++i)
                        sim.step(0.1);
                    const sl::dvec3 relative = target->body().velocity - ship->body().velocity;
                    const sl::dvec3 nose = ship->body().direction_to_world({0, 0, -1});
                    check(glm::length(relative) > 0.1 && glm::dot(nose, glm::normalize(relative)) > 0.90,
                          "RVEL turns toward target-relative retrograde in flight");
                    const double relative_before = glm::length(relative);
                    sim.props.set_double("/controls/throttle", 0.1);
                    for (int i = 0; i < 10; ++i)
                        sim.step(0.1);
                    sim.props.set_double("/controls/throttle", 0.0);
                    check(glm::length(target->body().velocity - ship->body().velocity) < relative_before - 0.2,
                          "manual burn reduces target-relative speed");
                    sim.props.set_string("/controls/sas", "stability");
                    const auto display_contains = [&](double distance, double speed, const char *text) {
                        target->body().position = ship->body().position + sl::dvec3(distance, 0, 0);
                        target->body().velocity = ship->body().velocity + sl::dvec3(speed, 0, 0);
                        MfdCanvas c;
                        sim.lua.execute("");
                        sol::protected_function_result result = draw(table, std::ref(c), mfd_size, mfd_size);
                        if (!result.valid())
                        {
                            const sol::error error = result;
                            std::fprintf(stderr, "sync approach draw: %s\n", error.what());
                            return false;
                        }
                        for (const MfdCommand &command : c.commands)
                            if (command.kind == MfdCommand::Kind::text && command.text.find(text) != std::string::npos)
                                return true;
                        return false;
                    };
                    check(display_contains(1000, -10, "Linear closest  0 m"), "Sync displays linear closest approach");
                    check(display_contains(1000, 10, "Not approaching"), "Sync warns when separating");
                    check(display_contains(100000, -1000, "Linear closest"), "Sync accepts range boundary");
                    check(display_contains(100001, -1000, "Straight-line estimate unavailable"), "Sync limits range");
                    check(display_contains(900, -3, "Linear closest"), "Sync accepts time boundary");
                    check(display_contains(1000, -1, "Straight-line estimate unavailable"), "Sync limits prediction time");
                    const bool same_soi = sim.props.get_bool("/target/same-soi");
                    sim.props.set_bool("/target/same-soi", false);
                    check(display_contains(1000, -10, "Straight-line estimate unavailable"), "Sync requires the same SOI");
                    sim.props.set_bool("/target/same-soi", same_soi);
                    if (!ship->spec().ports.empty() && !target->spec().ports.empty())
                    {
                        const sl::dquat original_attitude = target->body().orientation;
                        ship->body().angular_velocity = sl::dvec3(0);
                        target->body().angular_velocity = sl::dvec3(0);
                        sl::dvec3 own_port, own_direction, own_up, target_port, target_direction, target_up;
                        ship->port_world(0, own_port, own_direction, own_up);
                        target->body().orientation = glm::angleAxis(glm::pi<double>(), own_up) * ship->body().orientation;
                        target->port_world(0, target_port, target_direction, target_up);
                        target->body().position += own_port + own_direction * 10.0 - target_port;
                        target->body().velocity = ship->body().velocity - own_direction * 0.3;
                        sim.props.set_string("/controls/sas", "off");
                        sim.props.set_double("/nav/nav[0]/frequency", target->spec().ports[0].frequency);
                        sim.step(0.1);
                        check(sim.props.get_bool("/dock/valid") &&
                                  std::abs(sim.props.get_double("/dock/range") - 10.0) < 1.0,
                              "Docking guidance acquires nearby aligned target port");
                        for (int i = 0; i < 400 && !sim.nav.docked(sim.active_index()); ++i)
                            sim.step(0.1);
                        check(sim.nav.docked(sim.active_index()), "aligned slow final approach captures docking ports");
                        if (sim.nav.docked(sim.active_index()))
                            sim.nav.clear_links();
                        target->body().orientation = original_attitude;
                    }
                    target->body().position = position;
                    target->body().velocity = velocity;
                }
            }
            sim.lua.execute("");
            toggled = button(table, 5);
            check(toggled.valid(), "Sync approach returns to orbit page");
        }
        check(!sim.arm_timed_burn(-1.0, 1.0, 0.5, "main", "stability"), "negative countdown rejected");
        check(!sim.arm_timed_burn(1.0, 0.0, 0.5, "main", "stability"), "zero duration rejected");
        check(!sim.arm_timed_burn(1.0, 1.0, 1.1, "main", "stability"), "invalid throttle rejected");
        check(!sim.arm_timed_burn(1.0, 1.0, 0.5, "invalid", "stability"), "unknown engine rejected");
        sim.props.set_double("/controls/rcs-x", 1.0);
        check(!sim.arm_timed_burn(1.0, 1.0, 0.5, "main", "stability"), "manual translation prevents arming");
        sim.props.set_double("/controls/rcs-x", 0.0);
        double jd = 0.0;
        check(parse_burn_date("2000-01-02 12:00:00 UTC", jd) && std::abs(jd - 2451546.0) < 1e-8, "UTC ignition date parsed");
        check(parse_burn_date("51545.5", jd) && std::abs(jd - 2451546.0) < 1e-8, "MJD ignition date parsed");
        check(!parse_burn_date("2025-02-29 12:00 UTC", jd) && !parse_burn_date("2024-01-01 24:15 UTC", jd) &&
                  !parse_burn_date("nan", jd) && !parse_burn_date("2024-01-01 12:00+01:00", jd),
              "invalid dates and non-UTC offsets rejected");
        for (const char *engine : {"main", "hover"})
        {
            const double fuel = ship->telemetry().stage_fuel;
            const double start = sim.universe.time();
            check(sim.arm_timed_burn(1.0, 2.0, 0.5, engine, "stability"), "burn arms");
            std::string why;
            check(!sim.skip_time(10.0, why), "time skip refused while armed");
            double first = -1.0, last = -1.0;
            bool levels_correct = true;
            for (int i = 0; i < 80; ++i)
            {
                sim.step(0.05);
                const double level = sim.props.get_double(std::string("/controls/") + (std::string(engine) == "main" ? "throttle" : "hover"));
                const double other = sim.props.get_double(std::string("/controls/") + (std::string(engine) == "main" ? "hover" : "throttle"));
                if (level > 0.0)
                {
                    if (first < 0.0)
                        first = sim.universe.time();
                    last = sim.universe.time();
                    levels_correct = levels_correct && std::abs(level - 0.5) < 1e-9 && other == 0.0;
                }
            }
            check(first >= start + 1.0 && first <= start + 1.11 && last >= start + 2.95 && last <= start + 3.06,
                  "ignition and cutoff match countdown and duration");
            check(levels_correct && first >= 0.0 && ship->telemetry().stage_fuel < fuel, "selected engine burns at requested throttle and consumes fuel");
            check(!sim.props.get_bool("/autopilot/timed/armed") && sim.props.get_string("/autopilot/timed/state") == "Timed burn complete" &&
                      sim.props.get_double("/controls/throttle") == 0.0 && sim.props.get_double("/controls/hover") == 0.0,
                  "completion cuts both throttles");
        }
        check(sim.arm_timed_burn(1.0, 2.0, 0.5, "main", "stability"), "manual override test arms");
        sim.props.set_double("/controls/throttle", 0.2);
        sim.step(0.05);
        check(!sim.props.get_bool("/autopilot/timed/armed") && sim.props.get_string("/autopilot/timed/state") == "Timed burn cancelled: manual control changed",
              "external throttle change cancels");
        check(sim.arm_timed_burn(0.0, 2.0, 0.5, "hover", "stability"), "active cancellation test arms");
        sim.step(0.05);
        check(sim.props.get_double("/controls/hover") == 0.5, "active cancellation test ignites");
        sim.cancel_timed_burn("Timed burn cancelled");
        check(sim.props.get_double("/controls/hover") == 0.0 && !sim.props.get_bool("/autopilot/timed/armed"), "cancellation cuts active hover burn");
        check(sim.arm_timed_burn(1.0, 2.0, 0.5, "main", "stability"), "autopilot conflict test arms");
        sim.props.set_bool("/autopilot/enabled", true);
        sim.step(0.05);
        check(!sim.props.get_bool("/autopilot/timed/armed"), "another autopilot cancels timed burn");
        sim.props.set_bool("/autopilot/enabled", false);
        check(sim.arm_timed_burn(0.0, 2.0, 0.5, "main", "retrograde"), "alignment test arms");
        bool valid = false;
        Controls controls;
        controls.sas = "retrograde";
        const sl::dvec3 target = ship->sas_direction(controls, valid);
        if (valid)
        {
            const sl::dvec3 reference = std::abs(target.y) < 0.9 ? sl::dvec3(0, 1, 0) : sl::dvec3(1, 0, 0);
            ship->body().orientation = glm::quatLookAt(-target, reference);
        }
        sim.step(0.05);
        check(!sim.props.get_bool("/autopilot/timed/armed") && sim.props.get_string("/autopilot/timed/state") == "Timed burn aborted: attitude not aligned",
              "unaligned ignition aborts");
        std::printf("timed burn selftest: %d failure(s)\n", failures);
        return failures ? 1 : 0;
    }

    static int run_ui_windows_test(Sim &sim)
    {
        int failures = 0;
        const auto check = [&](bool ok, const char *what) {
            std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
            failures += ok ? 0 : 1;
        };
        MenuBar bar;
        bool tool = false;
        const std::vector<MenuWindow> windows = {{"tool", "", &tool}, {"empty", "", nullptr}};
        check(!bar.has_open_window(windows), "no windows leaves EVA look enabled");
        tool = true;
        check(bar.has_open_window(windows), "registered window releases EVA mouse");
        tool = false;
        for (bool *flag : {&bar.scenario_editor_open, &bar.base_editor_open, &bar.help_open, &bar.about_open,
                           &bar.aircraft_autopilot_open, &bar.spacecraft_autopilot_open, &bar.transfer_windows_open})
        {
            *flag = true;
            check(bar.has_open_window(windows), "menu window releases EVA mouse");
            *flag = false;
        }
        bar.dialog.show(FileDialog::Mode::open, "test", ".", "", {});
        check(bar.has_open_window(windows), "file dialog releases EVA mouse");
        bar.dialog.close();
        check(!bar.has_open_window(windows), "closing windows restores EVA look");
        check(app::gui_input_release_event(sl::Event::Type::mouse_button_up) &&
                  app::gui_input_release_event(sl::Event::Type::key_up) &&
                  app::gui_input_release_event(sl::Event::Type::window),
              "transition forwards input releases and focus changes");
        check(!app::gui_input_release_event(sl::Event::Type::mouse_button_down) &&
                  !app::gui_input_release_event(sl::Event::Type::key_down) &&
                  !app::gui_input_release_event(sl::Event::Type::mouse_wheel),
              "transition does not replay clicks or keystrokes");
        {
            bool window_open = true, panel_open = true;
            const auto dismiss = [&]() {
                if (!window_open)
                    return false;
                window_open = false;
                return true;
            };
            const auto close_panel = [&]() { panel_open = false; };
            check(!app::handle_escape(false, dismiss, close_panel) && !window_open && panel_open,
                  "escape closes a window before a habitat panel");
            check(!app::handle_escape(false, dismiss, close_panel) && !panel_open,
                  "escape returns from a habitat panel");
            check(!app::handle_escape(false, dismiss, close_panel),
                  "escape does not quit with nothing open");
            window_open = panel_open = true;
            check(app::handle_escape(true, dismiss, close_panel) && window_open && panel_open,
                  "control escape quits without dismissing UI");
        }

        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(800, 600);
        io.DeltaTime = 1.0f / 60.0f;
        unsigned char *pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        MenuBar auto_bar;
        auto_bar.keep_visible = false;
        std::vector<MenuWindow> menu_windows;
        MenuActions actions;
        ImVec2 combo_pos, choice_pos;
        bool choice_shown = false;
        int selected = 0;
        const auto frame = [&]() {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(300, 180), ImGuiCond_Always);
            ImGui::Begin("Dropdown test");
            if (ImGui::BeginCombo("Choice", selected == 0 ? "First" : "Second"))
            {
                if (ImGui::Selectable("First", selected == 0))
                    selected = 0;
                if (ImGui::Selectable("Second", selected == 1))
                    selected = 1;
                const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                choice_pos = ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
                choice_shown = true;
                ImGui::EndCombo();
            }
            const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            combo_pos = ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
            ImGui::End();
            auto_bar.draw(sim, menu_windows, actions);
            ImGui::Render();
        };
        io.AddMousePosEvent(200, 200);
        frame();
        io.AddMousePosEvent(combo_pos.x, combo_pos.y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        check(choice_shown, "dropdown opens with autohide enabled");
        choice_shown = false;
        frame();
        check(choice_shown && !auto_bar.visible(), "dropdown stays open without revealing the menu bar");
        io.AddMousePosEvent(5, 0);
        for (int i = 0; i < 15; ++i)
        {
            choice_shown = false;
            frame();
        }
        check(choice_shown && auto_bar.visible(), "revealing the menu bar does not steal focus from a dropdown");
        io.AddMousePosEvent(choice_pos.x, choice_pos.y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        check(selected == 1, "dropdown selection works with autohide enabled");

        io.AddMousePosEvent(400, 400);
        for (int i = 0; i < 15; ++i)
            frame();
        check(!auto_bar.visible(), "menu bar hides when the pointer leaves");
        io.AddMousePosEvent(5, 0);
        for (int i = 0; i < 15; ++i)
            frame();
        io.AddMousePosEvent(22, 10);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        check(ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId), "main menu opens with autohide enabled");
        io.AddMousePosEvent(50, 80);
        for (int i = 0; i < 15; ++i)
            frame();
        check(auto_bar.visible() && ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId),
              "main menu remains visible while its popup is open");
        check(auto_bar.dismiss(menu_windows), "escape dismisses the main menu");
        frame();
        check(!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId), "main menu popup closes on escape");
        for (int i = 0; i < 15; ++i)
            frame();
        check(!auto_bar.visible(), "menu bar hides after its popup closes");

        auto_bar.keep_visible = true;
        selected = 0;
        for (int i = 0; i < 15; ++i)
            frame();
        io.AddMousePosEvent(combo_pos.x, combo_pos.y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        choice_shown = false;
        frame();
        check(choice_shown && auto_bar.visible(), "dropdown stays open with the menu pinned");
        io.AddMousePosEvent(choice_pos.x, choice_pos.y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        check(selected == 1, "dropdown selection works with the menu pinned");
        ImGui::DestroyContext();
        std::printf("UI windows selftest: %d failure(s)\n", failures);
        return failures ? 1 : 0;
    }

    static int run_orbital_sas_test(Sim &sim, Ship *ship)
    {
        int failures = 0;
        const auto check = [&](bool ok, const char *what) {
            std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
            failures += ok ? 0 : 1;
        };
        sim.advance(0.05);
        const int soi = ship->telemetry().soi;
        const sl::FlightBody *central = soi >= 0 ? sim.universe.world.body(soi) : nullptr;
        if (!central)
        {
            check(false, "orbital SAS test has a central body");
            return 1;
        }
        const sl::dvec3 velocity = ship->body().velocity - central->velocity;
        const double speed = glm::length(velocity);
        Controls controls;
        controls.sas = "prograde";
        bool valid = false;
        const sl::dvec3 prograde = ship->sas_direction(controls, valid);
        check(speed > 2.0 && valid && glm::dot(prograde, velocity / speed) > 0.999999999,
              "prograde follows orbital velocity");
        controls.sas = "retrograde";
        const sl::dvec3 target = ship->sas_direction(controls, valid);
        check(speed > 2.0 && valid && glm::dot(target, velocity / speed) < -0.999999999,
              "retrograde opposes orbital velocity");
        if (!valid)
            return 1;
        const sl::dvec3 prograde_up = std::abs(prograde.y) < 0.9 ? sl::dvec3(0, 1, 0) : sl::dvec3(1, 0, 0);
        ship->body().orientation = glm::quatLookAt(prograde, prograde_up);
        sim.props.set_double("/autopilot/burn/target-radius",
                             ship->telemetry().body_radius + ship->telemetry().apoapsis + 1000000.0);
        sim.props.set_string("/autopilot/burn/state", "burn");
        sim.props.set_bool("/autopilot/burn/armed", true);
        sim.advance(0.05);
        check(sim.props.get_bool("/autopilot/burn/armed") && sim.props.get_double("/controls/throttle") > 0.0,
              "orbital burn alignment accepts prograde orientation");
        sim.props.set_bool("/autopilot/burn/armed", false);
        sim.props.set_double("/controls/throttle", 0.0);
        const sl::dvec3 reference = std::abs(target.y) < 0.9 ? sl::dvec3(0, 1, 0) : sl::dvec3(1, 0, 0);
        ship->body().orientation = glm::quatLookAt(target, reference);
        ship->body().angular_velocity = sl::dvec3(0.0);
        const double initial_periapsis = ship->telemetry().periapsis;
        sim.props.set_string("/controls/rcs-mode", "rot");
        sim.props.set_string("/controls/sas", "retrograde");
        sim.props.set_double("/controls/throttle", 0.4);
        double minimum_alignment = 1.0;
        for (int i = 0; i < 600; ++i)
        {
            sim.advance(0.1);
            const sl::dvec3 desired = ship->sas_direction(controls, valid);
            if (!valid)
            {
                minimum_alignment = -1.0;
                break;
            }
            minimum_alignment = std::min(minimum_alignment,
                glm::dot(ship->body().direction_to_world(sl::dvec3(0, 0, -1)), desired));
        }
        std::printf("retrograde burn: min alignment %.6f, angular rate %.6f rad/s, periapsis change %.0f m\n",
                    minimum_alignment, glm::length(ship->body().angular_velocity),
                    ship->telemetry().periapsis - initial_periapsis);
        check(minimum_alignment > 0.985, "retrograde remains aligned through a powered burn");
        check(ship->telemetry().periapsis < initial_periapsis - 100000.0,
              "retrograde burn lowers Mars periapsis");
        const sl::dvec3 r = ship->body().position - central->position;
        const sl::dvec3 v = ship->body().velocity - central->velocity;
        const sl::dvec3 normal = glm::normalize(glm::cross(r, v));
        ship->body().orientation = glm::quatLookAt(glm::normalize(v), normal);
        ship->body().angular_velocity = sl::dvec3(0.0);
        sim.props.set_double("/controls/throttle", 0.0);
        sim.props.set_string("/controls/sas", "off");
        sim.props.set_string("/controls/rcs-mode", "lin");
        sim.props.set_int("/sim/warp-index", 0);
        sim.props.set_double("/controls/rcs-y", 0.1);
        const double initial_speed = glm::dot(ship->body().velocity - central->velocity, normal);
        for (int i = 0; i < 10; ++i)
            sim.advance(0.1);
        const double low_speed = glm::dot(ship->body().velocity - central->velocity, normal);
        sim.props.set_double("/controls/rcs-y", 1.0);
        for (int i = 0; i < 10; ++i)
            sim.advance(0.1);
        const double high_speed = glm::dot(ship->body().velocity - central->velocity, normal);
        const double low_delta = low_speed - initial_speed;
        const double high_delta = high_speed - low_speed;
        std::printf("linear RCS delta-v: 10%% %.6f m/s, 100%% %.6f m/s\n", low_delta, high_delta);
        check(low_delta > 0.0 && high_delta > low_delta * 7.0 && high_delta < low_delta * 13.0,
              "linear RCS thrust follows input magnitude");
        sim.props.set_string("/controls/rcs-mode", "off");
        for (int i = 0; i < 10; ++i)
            sim.advance(0.1);
        const double off_speed = glm::dot(ship->body().velocity - central->velocity, normal);
        check(std::abs(off_speed - high_speed) < low_delta * 0.01,
              "RCS off prevents manual translation");
        std::printf("orbital SAS selftest: %d failure(s)\n", failures);
        return failures ? 1 : 0;
    }

    static int run_vessel_ascent_test(Sim &sim)
    {
        int failures = 0;
        const auto check = [&](bool ok, const char *what) {
            std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
            failures += ok ? 0 : 1;
        };
        sim.step(0.05);
        Ship *ship = sim.active_ship();
        const auto docks = sim.nav.dock_links();
        if (ship && ship->spec().name == "CVEL Shuttle-A" && docks.size() == 6 &&
            sim.nav.docked(sim.active_index()))
        {
            const int payload = docks[0][0] == sim.active_index() ? docks[0][2] : docks[0][0];
            sim.mfd.set_domain("any");
            sim.mfd.set_ship("cvel-shuttle-a");
            const int mode = sim.mfd.mode_by_id("shuttle_ascent");
            check(mode >= 0 && sim.mfd.allowed(mode), "Shuttle-A ascent MFD available");
            if (mode < 0)
                return 1;
            sim.mfd.set_mode(0, mode);
            check(ship->telemetry().mass > 200000.0, "docked payload mass included");
            sim.link_claims[payload] = {};
            sim.mfd.press(0, 0);
            sim.step(0.05);
            check(!sim.vessel_ascent_active() &&
                      sim.props.get_string("/autopilot/vessel-ascent/state") == "Not started: docked vessel or controlled payload",
                  "addon-controlled payload rejected");
            sim.link_claims.erase(payload);
            const double launch_heading = ship->telemetry().heading;
            const double requested_heading = std::fmod(launch_heading + 90.0, 360.0);
            sim.props.set_double("/autopilot/vessel-ascent/heading", requested_heading);
            sim.mfd.press(0, 0);
            sim.step(0.05);
            check(sim.vessel_ascent_active() && sim.props.get_double("/controls/hover") > 0.0,
                  "MFD starts ascent with docked cargo");
            check(std::abs(std::remainder(sim.props.get_double("/controls/target-heading") - launch_heading, 360.0)) < 1e-3,
                  "Shuttle-A holds its launch heading below tower-clearance altitude");
            const std::string ship_path = "/ships/ship[" + std::to_string(sim.active_index()) + "]/";
            check(sim.props.get_double(ship_path + "fuel-flow") > 0.0,
                  "Shuttle-A publishes hover-engine fuel flow during ascent");
            const int engine_mode = sim.mfd.mode_by_id("engine");
            bool engine_flow_visible = false;
            if (engine_mode >= 0)
            {
                sim.mfd.set_mode(0, engine_mode);
                sim.mfd.update(0.05);
                MfdCanvas canvas;
                const MfdMode &engine = sim.mfd.modes()[static_cast<std::size_t>(engine_mode)];
                sol::protected_function draw = engine.table["draw"];
                if (draw.valid() && draw(sim.mfd.unit(0).self, std::ref(canvas), mfd_size, mfd_size).valid())
                    for (const MfdCommand &command : canvas.commands)
                        if (command.kind == MfdCommand::Kind::text && command.text.rfind("Flow", 0) == 0 &&
                            command.text.find("0.00 kg/s") == std::string::npos)
                            engine_flow_visible = true;
            }
            check(engine_flow_visible, "Engine MFD shows active Shuttle-A fuel flow");
            sim.mfd.set_mode(0, mode);
            sim.link_claims[payload] = {};
            sim.step(0.05);
            check(!sim.vessel_ascent_active() && sim.props.get_double("/controls/hover") == 0.0 &&
                      sim.props.get_string("/autopilot/vessel-ascent/state") == "Ascent aborted: unsafe docking or payload control",
                  "payload control claim aborts active ascent");
            sim.link_claims.erase(payload);
            sim.mfd.press(0, 0);
            sim.step(0.05);
            check(sim.vessel_ascent_active(), "ascent restarts after payload control released");
            Ship *other = sim.spawn(ship->spec());
            check(other && !other->spec().ports.empty(), "second vessel spawned");
            if (other && !other->spec().ports.empty())
            {
                sim.nav.link(payload, 1, sim.ship_count() - 1, 0);
                sim.step(0.05);
                check(!sim.vessel_ascent_active() && sim.props.get_double("/controls/hover") == 0.0 &&
                          sim.props.get_string("/autopilot/vessel-ascent/state") == "Ascent aborted: unsafe docking or payload control",
                      "indirectly docked spacecraft aborts active ascent");
                sim.mfd.press(0, 0);
                sim.step(0.05);
                check(!sim.vessel_ascent_active() &&
                          sim.props.get_string("/autopilot/vessel-ascent/state") == "Not started: docked vessel or controlled payload",
                      "indirectly docked spacecraft rejected");
            }
            std::printf("vessel ascent selftest: %d failure(s)\n", failures);
            return failures ? 1 : 0;
        }
        if (ship && (ship->spec().name == "CVEL Deltaglider" || ship->spec().name == "CVEL Deltaglider (Nuclear)"))
        {
            const int mode = sim.mfd.mode_by_id("dg_ascent");
            sim.mfd.set_domain("any");
            sim.mfd.set_ship(ship->spec().name == "CVEL Deltaglider" ? "cvel-deltaglider" : "cvel-deltaglider-nuclear");
            check(mode >= 0 && sim.mfd.allowed(mode), "Deltaglider ascent MFD available");
            if (mode < 0)
                return 1;
            sim.mfd.set_mode(0, mode);
            const auto displays = [&](const std::string &text) {
                MfdCanvas canvas;
                const MfdMode &m = sim.mfd.modes()[static_cast<std::size_t>(mode)];
                sol::protected_function draw = m.table["draw"];
                if (!draw.valid() || !draw(m.table, std::ref(canvas), mfd_size, mfd_size).valid())
                    return false;
                for (const MfdCommand &command : canvas.commands)
                    if (command.kind == MfdCommand::Kind::text && command.text == text)
                        return true;
                return false;
            };
            check(displays("Off"), "MFD shows ascent is off before START");
            sim.props.set_double("/autopilot/vessel-ascent/target-km", 149.0);
            sim.mfd.press(0, 0);
            sim.step(0.05);
            check(!sim.vessel_ascent_active() && displays("Not started: invalid target or heading"),
                  "MFD displays rejected start");
            sim.props.set_double("/autopilot/vessel-ascent/target-km", 200.0);
            sim.props.set_double("/controls/gimbal-pitch", 0.1);
            sim.mfd.press(0, 0);
            sim.step(0.05);
            check(sim.vessel_ascent_active() && displays("Liftoff") && sim.props.get_double("/controls/throttle") > 0.0 &&
                      sim.props.get_double("/controls/hover") > 0.0,
                  "MFD START engages Deltaglider ascent");
            check(std::abs(sim.props.get_double("/controls/gimbal-pitch") -
                           ship->main_pitch_trim(sim.props.get_double("/controls/gimbal-yaw"))) < 1e-9,
                  "Deltaglider ascent applies main thrust pitch trim");
            {
                const double yaw = 0.3;
                const double pitch = ship->main_pitch_trim(yaw);
                const double range = glm::radians(ship->spec().gimbal_range);
                const sl::dquat swing = glm::angleAxis(yaw * range, sl::dvec3(0, 1, 0)) *
                                        glm::angleAxis(pitch * range, sl::dvec3(1, 0, 0));
                double torque = 0.0, moment_scale = 0.0;
                const auto &engines = ship->spec().stages[0].engines;
                for (std::size_t i = 0; i < engines.size(); ++i)
                {
                    if (engines[i].hover || engines[i].pod || !engines[i].rcs_groups.empty())
                        continue;
                    const sl::Thruster &thruster = ship->body().thrusters[i];
                    torque += glm::cross(thruster.position, swing * glm::normalize(engines[i].direction) * thruster.max_thrust).x;
                    moment_scale += glm::length(thruster.position) * thruster.max_thrust;
                }
                check(moment_scale > 0.0 && std::abs(torque) < moment_scale * 1e-8,
                      "main thrust pitch trim cancels pitch torque with yaw gimbal");
            }
            sim.props.set_double("/controls/gimbal-pitch", -0.2);
            sim.step(0.05);
            check(!sim.vessel_ascent_active() &&
                      std::abs(sim.props.get_double("/controls/gimbal-pitch") + 0.2) < 1e-9,
                  "manual pitch gimbal cancels ascent and keeps the requested setting");
            sim.props.set_double("/controls/gimbal-pitch", 0.1);
            sim.mfd.press(0, 0);
            sim.step(0.05);
            check(sim.vessel_ascent_active(), "Deltaglider ascent restarts after manual gimbal cancellation");
            for (int i = 0; i < 400; ++i)
                sim.advance(0.1);
            check(sim.vessel_ascent_active() && displays("Climb") && ship->telemetry().altitude > 100.0 &&
                      ship->telemetry().vertical_speed > 0.0,
                  "Deltaglider MFD shows active climb after takeoff");
            for (int i = 0; i < 1900; ++i)
                sim.advance(0.1);
            check(sim.vessel_ascent_active() && displays("Climb") && ship->telemetry().altitude > 20000.0 &&
                      ship->telemetry().vertical_speed > 0.0,
                  "Deltaglider MFD ascent climbs through 20 km");
            sim.mfd.press(0, 1);
            sim.step(0.05);
            check(!sim.vessel_ascent_active() && displays("Ascent cancelled") &&
                      sim.props.get_double("/controls/throttle") == 0.0 &&
                      sim.props.get_double("/controls/hover") == 0.0 &&
                      std::abs(sim.props.get_double("/controls/gimbal-pitch") - 0.1) < 1e-9,
                  "MFD STOP cancels Deltaglider ascent");
            std::printf("vessel ascent selftest: %d failure(s)\n", failures);
            return failures ? 1 : 0;
        }
        const char *ids[] = {"ares_ascent", "dg_ascent", "shuttle_ascent"};
        const char *names[] = {"Ares Ascent", "Deltaglider Ascent", "Shuttle-A Ascent"};
        const char *folders[] = {"cvel-ares", "cvel-deltaglider", "cvel-shuttle-a"};
        sim.mfd.set_domain("any");
        for (int i = 0; i < 3; ++i)
        {
            const int mode = sim.mfd.mode_by_id(ids[i]);
            sim.mfd.set_ship(folders[i]);
            bool drawn = false;
            if (mode >= 0)
            {
                const MfdMode &m = sim.mfd.modes()[static_cast<std::size_t>(mode)];
                sol::protected_function draw = m.table["draw"];
                MfdCanvas canvas;
                drawn = draw.valid() && draw(m.table, std::ref(canvas), mfd_size, mfd_size).valid() && m.name == names[i];
            }
            check(drawn && sim.mfd.allowed(mode), names[i]);
            sim.mfd.set_ship("unrelated");
            check(!sim.mfd.allowed(mode), "ascent mode restricted to its ship");
        }
        for (const auto &entry : {std::pair<const char *, const char *>{"dg_ascent", "cvel-deltaglider-nuclear"},
                                  {"shuttle_ascent", "cvel-shuttle-a-nuclear"}, {"shuttle_ascent", "cvel-shuttle-aq"}})
        {
            sim.mfd.set_ship(entry.second);
            check(sim.mfd.allowed(entry.first), "ascent mode offered to supported variant");
        }
        sim.props.set_string("/autopilot/vessel-ascent/request", "Shuttle-A");
        sim.step(0.05);
        check(!sim.vessel_ascent_active(), "wrong vessel rejected");
        sim.props.set_double("/autopilot/vessel-ascent/target-km", 149.0);
        sim.props.set_string("/autopilot/vessel-ascent/request", "Ares");
        sim.step(0.05);
        check(!sim.vessel_ascent_active(), "target below 150 km rejected");
        sim.props.set_double("/autopilot/vessel-ascent/target-km", 200.0);
        sim.mfd.set_ship("cvel-ares");
        sim.mfd.set_mode(0, sim.mfd.mode_by_id("ares_ascent"));
        sim.mfd.press(0, 0);
        sim.step(0.05);
        check(sim.vessel_ascent_active() && sim.props.get_double("/controls/hover") > 0.0, "MFD starts Ares ascent");
        {
            MfdCanvas canvas;
            const int index = sim.mfd.mode_by_id("ares_ascent");
            bool drawn = false;
            if (index >= 0)
            {
                const MfdMode &mode = sim.mfd.modes()[static_cast<std::size_t>(index)];
                sol::protected_function draw = mode.table["draw"];
                drawn = draw.valid() && draw(mode.table, std::ref(canvas), mfd_size, mfd_size).valid();
            }
            bool locked = false;
            for (const MfdCommand &command : canvas.commands)
                locked = locked || (command.kind == MfdCommand::Kind::text && command.text == "TARGET / HEADING LOCKED");
            check(drawn && locked, "active MFD shows target and heading are locked");
        }
        sim.mfd.set_mode(0, sim.mfd.mode_by_id("surface"));
        sim.step(0.05);
        check(sim.vessel_ascent_active(), "ascent continues after changing MFD mode");
        std::string why;
        check(!sim.skip_time(10.0, why), "time skip refused during ascent");
        sim.props.set_double("/controls/hover", 0.3);
        sim.step(0.05);
        check(!sim.vessel_ascent_active() && std::abs(sim.props.get_double("/controls/hover") - 0.3) < 1e-9,
              "manual hover override cancels and preserves input");
        sim.props.set_string("/autopilot/vessel-ascent/request", "Ares");
        sim.step(0.05);
        sim.props.set_double("/controls/throttle", 0.4);
        sim.step(0.05);
        check(!sim.vessel_ascent_active() && std::abs(sim.props.get_double("/controls/throttle") - 0.4) < 1e-9 &&
                  sim.props.get_double("/controls/hover") == 0.0,
              "manual main throttle override cuts automatic hover");
        sim.props.set_string("/autopilot/vessel-ascent/request", "Ares");
        sim.step(0.05);
        sim.props.set_string("/autopilot/vessel-ascent/request", "stop");
        sim.step(0.05);
        check(!sim.vessel_ascent_active() && sim.props.get_double("/controls/hover") == 0.0, "stop cuts thrust");
        sim.props.set_string("/autopilot/vessel-ascent/request", "Ares");
        sim.step(0.05);
        sim.props.set_bool("/autopilot/enabled", true);
        sim.step(0.05);
        check(!sim.vessel_ascent_active() && sim.props.get_bool("/autopilot/enabled"), "another autopilot takes control");
        std::printf("vessel ascent selftest: %d failure(s)\n", failures);
        return failures ? 1 : 0;
    }

    static int run_save_test(Sim &sim, Ship *ship)
    {
        int failures = 0;
        const auto check = [&](bool ok, const char *what) {
            std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
            failures += ok ? 0 : 1;
        };
        if (!ship)
            return 1;
        const std::string verify_path = sim.props.get_string("/selftest/verify-saved-flight");
        if (!verify_path.empty())
        {
            Scenario reference;
            std::string error;
            if (!load_scenario(verify_path, reference, error))
            {
                std::fprintf(stderr, "%s\n", error.c_str());
                return 1;
            }
            const ScenarioShip &entry = reference.ships[static_cast<std::size_t>(reference.active)];
            if (!entry.carry_state || !entry.has_state)
            {
                std::fprintf(stderr, "save selftest: missing saved state\n");
                return 1;
            }
            const int body = sim.universe.find(entry.body);
            if (body < 0)
                return 1;
            const sl::FlightBody &parent = *sim.universe.world.body(body);
            const sl::dvec3 r(entry.state_position[0], entry.state_position[1], entry.state_position[2]);
            const sl::dvec3 v(entry.state_velocity[0], entry.state_velocity[1], entry.state_velocity[2]);
            const sl::dquat q(entry.state_orientation[0], entry.state_orientation[1], entry.state_orientation[2], entry.state_orientation[3]);
            const sl::dvec3 omega(entry.state_angular_velocity[0], entry.state_angular_velocity[1], entry.state_angular_velocity[2]);
            const ShipCarryState actual = ship->carry_state();
            check(std::abs(reference.epoch_jd - sim.universe.julian_date()) < 1e-8, "startup date");
            check(glm::length(ship->body().position - parent.position - r) < 0.01 &&
                  glm::length(ship->body().velocity - parent.velocity - v) < 0.001, "startup trajectory");
            check(actual.active_stage == entry.carry_state->active_stage &&
                  actual.stage_propellant == entry.carry_state->stage_propellant &&
                  std::abs(actual.rcs_propellant - entry.carry_state->rcs_propellant) < 1e-8,
                  "startup stage and propellant");
            check(std::abs(glm::dot(ship->body().orientation, q)) > 1.0 - 1e-9 &&
                  glm::length(ship->body().angular_velocity - omega) < 1e-9, "startup attitude and angular rate");
            check(std::abs(actual.gear_position - entry.carry_state->gear_position) < 1e-8 &&
                  std::abs(actual.mission_time - entry.carry_state->mission_time) < 1e-8,
                  "startup gear and mission time");
            std::size_t expected_docks = 0;
            bool fleet_matches = true;
            for (std::size_t i = 0; i < reference.ships.size(); ++i)
            {
                const ScenarioShip &saved = reference.ships[i];
                if (saved.dock_to >= 0)
                    ++expected_docks;
                if (i >= static_cast<std::size_t>(sim.ship_count()) || !saved.has_state || !saved.carry_state)
                {
                    fleet_matches = false;
                    continue;
                }
                const Ship *vessel = sim.ship(static_cast<int>(i));
                const int parent_id = sim.universe.find(saved.body);
                if (parent_id < 0)
                {
                    fleet_matches = false;
                    continue;
                }
                const sl::FlightBody &p = *sim.universe.world.body(parent_id);
                const sl::dvec3 r_saved(saved.state_position[0], saved.state_position[1], saved.state_position[2]);
                const sl::dvec3 v_saved(saved.state_velocity[0], saved.state_velocity[1], saved.state_velocity[2]);
                fleet_matches = fleet_matches &&
                    glm::length(vessel->body().position - p.position - r_saved) < 0.01 &&
                    glm::length(vessel->body().velocity - p.velocity - v_saved) < 0.001 &&
                    vessel->carry_state().stage_propellant == saved.carry_state->stage_propellant;
            }
            check(fleet_matches && sim.nav.dock_links().size() == expected_docks,
                  "startup fleet trajectory, fuel and dock links");
            if (actual.has_systems && !actual.systems.components.empty())
                check(std::abs(actual.systems.components[0].amount - entry.carry_state->systems.components[0].amount) < 1e-8,
                      "startup systems");
            std::printf("save startup selftest: %d failure(s)\n", failures);
            return failures ? 1 : 0;
        }
        ShipCarryState changed = ship->carry_state();
        if (ship->stage_count() > 1)
            changed.active_stage = 1;
        for (double &fuel : changed.stage_propellant)
            fuel *= 0.6;
        changed.rcs_propellant *= 0.5;
        changed.gear_position = 0.25;
        changed.mission_time = 1234.0;
        if (changed.has_systems && !changed.systems.components.empty())
            changed.systems.components[0].amount *= 0.5;
        ship->restore_carry_state(changed);
        sim.step(0.05);
        ship->body().angular_velocity = {0.01, -0.02, 0.03};
        const sl::dvec3 position = ship->body().position;
        const sl::dvec3 velocity = ship->body().velocity;
        const sl::dquat orientation = ship->body().orientation;
        const ShipCarryState expected = ship->carry_state();
        const std::size_t docks = sim.nav.dock_links().size();
        const Scenario saved = capture_scenario(sim, "Save test");
        const std::string output_path = sim.props.get_string("/selftest/write-saved-flight");
        if (!output_path.empty())
        {
            std::string error;
            if (!save_scenario(output_path, saved, error))
            {
                std::fprintf(stderr, "%s\n", error.c_str());
                return 1;
            }
        }
        Scenario parsed;
        std::string error;
        const bool ok = parse_scenario(scenario_to_json(saved), "save-test.json", parsed, error);
        check(ok, "saved scenario JSON parses");
        if (!ok)
        {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        check(std::abs(parsed.epoch_jd - sim.universe.julian_date()) < 1e-8, "current date preserved");
        Ship *restored = app::apply_scenario(sim, parsed, app::TerrainSet{});
        check(restored != nullptr, "saved scenario restores");
        if (!restored)
            return 1;
        check(sim.nav.dock_links().size() == docks, "dock links preserved");
        const ShipCarryState actual = restored->carry_state();
        check(actual.active_stage == expected.active_stage && actual.stage_propellant == expected.stage_propellant,
              "staging and fuel preserved");
        check(std::abs(actual.rcs_propellant - expected.rcs_propellant) < 1e-8 &&
              std::abs(actual.gear_position - expected.gear_position) < 1e-8 &&
              std::abs(actual.mission_time - expected.mission_time) < 1e-8,
              "RCS, gear and mission time preserved");
        check(glm::length(restored->body().position - position) < 0.01 &&
              glm::length(restored->body().velocity - velocity) < 0.001,
              "position and velocity preserved");
        check(std::abs(glm::dot(restored->body().orientation, orientation)) > 1.0 - 1e-9 &&
              glm::length(restored->body().angular_velocity - sl::dvec3(0.01, -0.02, 0.03)) < 1e-9,
              "attitude and angular velocity preserved");
        if (expected.has_systems && !expected.systems.components.empty())
            check(std::abs(actual.systems.components[0].amount - expected.systems.components[0].amount) < 1e-8,
                  "systems state preserved");
        std::printf("save selftest: %d failure(s)\n", failures);
        return failures ? 1 : 0;
    }

    static int run_orbitcomp_patched_test(Sim &sim, Ship *ship)
    {
        sim.step(0.05);
        const int mode = sim.mfd.mode_by_id("orbitcomp");
        const int target = sim.universe.find(sim.props.get_string("/sim/target"));
        if (!ship || mode < 0 || target < 0)
        {
            std::fprintf(stderr, "orbitcomp patched selftest: missing ship, target or MFD\n");
            return 1;
        }
        const int origin = ship->telemetry().soi;
        const sol::table &table = sim.mfd.modes()[static_cast<std::size_t>(mode)].table;
        MfdCanvas canvas;
        sim.props.set_double("/orbitcomp/page", 3);
        sol::protected_function draw = table["draw"];
        sim.lua.execute("");
        sol::protected_function_result result = draw(table, std::ref(canvas), mfd_size, mfd_size);
        if (!result.valid())
        {
            const sol::error error = result;
            std::fprintf(stderr, "orbitcomp patched selftest: draw: %s\n", error.what());
            return 1;
        }
        sol::optional<sol::table> plan = table["plan"];
        if (!plan || (plan->get<std::string>("kind") != "ejection" && plan->get<std::string>("kind") != "return"))
        {
            std::fprintf(stderr, "orbitcomp patched selftest: no ejection or return plan\n");
            return 1;
        }
        const double wait = plan->get<double>("t_burn"), dv = plan->get<double>("dv");
        sol::protected_function button = table["button"];
        sim.lua.execute("");
        result = button(table, 7);
        if (!result.valid())
        {
            const sol::error error = result;
            std::fprintf(stderr, "orbitcomp patched selftest: TO MAN: %s\n", error.what());
            return 1;
        }
        sol::table hooks = table["_test"];
        sol::protected_function frame_fn = hooks["ship_frame"];
        sim.lua.execute("");
        result = frame_fn();
        if (!result.valid())
        {
            const sol::error error = result;
            std::fprintf(stderr, "orbitcomp patched selftest: frame: %s\n", error.what());
            return 1;
        }
        sol::table frame = result;
        sol::protected_function node_fn = hooks["compute_node"];
        sim.lua.execute("");
        result = node_fn(frame, sol::nil);
        if (!result.valid())
        {
            const sol::error error = result;
            std::fprintf(stderr, "orbitcomp patched selftest: node: %s\n", error.what());
            return 1;
        }
        sol::table node = result;
        sol::protected_function encounter_fn = hooks["patched_encounter"];
        sim.lua.execute("");
        result = encounter_fn(frame, node);
        if (!result.valid())
        {
            const sol::error error = result;
            std::fprintf(stderr, "orbitcomp patched selftest: prediction: %s\n", error.what());
            return 1;
        }
        sol::optional<sol::table> encounter = result;
        if (!encounter)
        {
            std::fprintf(stderr, "orbitcomp patched selftest: no encounter prediction\n");
            return 1;
        }
        const double predicted_distance = encounter->get<double>("distance");
        const double predicted_time = encounter->get<double>("time");
        if (!std::isfinite(wait) || !std::isfinite(dv) || !std::isfinite(predicted_time) ||
            !std::isfinite(predicted_distance) || wait < 0.0 || dv <= 0.0 ||
            predicted_time <= wait || predicted_time > 20.0 * 365.25 * 86400.0)
        {
            std::fprintf(stderr, "orbitcomp patched selftest: invalid prediction\n");
            return 1;
        }
        sol::protected_function text_fn = hooks["target_approach_text"];
        sim.lua.execute("");
        const double target_radius = sim.universe.bodies[static_cast<std::size_t>(target)].radius;
        result = text_fn(target_radius + 200000.0);
        if (!result.valid() || result.get<std::string>() != "Alt 200.0 km")
        {
            std::fprintf(stderr, "orbitcomp patched selftest: target radius not applied\n");
            return 1;
        }
        sim.lua.execute("");
        result = text_fn(predicted_distance);
        if (!result.valid())
        {
            const sol::error error = result;
            std::fprintf(stderr, "orbitcomp patched selftest: formatting: %s\n", error.what());
            return 1;
        }
        const std::string expected = "Close " + result.get<std::string>() + " in ";
        canvas.commands.clear();
        sim.lua.execute("");
        result = draw(table, std::ref(canvas), mfd_size, mfd_size);
        bool displayed = false;
        for (const MfdCommand &command : canvas.commands)
            if (command.kind == MfdCommand::Kind::text && command.text.rfind(expected, 0) == 0 &&
                command.text.find("(SOI patched)") != std::string::npos)
                displayed = true;
        if (!result.valid() || expected.rfind("Close Alt ", 0) != 0 || !displayed)
        {
            std::fprintf(stderr, "orbitcomp patched selftest: expected body altitude on MAN: %s\n", expected.c_str());
            return 1;
        }
        std::string why;
        if (wait > 0.0 && !sim.skip_time(wait, why))
        {
            std::fprintf(stderr, "orbitcomp patched selftest: departure skip: %s\n", why.c_str());
            return 1;
        }
        const sl::FlightBody &central = *sim.universe.world.body(origin);
        const auto node_vector = [](const sol::table &t) {
            return sl::dvec3(t.get<double>(1), t.get<double>(2), t.get<double>(3));
        };
        if (const sol::optional<sol::table> geo = (*encounter)["geo"])
        {
            const double plotted_distance = glm::length(node_vector(geo->get<sol::table>("ship_at")) -
                                                        node_vector(geo->get<sol::table>("target_at")));
            if (std::abs(plotted_distance - predicted_distance) > std::max(1.0, predicted_distance * 1.0e-9))
            {
                std::fprintf(stderr, "orbitcomp patched selftest: plot and predicted encounter disagree\n");
                return 1;
            }
        }
        const double node_position_error = glm::length(ship->body().position - central.position - node_vector(node.get<sol::table>("r")));
        const double node_velocity_error = glm::length(ship->body().velocity - central.velocity - node_vector(node.get<sol::table>("v")));
        if (node_position_error > 100.0 || node_velocity_error > 0.1)
        {
            std::fprintf(stderr, "orbitcomp patched selftest: departure mismatch %.3f m, %.6f m/s\n",
                         node_position_error, node_velocity_error);
            return 1;
        }
        ship->body().velocity += glm::normalize(ship->body().velocity - central.velocity) * dv;
        double remaining = predicted_time - wait;
        while (remaining > 0.0)
        {
            double step = 21600.0;
            if (ship->telemetry().soi == origin)
            {
                const sl::FlightBody &centre = *sim.universe.world.body(origin);
                const double distance = glm::length(ship->body().position - centre.position);
                const double speed = glm::length(ship->body().velocity - centre.velocity);
                const double to_boundary = (sim.universe.bodies[static_cast<std::size_t>(origin)].soi - distance) / std::max(speed, 1.0);
                step = std::clamp(to_boundary * 0.1, 30.0, 3600.0);
            }
            else if (target != origin && target != sim.universe.bodies[static_cast<std::size_t>(origin)].parent)
            {
                const sl::FlightBody &destination = *sim.universe.world.body(target);
                const double distance = glm::length(ship->body().position - destination.position);
                const double speed = glm::length(ship->body().velocity - destination.velocity);
                const double soi = sim.universe.bodies[static_cast<std::size_t>(target)].soi;
                if (distance < soi * 2.0)
                    step = distance > soi ? std::clamp((distance - soi) / std::max(speed, 1.0) * 0.1, 30.0, 3600.0) : 300.0;
            }
            step = std::min(step, remaining);
            if (!sim.skip_time(step, why))
            {
                std::fprintf(stderr, "orbitcomp patched selftest: transfer skip: %s\n", why.c_str());
                return 1;
            }
            remaining -= step;
        }
        const double actual = glm::length(ship->body().position - sim.universe.position(target));
        const double error = std::abs(actual - predicted_distance);
        std::printf("patched prediction: %.3f km at %.3f days, simulator %.3f km, error %.3f km\n",
                    predicted_distance / km, predicted_time / 86400.0, actual / km, error / km);
        const bool ok = error < std::max(50000.0, predicted_distance * 0.001);
        std::printf("orbitcomp patched selftest: %d failure(s)\n", ok ? 0 : 1);
        return ok ? 0 : 1;
    }

    static int run_orbitcomp_test(Sim &sim, Ship *ship)
    {
        if (sim.props.get_bool("/selftest/orbitcomp-patched"))
            return run_orbitcomp_patched_test(sim, ship);
        int failures = 0;
        const auto check = [&](bool ok, const char *what) {
            std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
            failures += ok ? 0 : 1;
        };
        if (!ship || sim.ship_count() < 2)
        {
            std::fprintf(stderr, "orbitcomp selftest: a ship and a target are required\n");
            return 1;
        }
        sim.step(0.05);
        const int mode = sim.mfd.mode_by_id("orbitcomp");
        if (mode < 0)
        {
            std::fprintf(stderr, "orbitcomp selftest: MFD not found\n");
            return 1;
        }
        const sol::table &table = sim.mfd.modes()[static_cast<std::size_t>(mode)].table;
        sim.props.set_double("/orbitcomp/page", 3);
        MfdCanvas canvas;
        sol::protected_function draw = table["draw"];
        sim.lua.execute("");
        sol::protected_function_result result = draw(table, std::ref(canvas), mfd_size, mfd_size);
        if (!result.valid())
        {
            const sol::error error = result;
            std::fprintf(stderr, "orbitcomp selftest: %s\n", error.what());
            return 1;
        }
        sol::optional<sol::table> plan = table["plan"];
        if (!plan || plan->get<std::string>("kind") != "lambert")
        {
            std::fprintf(stderr, "orbitcomp selftest: no Lambert plan\n");
            return 1;
        }
        const auto vector = [](const sol::table &t) {
            return sl::dvec3(t.get<double>(1), t.get<double>(2), t.get<double>(3));
        };
        const double wait = plan->get<double>("wait");
        const double tof = plan->get<double>("tof");
        const double dv = plan->get<double>("dv");
        const sl::dvec3 departure = vector(plan->get<sol::table>("r"));
        const sl::dvec3 velocity = vector(plan->get<sol::table>("v"));
        const sl::dvec3 delta_v = vector(plan->get<sol::table>("delta_v"));
        const sl::dvec3 predicted_target = vector(plan->get<sol::table>("target_r"));
        const double duration = dv * ship->telemetry().mass / ship->main_max_thrust();
        const bool finite_burn = sim.props.get_bool("/selftest/orbitcomp-finite-burn");
        const double finite_throttle = 0.1;
        check(std::abs(sim.props.get_double("/plan/orbitcomp/at") -
                       (sim.universe.time() + wait - duration / 2.0)) < 1e-5,
              "published burn start follows the Lambert departure");
        sol::protected_function button = table["button"];
        sim.lua.execute("");
        result = button(table, 7);
        check(result.valid(), "load Lambert plan into MAN");
        const auto check_page = [&](int page, const char *prefix, const char *name) {
            if (page > 0) sim.props.set_double("/orbitcomp/page", page);
            canvas.commands.clear();
            sim.lua.execute("");
            sol::protected_function_result drawn = draw(table, std::ref(canvas), mfd_size, mfd_size);
            bool found = false;
            for (const MfdCommand &command : canvas.commands)
                if (command.kind == MfdCommand::Kind::text && command.text.rfind(prefix, 0) == 0)
                    found = true;
            check(drawn.valid() && found, name);
        };
        check_page(0, "Close Dist ", "MAN post-burn ship separation");
        check_page(1, "Cls Dist ", "ORB current ship separation");
        check_page(2, "Close Dist ", "MAN no-burn ship separation");
        const std::string original_target = sim.props.get_string("/sim/target");
        sim.props.set_string("/sim/target", "Moon");
        sim.props.set_string("/target/kind", "body");
        check_page(1, "Cls Alt ", "ORB current body altitude");
        check_page(2, "Close Alt ", "MAN no-burn body altitude");
        sim.props.set_double("/orbitcomp/prograde", 10);
        check_page(2, "Close Alt ", "MAN post-burn body altitude");
        sim.props.set_double("/orbitcomp/prograde", 0);
        sim.props.set_string("/sim/target", original_target);
        sim.props.set_string("/target/kind", "ship");
        std::string why;
        const double finite_duration = duration / finite_throttle;
        const double coast = finite_burn ? wait - finite_duration / 2.0 : wait;
        if (coast < 0.0 || (coast > 0.0 && !sim.skip_time(coast, why)))
        {
            std::fprintf(stderr, "orbitcomp selftest: departure skip failed: %s\n", why.c_str());
            return 1;
        }
        const int parent = sim.universe.find("Earth");
        double transfer_coast = tof;
        if (finite_burn)
        {
            const sl::dvec3 direction = glm::normalize(delta_v);
            const sl::dvec3 up = std::abs(direction.y) < 0.9 ? sl::dvec3(0, 1, 0) : sl::dvec3(1, 0, 0);
            ship->body().orientation = glm::quatLookAt(direction, up);
            ship->body().angular_velocity = sl::dvec3(0.0);
            sim.props.set_string("/controls/sas", "attitude");
            const double initial_fuel = ship->telemetry().stage_fuel;
            double delivered = 0.0, burn_elapsed = 0.0;
            const double dt = 0.05;
            while (delivered < dv && burn_elapsed < finite_duration * 2.0 + 1.0)
            {
                sl::dvec3 local_up, east, north;
                ship->local_frame(local_up, east, north);
                sim.props.set_double("/controls/target-pitch", std::asin(glm::dot(direction, local_up)) * 180.0 / app::pi);
                sim.props.set_double("/controls/target-heading", std::atan2(glm::dot(direction, east), glm::dot(direction, north)) * 180.0 / app::pi);
                const double accel = ship->main_max_thrust() / ship->telemetry().mass;
                sim.props.set_double("/controls/throttle", std::min(finite_throttle, (dv - delivered) / (accel * dt)));
                sim.step(dt);
                delivered += ship->telemetry().thrust / ship->telemetry().mass * dt;
                burn_elapsed += dt;
            }
            sim.props.set_double("/controls/throttle", 0.0);
            transfer_coast -= burn_elapsed - finite_duration / 2.0;
            std::printf("finite burn: %.2f s, delivered %.3f m/s, fuel used %.3f kg, final alignment %.5f, angular rate %.5f\n",
                        burn_elapsed, delivered, initial_fuel - ship->telemetry().stage_fuel,
                        glm::dot(ship->body().direction_to_world(sl::dvec3(0, 0, -1)), direction),
                        glm::length(ship->body().angular_velocity));
            check(std::abs(delivered - dv) < 0.1 && ship->telemetry().stage_fuel < initial_fuel,
                  "real engine delivers planned delta-v");
        }
        else
        {
            const sl::FlightBody &body = *sim.universe.world.body(parent);
            const double position_error = glm::length(ship->body().position - body.position - departure);
            const double velocity_error = glm::length(ship->body().velocity - body.velocity - velocity);
            std::printf("departure error: %.3f m, %.6f m/s; wait %.1f s, flight %.1f s, dV %.1f m/s\n",
                        position_error, velocity_error, wait, tof, dv);
            check(position_error < 10.0 && velocity_error < 0.01, "planner departure matches simulator coast");
            ship->body().velocity += delta_v;
        }
        if (transfer_coast <= 0.0 || !sim.skip_time(transfer_coast, why))
        {
            std::fprintf(stderr, "orbitcomp selftest: transfer skip failed: %s\n", why.c_str());
            return 1;
        }
        const sl::FlightBody &at_arrival = *sim.universe.world.body(parent);
        const sl::dvec3 target_r = sim.ship(1)->body().position - at_arrival.position;
        const double target_error = glm::length(target_r - predicted_target);
        const double miss = glm::length(ship->body().position - sim.ship(1)->body().position);
        std::printf("target prediction error: %.3f m; %s intercept miss: %.3f m\n",
                    target_error, finite_burn ? "finite-burn" : "impulsive", miss);
        check(target_error < (finite_burn ? 100.0 : 10.0), "target prediction matches simulator coast");
        check(miss < (finite_burn ? 20000.0 : 100.0),
              finite_burn ? "finite burn reaches within 20 km of target" : "planned impulse intercepts target in simulator");
        std::printf("orbitcomp selftest: %d failure(s)\n", failures);
        return failures ? 1 : 0;
    }

    int run_selftest(Sim &sim, Ship *ship, const SelftestOptions &options)
    {
        if (sim.props.get_bool("/selftest/warp-atmosphere"))
        {
            const bool expected_rails = sim.props.get_bool("/selftest/expect-rails");
            const std::string blocker = ship->rails_blocker(Controls{});
            sim.props.set_int("/sim/warp-index", 4);
            sim.step(0.01);
            const bool ok = ship->on_rails() == expected_rails &&
                            (expected_rails ? blocker.empty() && sim.current_warp() == 5.0
                                            : blocker == "inside the atmosphere" && sim.current_warp() == 4.0);
            std::printf("atmospheric warp selftest: %d failure(s), blocker '%s', rails %d, warp %.0f\n",
                        ok ? 0 : 1, blocker.c_str(), ship->on_rails(), sim.current_warp());
            return ok ? 0 : 1;
        }
        if (sim.props.get_bool("/selftest/buran-docked-gear"))
        {
            if (!ship || ship->spec().name != "CVEL Buran" || sim.props.get_double("/controls/gear") != 0.0 ||
                ship->gear_position() != 0.0 || !sim.nav.docked(sim.active_index()))
            {
                std::fprintf(stderr, "Buran docked gear selftest: invalid initial gear or docking state\n");
                return 1;
            }
            for (int i = 0; i < 20; ++i)
                sim.step(0.05);
            if (ship->gear_position() != 0.0)
            {
                std::fprintf(stderr, "Buran docked gear selftest: gear deployed without a command\n");
                return 1;
            }
            std::puts("Buran docked gear selftest ok");
            return 0;
        }
        if (sim.props.get_bool("/selftest/energia-autopilot"))
        {
            if (!ship || ship->spec().name != "CVEL Energia" || ship->active_stage() != 0)
            {
                std::fprintf(stderr, "Energia autopilot selftest: expected the launch scenario\n");
                return 1;
            }
            sim.props.set_bool("/autopilot/enabled", true);
            bool staged = false;
            for (int i = 0; i < 5000 && !ship->crashed(); ++i)
            {
                sim.step(0.05);
                if (ship->active_stage() == 1)
                {
                    staged = true;
                    break;
                }
            }
            if (!staged || ship->telemetry().altitude < 10000.0 ||
                sim.props.get_double("/controls/throttle") < 0.99 || ship->body().tanks[1].mass <= 0.0 ||
                ship->body().tanks[1].mass >= ship->body().tanks[1].capacity)
            {
                std::fprintf(stderr, "Energia autopilot selftest: booster separation or continuous core burn failed\n");
                return 1;
            }
            const double core = ship->body().tanks[1].mass;
            for (int i = 0; i < 20; ++i)
                sim.step(0.05);
            if (ship->crashed() || ship->body().tanks[1].mass >= core)
            {
                std::fprintf(stderr, "Energia autopilot selftest: core did not continue after separation\n");
                return 1;
            }
            if (sim.ship_count() < 2 || sim.ship(1)->spec().name != "CVEL Buran" || sim.ship(1)->gear_position() != 0.0)
            {
                std::fprintf(stderr, "Energia autopilot selftest: docked Buran gear was not retracted\n");
                return 1;
            }
            sim.props.set_double("/controls/gear", 1.0);
            sim.props.set_bool("/controls/stage", true);
            sim.step(0.05);
            if (sim.active_ship() != sim.ship(1) || sim.props.get_double("/controls/gear") != 0.0)
            {
                std::fprintf(stderr, "Energia autopilot selftest: Buran handoff inherited launcher gear command\n");
                return 1;
            }
            for (int i = 0; i < 10; ++i)
                sim.step(0.05);
            if (sim.ship(1)->gear_position() != 0.0)
            {
                std::fprintf(stderr, "Energia autopilot selftest: Buran gear deployed without a command\n");
                return 1;
            }
            sim.props.set_double("/controls/gear", 1.0);
            sim.step(0.05);
            if (sim.ship(1)->gear_position() <= 0.0)
            {
                std::fprintf(stderr, "Energia autopilot selftest: Buran gear ignored deployment command\n");
                return 1;
            }
            std::puts("Energia autopilot selftest ok");
            return 0;
        }
        if (sim.props.get_bool("/selftest/energia-liftoff"))
        {
            const auto fail = [](const char *message) {
                std::fprintf(stderr, "Energia liftoff selftest: %s\n", message);
                return 1;
            };
            if (!ship || ship->spec().name != "CVEL Energia" || ship->active_stage() != 0 ||
                ship->burning_stage_end() != 2 || ship->body().tanks.size() < 2)
                return fail("expected boosters and core burning in parallel");
            const double booster = ship->body().tanks[0].mass, core = ship->body().tanks[1].mass;
            const double expected_thrust = 4.0 * (7850806.0 + 1961000.0);
            sim.props.set_double("/controls/throttle", 1.0);
            for (int i = 0; i < 200; ++i)
                sim.step(0.05);
            const double core_fuel = ship->body().tanks[1].mass;
            if (ship->crashed() || ship->telemetry().altitude < 10.0 ||
                ship->body().tanks[0].mass >= booster || core_fuel >= core ||
                std::abs(ship->main_max_thrust() - expected_thrust) > 1.0)
                return fail("liftoff, separate tank consumption or total thrust incorrect");
            if (std::abs(sim.props.get_double("/ships/ship[0]/stage[1]/fuel") - core_fuel) > 0.01 ||
                !sim.props.get_bool("/ships/ship[0]/parallel-next-stage"))
                return fail("core tank not published to Fuel MFD");
            const int fuel_mode = sim.mfd.mode_by_id("fuel");
            if (fuel_mode < 0)
                return fail("Fuel MFD unavailable");
            sim.mfd.set_mode(0, fuel_mode);
            sim.mfd.update(0.05);
            double previous = ship->body().tanks[0].mass, stage_flow = 0.0;
            for (int i = 0; i < 100; ++i)
            {
                sim.step(0.05);
                const double current = ship->body().tanks[0].mass;
                stage_flow += ((previous - current) / 0.05 - stage_flow) * 0.1;
                previous = current;
                sim.mfd.update(0.05);
            }
            MfdCanvas canvas;
            const MfdMode &fuel_mode_data = sim.mfd.modes()[static_cast<std::size_t>(fuel_mode)];
            sol::protected_function draw = fuel_mode_data.table["draw"];
            if (!draw.valid() || !draw(sim.mfd.unit(0).self, std::ref(canvas), mfd_size, mfd_size).valid())
                return fail("Fuel MFD draw failed");
            const int seconds = static_cast<int>(previous / stage_flow);
            char endurance[80];
            std::snprintf(endurance, sizeof(endurance), "Endurance  %d min %02d s", seconds / 60, seconds % 60);
            char core_text[80];
            std::snprintf(core_text, sizeof(core_text), "%.0f kg", ship->body().tanks[1].mass);
            bool endurance_visible = false, core_visible = false;
            for (const MfdCommand &command : canvas.commands)
                if (command.kind == MfdCommand::Kind::text)
                {
                    endurance_visible = endurance_visible || command.text == endurance;
                    core_visible = core_visible || command.text == core_text;
                }
            if (!endurance_visible || !core_visible)
                return fail("Fuel MFD core fuel or booster endurance incorrect");
            const double core_before_stage = ship->body().tanks[1].mass;
            if (!ship->stage_once() || ship->active_stage() != 1)
                return fail("booster separation did not retain the core");
            for (int i = 0; i < 20; ++i)
                sim.step(0.05);
            if (ship->body().tanks[1].mass >= core_before_stage || ship->body().tanks[0].mass != 0.0)
                return fail("core did not continue after booster separation");
            sim.mfd.update(0.05);
            canvas.commands.clear();
            if (!draw(sim.mfd.unit(0).self, std::ref(canvas), mfd_size, mfd_size).valid())
                return fail("Fuel MFD draw after staging failed");
            bool reset_visible = false;
            for (const MfdCommand &command : canvas.commands)
                if (command.kind == MfdCommand::Kind::text && command.text == "Endurance  --")
                    reset_visible = true;
            if (!reset_visible)
                return fail("Fuel MFD retained the booster flow after staging");
            std::puts("Energia liftoff selftest ok");
            return 0;
        }
        if (sim.props.get_bool("/selftest/saturn-v23-autopilot"))
        {
            if (!ship || ship->spec().name != "CVEL Saturn V-23L" || ship->active_stage() != 0)
            {
                std::fprintf(stderr, "Saturn V-23L autopilot selftest: expected the launch scenario\n");
                return 1;
            }
            sim.props.set_bool("/autopilot/enabled", true);
            bool staged = false;
            for (int i = 0; i < 4000 && !ship->crashed(); ++i)
            {
                sim.step(0.05);
                if (ship->active_stage() == 1)
                {
                    staged = true;
                    break;
                }
            }
            if (!staged || ship->telemetry().altitude < 50000.0 ||
                sim.props.get_double("/controls/throttle") < 0.99 || ship->body().tanks[1].mass <= 0.0 ||
                ship->body().tanks[1].mass >= ship->body().tanks[1].capacity)
            {
                std::fprintf(stderr, "Saturn V-23L autopilot selftest: booster separation or continuous core burn failed\n");
                return 1;
            }
            std::puts("Saturn V-23L autopilot selftest ok");
            return 0;
        }
        if (sim.props.get_bool("/selftest/saturn-v23-liftoff"))
        {
            const auto fail = [](const char *message) {
                std::fprintf(stderr, "Saturn V-23L liftoff selftest: %s\n", message);
                return 1;
            };
            if (!ship || (ship->spec().name != "CVEL Saturn V-23L" && ship->spec().name != "CVEL Saturn V-23L Payload") ||
                ship->active_stage() != 0 || ship->burning_stage_end() != 2 || ship->body().tanks.size() < 3)
                return fail("expected V-23L with boosters and core burning together");
            const double booster = ship->body().tanks[0].mass, core = ship->body().tanks[1].mass;
            sim.props.set_double("/controls/throttle", 1.0);
            for (int i = 0; i < 400; ++i)
                sim.step(0.05);
            if (ship->crashed() || ship->telemetry().altitude < 100.0 || ship->telemetry().vertical_speed < 10.0)
                return fail("rocket did not lift off");
            if (ship->body().tanks[0].mass >= booster || ship->body().tanks[1].mass >= core ||
                ship->body().tanks[2].mass != ship->body().tanks[2].capacity ||
                ship->telemetry().max_thrust < 100000000.0 || ship->main_max_thrust() < 100000000.0)
                return fail("parallel engine thrust or independent fuel tanks incorrect");
            if (sim.props.get_double("/ships/ship[0]/stage-delta-v") <= 0.0 ||
                !sim.props.get_bool("/ships/ship[0]/parallel-next-stage"))
                return fail("parallel staging telemetry incorrect");
            const double core_before = ship->body().tanks[1].mass;
            if (!ship->stage_once() || ship->active_stage() != 1 || ship->burning_stage_end() != 2)
                return fail("booster separation did not retain the core");
            for (int i = 0; i < 20; ++i)
                sim.step(0.05);
            if (ship->body().tanks[0].mass != 0.0 || ship->body().tanks[1].mass >= core_before ||
                sim.props.get_bool("/ships/ship[0]/parallel-next-stage"))
                return fail("core did not continue after booster separation");
            std::puts("Saturn V-23L liftoff selftest ok");
            return 0;
        }
        if (sim.props.get_bool("/selftest/star48-stack"))
        {
            const auto fail = [](const char *message) {
                std::fprintf(stderr, "Star 48 stack selftest: %s\n", message);
                return 1;
            };
            const bool delta = ship && ship->spec().name == "CVEL Delta IV Heavy";
            const int first = delta ? 1 : 0;
            if (!ship || sim.ship_count() < first + 3 ||
                sim.ship(first)->spec().name != "CVEL Star 48B" ||
                sim.ship(first + 1)->spec().name != "CVEL Star 37FM" ||
                sim.ship(first + 2)->spec().name != "Luna E3")
                return fail("expected Star 48B, Star 37FM and Luna E3 stack");
            sim.step(0.05);
            if (sim.nav.dock_links().size() != static_cast<std::size_t>(first + 2))
                return fail("stack did not dock");
            for (const auto &link : sim.nav.dock_links())
            {
                sl::dvec3 position_a, direction_a, up_a, position_b, direction_b, up_b;
                sim.ship(link[0])->port_world(link[1], position_a, direction_a, up_a);
                sim.ship(link[2])->port_world(link[3], position_b, direction_b, up_b);
                if (glm::length(position_a - position_b) > 0.1 || glm::dot(direction_a, direction_b) > -0.99)
                    return fail("stack ports are misaligned");
            }
            if (delta)
            {
                for (int i = 0; i < 3; ++i)
                {
                    sim.props.set_bool("/controls/stage", true);
                    sim.step(0.05);
                }
                if (ship->active_stage() != 4)
                    return fail("Delta did not separate boosters, core and fairing");
                sim.props.set_bool("/controls/stage", true);
                sim.step(0.05);
                if (sim.active_ship() != sim.ship(first))
                    return fail("Delta did not release Star 48B");
            }
            for (int i = first; i < first + 2; ++i)
            {
                ship = sim.active_ship();
                if (ship != sim.ship(i) || ship->body().tanks.empty())
                    return fail("wrong active stage");
                const double fuel = ship->body().tanks[0].mass;
                sim.props.set_double("/controls/throttle", 1.0);
                for (int step = 0; step < 20; ++step)
                    sim.step(0.05);
                sim.props.set_double("/controls/throttle", 0.0);
                if (ship->body().tanks[0].mass >= fuel || ship->telemetry().thrust <= 0.0)
                    return fail("upper stage did not burn");
                sim.props.set_bool("/controls/stage", true);
                sim.step(0.05);
                if (sim.active_ship() != sim.ship(i + 1))
                    return fail("upper stage did not release payload");
            }
            if (!sim.nav.dock_links().empty())
                return fail("payloads remain docked after staging");
            std::puts("Star 48 stack selftest ok");
            return 0;
        }
        if (sim.props.get_bool("/selftest/entrance-label-settings"))
        {
            sl::PropertyTree settings;
            settings_defaults(settings);
            if (!settings.get_bool("/sim/settings/entrance-labels"))
            {
                std::fprintf(stderr, "Entrance labels should default on\n");
                return 1;
            }
            settings_load(settings);
            if (!settings.get_bool("/sim/settings/entrance-labels"))
            {
                std::fprintf(stderr, "Existing settings should retain the entrance label default\n");
                return 1;
            }
            for (bool enabled : {false, true})
            {
                settings.set_bool("/sim/settings/entrance-labels", enabled);
                settings_save(settings);
                sl::PropertyTree restored;
                settings_defaults(restored);
                settings_load(restored);
                if (restored.get_bool("/sim/settings/entrance-labels") != enabled)
                {
                    std::fprintf(stderr, "Entrance label setting did not persist\n");
                    return 1;
                }
            }
            std::puts("Entrance label settings selftest ok");
            return 0;
        }
        if (sim.props.get_bool("/selftest/curiosity") || sim.props.get_bool("/selftest/sev"))
        {
            const bool curiosity = sim.props.get_bool("/selftest/curiosity");
            const char *name = curiosity ? "Curiosity" : "SEV";
            const auto fail = [name](const char *message) {
                std::fprintf(stderr, "%s selftest: %s\n", name, message);
                return 1;
            };
            if (!ship || ship->spec().name != (curiosity ? "Curiosity Rover" : "Space Exploration Vehicle") || !ship->systems())
                return fail("expected rover with electrical systems");
            const int planet = ship->telemetry().soi;
            const sl::dvec3 up = glm::normalize(ship->body().position - sim.universe.position(planet));
            const sl::FlightBody &primary = *sim.universe.world.body(planet);
            for (int i = 0; i < 300; ++i)
                sim.step(0.01);
            const sl::dvec3 ground_velocity = primary.velocity + glm::cross(primary.spin_axis * primary.spin_rate, ship->body().position - primary.position);
            if (glm::length(ship->body().velocity - ground_velocity) > 0.25 || ship->crashed())
                return fail("parked rover moved or crashed");
            const double battery = ship->systems()->battery_wh();
            if (curiosity)
                sim.props.set_double("/controls/anim/mast", 1.0);
            sim.props.set_double("/controls/throttle", 1.0);
            for (int i = 0; i < 1000; ++i)
                sim.step(0.01);
            if (curiosity && ship->animation_position(0) < 0.99)
                return fail("mast control did not reach its target");
            if (ship->systems()->battery_wh() >= battery || ship->systems()->demand_w() < 500.0)
                return fail("wheel motors did not draw battery power");
            if (ship->telemetry().surface_speed < 0.05 || ship->crashed() ||
                glm::dot(ship->body().direction_to_world({0, 1, 0}), up) < 0.5)
                return fail("rover did not drive upright");
            const auto forward_speed = [&]() {
                const sl::FlightBody &body = ship->body();
                const sl::dvec3 surface_velocity = primary.velocity + glm::cross(primary.spin_axis * primary.spin_rate, body.position - primary.position);
                return glm::dot(body.velocity - surface_velocity, body.direction_to_world({0, 0, -1}));
            };
            if (forward_speed() < 0.05)
                return fail("forward throttle did not drive forward");
            if (!curiosity)
            {
                sim.props.set_double("/controls/throttle", -1.0);
                for (int i = 0; i < 2000; ++i)
                    sim.step(0.01);
                if (forward_speed() > -0.05)
                    return fail("reverse throttle did not drive backward");
            }
            sim.props.set_double("/controls/throttle", 0.0);
            for (double angle : {90.0, 180.0, -90.0})
            {
                ship->place_on_surface(planet, up, 0.0, 0.0);
                sl::FlightBody &body = ship->body();
                body.orientation = glm::normalize(body.orientation * glm::angleAxis(angle * 3.14159265358979323846 / 180.0, sl::dvec3(0, 0, 1)));
                for (int i = 0; i < 1000; ++i)
                    sim.step(0.01);
                const double altitude = sim.universe.world.altitude_of(body.position);
                if (!std::isfinite(altitude) || altitude < 0.3 || !ship->telemetry().landed || ship->crashed())
                    return fail("overturned rover was not supported");
            }
            std::printf("%s selftest ok\n", name);
            return 0;
        }
        if (sim.props.get_bool("/selftest/rover-collision"))
        {
            const auto fail = [](const char *message) {
                std::fprintf(stderr, "Rover collision selftest: %s\n", message);
                return 1;
            };
            const int moon = sim.universe.find("Moon");
            if (!ship || !ship->spec().ground() || moon < 0)
                return fail("expected ground vehicle and Moon");
            const auto base_it = std::find_if(sim.bases.begin(), sim.bases.end(), [](const Base &base) { return base.id == "tranquility"; });
            if (base_it == sim.bases.end())
                return fail("Tranquility base missing");
            Base base = *base_it;
            base.objects.clear();
            BaseObject obstacle;
            obstacle.type = "tank";
            obstacle.radius = 2.5;
            obstacle.height = 5.0;
            obstacle.x = base.pad_x;
            obstacle.z = base.pad_z;
            base.objects.push_back(obstacle);
            const std::vector<Base> bases{base};
            SolidScene scene;
            scene.universe = &sim.universe;
            scene.body = moon;
            scene.bases = &bases;
            sl::dvec3 direction;
            double heading;
            base_pad(base, sim.universe.bodies[static_cast<std::size_t>(moon)].radius, direction, heading);
            ship->place_on_surface(moon, direction, heading, base.height);
            const sl::dvec3 initial = ship->body().position;
            collide_ground_vehicle(scene, *ship);
            const sl::dvec3 displacement = ship->body().position - initial;
            if (glm::length(displacement) < 0.1)
                return fail("fixture did not collide");
            const sl::dvec3 normal = glm::normalize(displacement);
            const sl::dvec3 tangent = glm::normalize(glm::cross(direction, normal));
            sl::FlightBody &planet = *sim.universe.world.body(moon);
            planet.velocity = -30000.0 * normal;
            for (double incoming : {0.0, -2.0, 2.0})
            {
                ship->place_on_surface(moon, direction, heading, base.height);
                ship->body().velocity += incoming * normal + tangent;
                collide_ground_vehicle(scene, *ship);
                const sl::dvec3 ground_velocity = planet.velocity + glm::cross(planet.spin_axis * planet.spin_rate, ship->body().position - planet.position);
                const sl::dvec3 relative = ship->body().velocity - ground_velocity;
                if (glm::length(relative - normal * std::max(0.0, incoming) - tangent) > 0.01)
                {
                    std::fprintf(stderr, "incoming %.1f, surface-relative speed %.3f\n", incoming, glm::length(relative));
                    return fail("collision changed the wrong velocity");
                }
            }
            std::puts("Rover collision selftest ok");
            return 0;
        }
        if (sim.props.get_bool("/selftest/rover-rollover"))
        {
            const auto fail = [](const char *message) {
                std::fprintf(stderr, "Rover rollover selftest: %s\n", message);
                return 1;
            };
            const int mars = sim.universe.find("Mars");
            if (!ship || ship->spec().name != "CVEL Lunar Rover" || mars < 0)
                return fail("expected lunar rover and Mars");
            const sl::dvec3 direction = glm::normalize(sl::dvec3(1.0, 0.2, 0.3));
            for (double angle : {0.0, 90.0, 180.0, -90.0})
            {
                ship->place_on_surface(mars, direction, 0.0, 0.0);
                sl::FlightBody &body = ship->body();
                body.orientation = glm::normalize(body.orientation * glm::angleAxis(angle * 3.14159265358979323846 / 180.0, sl::dvec3(0, 0, 1)));
                for (int i = 0; i < 1000; ++i)
                    sim.step(0.01);
                const double altitude = sim.universe.world.altitude_of(body.position);
                double lowest_hull = 1.0e9;
                for (double x : {-0.8, 0.8})
                    for (double y : {-0.25, 0.25})
                        for (double z : {-1.5, 1.5})
                            lowest_hull = std::min(lowest_hull, sim.universe.world.altitude_of(ship->origin() + body.direction_to_world({x, y, z})));
                for (double x : {-0.6, 0.6})
                    for (double y : {0.25, 0.75})
                        for (double z : {-0.9, 0.4})
                            lowest_hull = std::min(lowest_hull, sim.universe.world.altitude_of(ship->origin() + body.direction_to_world({x, y, z})));
                const double up = glm::dot(body.direction_to_world({0, 1, 0}), glm::normalize(body.position - sim.universe.position(mars)));
                if (!std::isfinite(altitude) || altitude < 0.45 || lowest_hull < -0.3 ||
                    !ship->telemetry().landed || ship->crashed() || (angle == 180.0 && up > -0.5))
                {
                    std::fprintf(stderr, "rover angle %.0f: altitude %.3f, hull %.3f, up %.3f, landed %d, crashed %d\n",
                                 angle, altitude, lowest_hull, up, int(ship->telemetry().landed), int(ship->crashed()));
                    return fail("rover did not remain supported above ground");
                }
            }
            ship->place_on_surface(mars, direction, 0.0, 0.0);
            sim.props.set_double("/controls/throttle", 1.0);
            for (int i = 0; i < 500; ++i)
                sim.step(0.01);
            sim.props.set_double("/controls/throttle", 0.0);
            if (ship->telemetry().surface_speed < 1.0 || ship->crashed())
                return fail("upright rover did not drive");
            std::puts("Rover rollover selftest ok");
            return 0;
        }
        if (sim.props.get_bool("/selftest/centaur-handoff"))
        {
            const auto fail = [](const char *message) {
                std::fprintf(stderr, "Centaur handoff selftest: %s\n", message);
                return 1;
            };
            if (!ship || ship->spec().name != "CVEL Energia-M" || sim.ship_count() < 4 ||
                sim.ship(1)->spec().name != "CVEL Centaur G" || sim.ship(2)->spec().name != "CVEL Fregat" ||
                sim.ship(3)->spec().name != "Luna E3")
                return fail("expected Energia-M, Centaur, Fregat and Luna E3");
            if (!options.select_instruments)
                return fail("instrument selection unavailable");
            options.select_instruments();
            const int surface = sim.mfd.mode_by_id("surface"), orbit = sim.mfd.mode_by_id("orbit");
            if (surface < 0 || orbit < 0)
                return fail("expected surface and orbit MFDs");
            sim.mfd.set_mode(0, surface);
            sim.mfd.set_mode(1, orbit);
            sim.mfd.unit(0).self["handoff_test"] = 42;
            sim.mfd.unit(0).selecting = true;
            sim.mfd.unit(1).power = false;
            sim.step(0.05);
            if (sim.nav.dock_links().size() != 3 || ship->body().tanks.size() < 4)
                return fail("initial payload stack not docked");
            ship->body().tanks[0].mass = 0.0;
            sim.props.set_bool("/controls/stage", true);
            sim.step(0.05);
            if (ship->active_stage() != 3)
                return fail("Energia-M boosters and fairing did not separate");
            options.select_instruments();
            if (sim.mfd.unit(0).mode != surface || !sim.mfd.unit(0).selecting || sim.mfd.unit(1).mode != orbit)
                return fail("MFDs changed during same-vessel staging");
            ship->body().tanks[3].mass = 0.0;
            sim.props.set_bool("/controls/stage", true);
            sim.step(0.05);
            ship = sim.active_ship();
            if (ship != sim.ship(1) || sim.nav.dock_links().size() != 2 || ship->body().tanks.empty())
                return fail("Centaur was not released with its payload");
            options.select_instruments();
            if (sim.mfd.unit(0).mode != surface || sim.mfd.unit(1).mode != orbit ||
                sim.mfd.unit(0).self.get_or("handoff_test", 0) != 42 || !sim.mfd.unit(0).selecting || sim.mfd.unit(1).power)
                return fail("MFDs changed on Energia-M to Centaur handoff");
            ship->body().tanks[0].mass = 0.1;
            sim.props.set_double("/controls/throttle", 1.0);
            sim.step(0.1);
            if (ship->body().tanks[0].mass > 1e-6)
                return fail("Centaur fuel did not run out");
            if (sim.props.get_bool("/selftest/centaur-handoff-rails"))
            {
                sim.props.set_double("/controls/throttle", 0.0);
                sim.props.set_int("/sim/warp-index", 8);
                sim.step(0.05);
                if (!ship->on_rails())
                    return fail("could not enter time warp before staging");
            }
            sim.props.set_bool("/controls/stage", true);
            sim.step(0.05);
            if (sim.active_ship() != sim.ship(2) || sim.nav.dock_links().size() != 1 ||
                !sim.nav.docked(2) || !sim.nav.docked(3))
                return fail("staging did not hand control to Fregat with Luna attached");
            options.select_instruments();
            if (sim.mfd.unit(0).mode != surface || sim.mfd.unit(1).mode != orbit ||
                sim.mfd.unit(0).self.get_or("handoff_test", 0) != 42 || !sim.mfd.unit(0).selecting || sim.mfd.unit(1).power)
                return fail("MFDs changed on Centaur to Fregat handoff");
            sim.props.set_int("/sim/warp-index", 0);
            sim.props.set_double("/controls/throttle", 1.0);
            for (int i = 0; i < 100; ++i)
                sim.step(0.05);
            for (int i = 0; i < 4; ++i)
            {
                const sl::FlightBody &body = sim.ship(i)->body();
                for (int axis = 0; axis < 3; ++axis)
                    if (!std::isfinite(body.position[axis]) || !std::isfinite(body.velocity[axis]))
                        return fail("non-finite vessel state after handoff");
            }
            if (sim.ship(2)->body().tanks[0].mass >= sim.ship(2)->spec().stages[0].propellant)
                return fail("Fregat engine did not burn");
            const int restricted = sim.mfd.mode_by_id("dg_ascent");
            if (restricted < 0 || sim.mfd.allowed(restricted))
                return fail("expected ship-restricted MFD");
            sim.mfd.set_mode(0, restricted);
            sim.mfd.unit(0).self["handoff_test"] = 42;
            sim.props.set_bool("/controls/stage", true);
            sim.step(0.05);
            if (sim.active_ship() != sim.ship(3))
                return fail("Fregat did not release Luna");
            options.select_instruments();
            if (sim.mfd.unit(0).mode == restricted || sim.mfd.unit(0).self.get_or("handoff_test", 0) == 42 ||
                sim.mfd.unit(1).mode != orbit || sim.mfd.unit(1).power)
                return fail("incompatible MFD was not replaced on staging");
            sim.mfd.unit(0).self["handooff_test"] = 42;
            sim.activate(1);
            options.select_instruments();
            if (sim.mfd.unit(0).self.get_or("handoff_test", 0) == 42)
                return fail("manual vessel selection retained staged MFD state");
            std::puts("Centaur handoff selftest ok");
            return 0;
        }
        if (sim.props.get_bool("/selftest/orbital-base-landing"))
        {
            sim.props.set_bool("/autopilot/land/targeted", true);
            sim.props.set_string("/autopilot/land/base", sim.props.get_string("/selftest/landing-plane-base", "gale"));
            sim.props.set_bool("/autopilot/land/armed", true);
            sim.props.set_string("/autopilot/land/state", "off");
            const std::string fault = sim.props.get_string("/selftest/orbital-fault");
            sim.props.set_bool("/sim/settings/weather", fault == "weather");
            sim.props.set_bool("/sim/settings/nonspherical-gravity", fault == "gravity");
            if (fault == "fuel")
            {
                ShipCarryState state = ship->carry_state();
                for (double &fuel : state.stage_propellant)
                    fuel = 50.0;
                ship->restore_carry_state(state);
                ship->after_step(0.0);
            }
            std::string last_phase;
            bool engaged = false, fault_injected = false;
            const bool test_warp = sim.props.get_bool("/selftest/orbital-warp");
            bool saw_alignment_warp = false, saw_burn_warp = false, saw_descent_warp = false, saw_approach_warp = false;
            bool saw_burn_one = false, countdown_ok = true;
            bool saw_wait_countdown = false, saw_check_countdown = false, saw_align_countdown = false;
            double countdown_ignition = -1.0;
            int burn_frames = 0;
            for (int step = 0; step < 300000; ++step)
            {
                const std::string phase = sim.props.get_string("/autopilot/land/state");
                if (phase != last_phase)
                {
                    std::printf("orbital autoland phase: %s time %.1f fuel %.1f\n",
                                phase.c_str(), sim.universe.time(), ship->telemetry().stage_fuel);
                    last_phase = phase;
                }
                if (phase == "Waiting for window")
                {
                    if (fault == "orbit-change" && !fault_injected)
                    {
                        ship->go_dynamic();
                        ship->body().velocity += sl::dvec3(10.0, 0.0, 0.0);
                        ship->after_step(0.0);
                        fault_injected = true;
                    }
                    if (fault == "competing-autopilot" && !fault_injected)
                    {
                        sim.props.set_bool("/autopilot/enabled", true);
                        fault_injected = true;
                    }
                    sim.props.set_int("/sim/warp-index", 10);
                }
                if (test_warp && (phase == "Plane alignment" || phase == "Plane burn" ||
                                  phase == "Powered descent" || phase == "Target approach"))
                {
                    if (phase == "Plane burn")
                        ++burn_frames;
                    sim.props.set_int("/sim/warp-index", phase == "Plane burn" && burn_frames >= 2 && burn_frames < 4 ? 0 : 3);
                }
                if ((phase == "Waiting for window" || phase == "Checking orbit" || phase == "Plane alignment") &&
                    sim.current_warp() <= 4.0)
                {
                    const double left = sim.props.get_double("/autopilot/land/window");
                    const double ignition = sim.universe.time() + left;
                    if (left < 0.0 || (countdown_ignition >= 0.0 && std::abs(ignition - countdown_ignition) > 0.5))
                        countdown_ok = false;
                    if (countdown_ignition < 0.0)
                        countdown_ignition = ignition;
                    saw_wait_countdown |= phase == "Waiting for window";
                    saw_check_countdown |= phase == "Checking orbit" && sim.current_warp() == 1.0;
                    saw_align_countdown |= phase == "Plane alignment";
                }
                if ((phase == "Plane burn" || phase == "Powered descent" || phase == "Target approach") &&
                    sim.props.get_double("/autopilot/land/window") != 0.0)
                    countdown_ok = false;
                if (phase == "Plane alignment" && fault == "sas" && !fault_injected)
                {
                    sim.props.set_string("/controls/sas", "stability");
                    fault_injected = true;
                }
                if (phase == "Plane alignment" && fault == "alignment-miss" && !fault_injected &&
                    sim.props.get_double("/autopilot/land/window") < 0.8)
                {
                    const sl::dvec3 nose = ship->body().direction_to_world(sl::dvec3(0, 0, -1));
                    sl::dvec3 turn_axis = glm::cross(nose, sl::dvec3(0, 1, 0));
                    if (glm::length(turn_axis) < 0.1)
                        turn_axis = glm::cross(nose, sl::dvec3(1, 0, 0));
                    ship->body().orientation = glm::angleAxis(glm::pi<double>(), glm::normalize(turn_axis)) * ship->body().orientation;
                    fault_injected = true;
                }
                if (phase == "Powered descent" && (fault == "gravity-descent" || fault == "fuel-descent") && !fault_injected)
                {
                    if (fault == "gravity-descent")
                        sim.props.set_bool("/sim/settings/nonspherical-gravity", true);
                    else
                    {
                        ShipCarryState state = ship->carry_state();
                        for (double &fuel : state.stage_propellant)
                            fuel = 99.0;
                        ship->restore_carry_state(state);
                        ship->after_step(0.0);
                    }
                    sim.step(0.1);
                    const auto &messages = sim.messages();
                    const std::string expected_warning = fault == "gravity-descent" ?
                        "forecast conditions changed during descent" : "descent fuel reserve depleted";
                    const bool warning = std::any_of(messages.begin(), messages.end(), [&](const std::string &message) {
                        return message.find(expected_warning) != std::string::npos;
                    });
                    const bool ok = warning && sim.props.get_bool("/autopilot/land/armed");
                    sim.cancel_autoland("Autoland cancelled: selftest");
                    std::printf("orbital autoland fault %s: %s\n", fault.c_str(), ok ? "passed" : "FAILED");
                    return ok ? 0 : 1;
                }
                if (phase == "Plane burn" && sim.props.get_double("/controls/hover") > 0.0 && !fault_injected)
                {
                    if (fault == "cancel-burn")
                    {
                        sim.cancel_autoland("Autoland cancelled: selftest");
                        fault_injected = true;
                    }
                    else if (fault == "disarm-burn")
                    {
                        sim.props.set_bool("/autopilot/land/armed", false);
                        fault_injected = true;
                    }
                }
                const bool burn_was_firing = phase == "Plane burn" && sim.props.get_double("/controls/hover") > 0.0;
                const double before = sim.universe.time();
                if (sim.props.get_bool("/selftest/orbital-real-step"))
                    sim.advance(0.1);
                else
                    sim.step(0.1);
                const double elapsed = sim.universe.time() - before;
                if (test_warp && burn_was_firing && sim.current_warp() == 1.0 && elapsed > 0.09 && elapsed < 0.11)
                    saw_burn_one = true;
                if (test_warp && sim.props.get_string("/autopilot/land/state") == phase)
                {
                    const bool effective_four = sim.current_warp() == 4.0 && elapsed > 0.35 && elapsed < 0.45;
                    if (phase == "Plane alignment") saw_alignment_warp |= effective_four;
                    if (phase == "Plane burn")
                    {
                        saw_burn_warp |= effective_four;
                    }
                    if (phase == "Powered descent") saw_descent_warp |= effective_four;
                    if (phase == "Target approach") saw_approach_warp |= effective_four;
                }
                if (sim.props.get_bool("/autopilot/land/armed"))
                    engaged = true;
                else
                    break;
            }
            const Telemetry &t = ship->telemetry();
            const std::string result = sim.props.get_string("/autopilot/land/state");
            const double distance = sim.props.get_double("/autopilot/land/distance");
            countdown_ok = countdown_ok && countdown_ignition >= 0.0 &&
                           sim.props.get_double("/autopilot/land/window") == 0.0;
            if (!fault.empty())
            {
                const std::string expected = fault == "weather" ? "disable weather" :
                                             fault == "gravity" ? "requires spherical gravity" :
                                             fault == "fuel" ? "no plane-change windows" :
                                             fault == "orbit-change" ? "orbit changed" :
                                             fault == "sas" ? "manual SAS" :
                                             fault == "alignment-miss" ? "plane-burn alignment missed" :
                                             fault == "competing-autopilot" ? "another autopilot engaged" :
                                             fault == "cancel-burn" ? "cancelled: selftest" : "cancelled";
                const bool ok = !sim.props.get_bool("/autopilot/land/armed") &&
                                result.find(expected) != std::string::npos &&
                                sim.props.get_double("/controls/hover") == 0.0 &&
                                sim.props.get_double("/autopilot/land/window") == 0.0 &&
                                (fault == "weather" || fault == "gravity" || fault == "fuel" || fault_injected);
                std::printf("orbital autoland fault %s: %s (%s)\n", fault.c_str(), ok ? "passed" : "FAILED", result.c_str());
                return ok ? 0 : 1;
            }
            const bool ok = engaged && result == "Autoland: landed" && !ship->crashed() && t.landed &&
                            distance <= 3000.0 && t.surface_speed < 2.0 && t.stage_fuel > 100.0 && countdown_ok &&
                            saw_wait_countdown && saw_check_countdown && saw_align_countdown &&
                            (!test_warp || (saw_alignment_warp && saw_burn_warp && saw_burn_one &&
                                           saw_descent_warp && saw_approach_warp));
            std::printf("orbital autoland checks: countdown %d warp alignment %d burn %d burn-1x %d descent %d approach %d\n",
                        countdown_ok, saw_alignment_warp, saw_burn_warp, saw_burn_one, saw_descent_warp, saw_approach_warp);
            std::printf("orbital autoland result: %s distance %.1f speed %.6f fuel %.1f crashed %d\n",
                        result.c_str(), distance, t.surface_speed, t.stage_fuel, ship->crashed() ? 1 : 0);
            return ok ? 0 : 1;
        }
        if (sim.props.get_bool("/selftest/landing-plane"))
        {
            const int body_id = ship->telemetry().soi;
            const std::string base_id = sim.props.get_string("/selftest/landing-plane-base", "gale");
            const auto base = std::find_if(sim.bases.begin(), sim.bases.end(), [&](const Base &b) {
                return body_id >= 0 && b.id == base_id &&
                       b.body == sim.universe.bodies[static_cast<std::size_t>(body_id)].name;
            });
            if (base == sim.bases.end())
            {
                std::fprintf(stderr, "landing plane selftest: selected base or body missing\n");
                return 1;
            }
            Base pad = *base;
            pad.direction = base_direction(sim.universe, body_id, pad);
            pad.pole = sim.universe.pole(body_id);
            const Body &body_spec = sim.universe.bodies[static_cast<std::size_t>(body_id)];
            sl::dvec3 target;
            double heading;
            base_pad(pad, body_spec.radius, target, heading);
            const sl::FlightBody &planet = *sim.universe.world.body(body_id);
            const double flight_time = 600.0;
            const double lead_angle = sim.props.get_double("/selftest/landing-plane-lead", 0.6);
            const double search_orbits = sim.props.get_double("/selftest/landing-plane-orbits", 3.0);
            const LandingPlaneWindow plan =
                landing_plane_window(body_spec.gm, ship->body().position - planet.position,
                                     ship->body().velocity - planet.velocity, target, pad.pole, body_spec.spin,
                                     flight_time, lead_angle, ship->telemetry().period * search_orbits);
            if (!plan.valid)
            {
                std::fprintf(stderr, "landing plane selftest: no departure window\n");
                return 1;
            }
            std::string why;
            if (sim.props.get_bool("/selftest/landing-plane-burn"))
            {
                const bool shuttle = ship->spec().name == "CVEL Shuttle-A";
                if ((!shuttle && ship->spec().name != "Ares Spider") || plan.wait < 60.0)
                {
                    std::fprintf(stderr, "landing plane burn selftest: requires Ares or Shuttle-A and a future window\n");
                    return 1;
                }
                const double dv = glm::length(plan.change.delta_v);
                const double mass = ship->telemetry().mass;
                const double initial_fuel = ship->telemetry().stage_fuel;
                double thrust = 0.0, flow = 0.0;
                const StageSpec &stage = ship->spec().stages[static_cast<std::size_t>(ship->active_stage())];
                for (const EngineSpec &engine : stage.engines)
                    if (engine.hover && engine.isp > 0.0)
                    {
                        thrust += engine.thrust;
                        flow += engine.thrust / (engine.isp * 9.80665);
                    }
                if (thrust <= 0.0 || flow <= 0.0)
                {
                    std::fprintf(stderr, "landing plane burn selftest: missing hover thrust\n");
                    return 1;
                }
                if (dv < 0.1)
                {
                    std::fprintf(stderr, "landing plane burn selftest: plane change too small\n");
                    return 1;
                }
                const LandingPlaneBurn burn = landing_plane_burn(dv, mass, initial_fuel, 0.0, thrust, flow);
                if (!burn.valid || !burn.sufficient_fuel)
                {
                    std::fprintf(stderr, "landing plane burn selftest: insufficient fuel for plane change\n");
                    return 1;
                }
                const double duration = burn.duration;
                const double centre_time = burn.impulse_center;
                LandingForecastResult orbital_forecast;
                LandingForecastResult touchdown_forecast;
                if (sim.props.get_bool("/selftest/landing-shadow") &&
                    sim.props.get_bool("/selftest/landing-plane-descent"))
                {
                    const auto candidates = landing_candidates(body_spec.gm, ship->body().position - planet.position,
                                                               ship->body().velocity - planet.velocity, target, pad.pole,
                                                               body_spec.spin, flight_time,
                                                               ship->telemetry().period * search_orbits,
                                                               {lead_angle}, mass, initial_fuel, 100.0,
                                                               thrust, flow, 40.0, 240.0);
                    const auto candidate = std::find_if(candidates.begin(), candidates.end(), [&](const auto &c) {
                        return std::abs(c.window.wait - plan.wait) < 1e-5;
                    });
                    if (candidate == candidates.end())
                    {
                        std::fprintf(stderr, "landing candidate selftest: expected window unavailable\n");
                        return 1;
                    }
                    const double live_time = sim.universe.time();
                    const sl::dvec3 live_position = ship->body().position;
                    const double live_fuel = ship->telemetry().stage_fuel;
                    const sl::dvec3 axis = shuttle ? sl::dvec3(0, 1, 0) : sl::dvec3(0, 0, -1);
                    Universe windy_universe = sim.universe;
                    windy_universe.world.body(body_id)->wind_field = [](const sl::dvec3 &) {
                        return sl::dvec3(1.0, 0.0, 0.0);
                    };
                    LandingCandidateForecast windy(windy_universe, *ship, pad, body_id, shuttle,
                                                    axis, *candidate, thrust, 100.0, true);
                    if (!windy.advance(1) || windy.result().status != LandingForecastStatus::unavailable_wind)
                    {
                        std::fprintf(stderr, "landing candidate selftest: mutable wind callback was not rejected\n");
                        return 1;
                    }
                    LandingCandidateForecast candidate_forecast(sim.universe, *ship, pad, body_id, shuttle,
                                                                axis, *candidate, thrust, 100.0);
                    if (candidate_forecast.advance(0) || candidate_forecast.advance(-1))
                    {
                        std::fprintf(stderr, "landing candidate selftest: forecast ended before stepping\n");
                        return 1;
                    }
                    int batches = 0;
                    while (!candidate_forecast.advance(37))
                        if (++batches > 1000)
                        {
                            std::fprintf(stderr, "landing candidate selftest: forecast exceeded its bounded budget\n");
                            return 1;
                        }
                    orbital_forecast = candidate_forecast.result();
                        const LandingForecastResult synchronous =
                            forecast_landing_candidate(sim.universe, *ship, pad, body_id, shuttle, axis,
                                                       *candidate, thrust, 100.0);
                        if (!orbital_forecast.captured || sim.universe.time() != live_time ||
                            ship->body().position != live_position || ship->telemetry().stage_fuel != live_fuel ||
                            !candidate_forecast.advance(0) || synchronous.status != orbital_forecast.status ||
                            synchronous.captured != orbital_forecast.captured ||
                            glm::length(synchronous.position - orbital_forecast.position) > 1e-6 ||
                            glm::length(synchronous.velocity - orbital_forecast.velocity) > 1e-9 ||
                            std::abs(synchronous.fuel - orbital_forecast.fuel) > 1e-9)
                        {
                            std::fprintf(stderr, "landing candidate selftest: forecasts differ, failed or changed live flight (status %d)\n",
                                         static_cast<int>(orbital_forecast.status));
                            return 1;
                        }
                        std::printf("landing candidate forecast: captured with %.1f kg fuel in %d batches; synchronous match\n",
                                    orbital_forecast.fuel, batches + 1);
                        touchdown_forecast = forecast_landing_candidate(sim.universe, *ship, pad, body_id, shuttle, axis,
                                                                        *candidate, thrust, 100.0, true);
                        LandingCandidateForecast incremental_touchdown(sim.universe, *ship, pad, body_id, shuttle,
                                                                       axis, *candidate, thrust, 100.0, true);
                        int touchdown_batches = 0;
                        while (!incremental_touchdown.advance(37))
                            if (++touchdown_batches > 1000)
                            {
                                std::fprintf(stderr, "landing touchdown forecast: exceeded its bounded budget\n");
                                return 1;
                            }
                        const auto &incremental = incremental_touchdown.result();
                        if (!touchdown_forecast.captured || touchdown_forecast.status != LandingForecastStatus::landed ||
                            touchdown_forecast.distance > 3000.0 || touchdown_forecast.surface_speed >= 2.0 ||
                            touchdown_forecast.fuel <= 100.0 || sim.universe.time() != live_time ||
                            ship->body().position != live_position || ship->telemetry().stage_fuel != live_fuel ||
                            incremental.status != touchdown_forecast.status || !incremental.captured ||
                            glm::length(incremental.position - touchdown_forecast.position) > 1e-6 ||
                            std::abs(incremental.fuel - touchdown_forecast.fuel) > 1e-9)
                        {
                            std::fprintf(stderr, "landing touchdown forecast: failed or changed live flight (status %d)\n",
                                         static_cast<int>(touchdown_forecast.status));
                            return 1;
                        }
                        std::printf("landing touchdown forecast: distance %.1f m speed %.1f m/s fuel %.1f kg\n",
                                    touchdown_forecast.distance, touchdown_forecast.surface_speed, touchdown_forecast.fuel);
                        const double approach_reserve = (orbital_forecast.fuel + touchdown_forecast.fuel) * 0.5;
                        const auto short_fuel = forecast_landing_candidate(sim.universe, *ship, pad, body_id, shuttle, axis,
                                                                           *candidate, thrust, approach_reserve, true);
                        if (!short_fuel.captured || short_fuel.status != LandingForecastStatus::insufficient_fuel)
                        {
                            std::fprintf(stderr, "landing touchdown forecast: did not enforce reserve during approach\n");
                            return 1;
                        }
                }
                if (sim.props.get_bool("/selftest/landing-prediction"))
                {
                    sl::OrbitElements orbit;
                    if (!sl::orbit_from_state(body_spec.gm, ship->body().position - planet.position,
                                              ship->body().velocity - planet.velocity, orbit) || plan.wait < centre_time)
                    {
                        std::fprintf(stderr, "landing prediction selftest: invalid departure orbit\n");
                        return 1;
                    }
                    const double start = plan.wait - centre_time;
                    const sl::OrbitState state = sl::state_from_orbit(body_spec.gm, orbit, start);
                    LandingTrajectoryInput input;
                    input.position = state.position;
                    input.velocity = state.velocity;
                    input.target_direction = glm::angleAxis(body_spec.spin * start, pad.pole) * target;
                    input.spin_axis = pad.pole;
                    input.spin_rate = body_spec.spin;
                    input.mu = body_spec.gm;
                    input.radius = body_spec.radius;
                    input.target_elevation = pad.height + pad.pad_deck;
                    input.plane_delta_v = plan.change.delta_v;
                    input.mass = mass;
                    input.fuel = initial_fuel;
                    input.reserve = 100.0;
                    input.thrust = thrust;
                    input.flow = flow;
                    input.drag_area = ship->body().drag_area;
                    input.atmosphere_density = planet.atmosphere.surface_density;
                    input.atmosphere_scale_height = planet.atmosphere.scale_height;
                    input.lifting_surfaces = !ship->body().surfaces.empty();
                    input.torque_accel = ship->spec().torque_accel;
                    input.max_rate = ship->spec().max_rate;
                    double sea_level_thrust = 0.0;
                    for (const EngineSpec &engine : stage.engines)
                        if (engine.hover)
                            sea_level_thrust += engine.thrust *
                                                (engine.isp_sl > 0.0 ? engine.isp_sl / engine.isp : 1.0);
                    input.sea_level_thrust_ratio = sea_level_thrust / thrust;
                    const auto predicted = landing_trajectory_predict(input);
                    std::printf("landing prediction: %s fuel %.1f altitude %.1f distance %.1f time %.1f\n",
                                !predicted.supported ? "unsupported" : predicted.captured ? "captured" : "not captured",
                                predicted.fuel, predicted.altitude,
                                predicted.distance, predicted.time);
                }
                if (duration > 240.0 || plan.wait < 40.0 + centre_time ||
                    !sim.skip_time(plan.wait - 40.0 - centre_time, why))
                {
                    std::fprintf(stderr, "landing plane burn selftest: unavailable burn: %s\n", why.c_str());
                    return 1;
                }
                const sl::dvec3 direction = plan.change.delta_v / dv;
                const sl::dvec3 thrust_axis = shuttle ? sl::dvec3(0, 1, 0) : sl::dvec3(0, 0, -1);
                const double dt = 0.1;
                std::vector<std::unique_ptr<sl::Terrain>> shadow_terrain;
                std::unique_ptr<Universe> shadow_universe;
                std::unique_ptr<Ship> shadow_ship;
                LandingForecastResult forecast;
                if (sim.props.get_bool("/selftest/landing-shadow"))
                {
                    shadow_universe = std::make_unique<Universe>(sim.universe);
                    for (int i = 0; i < shadow_universe->world.body_count(); ++i)
                    {
                        sl::FlightBody *copy = shadow_universe->world.body(i);
                        if (!copy || !copy->active)
                            continue;
                        if (static_cast<std::size_t>(i) >= shadow_universe->bodies.size())
                        {
                            shadow_universe->world.destroy_body(i);
                            continue;
                        }
                        if (copy->terrain)
                        {
                            sl::TerrainConfig config = copy->terrain->config();
                            config.worker_threads = 0;
                            shadow_terrain.push_back(std::make_unique<sl::Terrain>(config, copy->terrain->source()));
                            copy->terrain = shadow_terrain.back().get();
                        }
                    }
                    const sl::FlightBody saved_body = ship->body();
                    shadow_ship = std::make_unique<Ship>(*shadow_universe, ship->spec());
                    shadow_ship->restore_carry_state(ship->carry_state());
                    shadow_ship->body() = saved_body;
                    shadow_ship->after_step(0.0);
                    if (sim.props.get_bool("/selftest/landing-plane-descent"))
                    {
                        Universe forecast_universe = *shadow_universe;
                        const sl::FlightBody forecast_body = shadow_ship->body();
                        forecast_universe.world.destroy_body(shadow_ship->body_id());
                        Ship forecast_ship(forecast_universe, ship->spec());
                        forecast_ship.restore_carry_state(shadow_ship->carry_state());
                        forecast_ship.body() = forecast_body;
                        forecast_ship.after_step(0.0);
                        const double before = forecast_universe.time();
                        const auto invalid = forecast_landing(forecast_universe, forecast_ship, pad, body_id, shuttle,
                                                              thrust_axis, sl::dvec3(0.0), dv, duration, thrust);
                        const auto dry = forecast_landing(forecast_universe, forecast_ship, pad, body_id, shuttle,
                                                          thrust_axis, direction, dv, duration, thrust,
                                                          forecast_ship.telemetry().stage_fuel);
                        if (invalid.status != LandingForecastStatus::invalid_input ||
                            dry.status != LandingForecastStatus::insufficient_fuel ||
                            forecast_universe.time() != before)
                        {
                            std::fprintf(stderr, "landing shadow forecast: invalid or insufficient fuel was not rejected\n");
                            return 1;
                        }
                        {
                            Universe limited_universe = forecast_universe;
                            limited_universe.world.destroy_body(forecast_ship.body_id());
                            Ship limited_ship(limited_universe, ship->spec());
                            limited_ship.restore_carry_state(forecast_ship.carry_state());
                            limited_ship.body() = forecast_body;
                            limited_ship.after_step(0.0);
                            const auto limited = forecast_landing(limited_universe, limited_ship, pad, body_id, shuttle,
                                                                  thrust_axis, direction, dv, duration, thrust,
                                                                  limited_ship.telemetry().stage_fuel - 1.0);
                            if (limited.status != LandingForecastStatus::insufficient_fuel)
                            {
                                std::fprintf(stderr, "landing shadow forecast: burn did not stop at fuel reserve\n");
                                return 1;
                            }
                        }
                        const double live_time = sim.universe.time();
                        const sl::dvec3 live_position = ship->body().position;
                        const double live_fuel = ship->telemetry().stage_fuel;
                        LandingForecastStepper stepper(forecast_universe, forecast_ship, pad, body_id, shuttle,
                                                       thrust_axis, direction, dv, duration, thrust);
                        const double forecast_time = forecast_universe.time();
                        if (stepper.advance(0) || forecast_universe.time() != forecast_time)
                        {
                            std::fprintf(stderr, "landing shadow forecast: zero-step budget changed the world\n");
                            return 1;
                        }
                        int batches = 0;
                        while (!stepper.advance(7))
                        {
                            if (++batches > 4000)
                            {
                                std::fprintf(stderr, "landing shadow forecast: step budget was not bounded\n");
                                return 1;
                            }
                        }
                        forecast = stepper.result();
                        if (!forecast.captured || sim.universe.time() != live_time ||
                            ship->body().position != live_position || ship->telemetry().stage_fuel != live_fuel)
                        {
                            std::fprintf(stderr, "landing shadow forecast: failed to reach capture or changed live flight\n");
                            return 1;
                        }
                        std::printf("landing shadow forecast: captured with %.1f kg fuel\n", forecast.fuel);
                    }
                }
                double delivered = 0.0;
                bool aligned = true;
                for (double elapsed = 0.0; elapsed < 40.0 + duration + dt; elapsed += dt)
                {
                    const sl::FlightBody &primary = *sim.universe.world.body(body_id);
                    const sl::dvec3 up = glm::normalize(ship->body().position - primary.position);
                    sl::dvec3 east = glm::cross(primary.spin_axis, up);
                    if (glm::length(east) < 1e-8)
                        east = glm::cross(sl::dvec3(1.0, 0.0, 0.0), up);
                    east = glm::normalize(east);
                    const sl::dvec3 north = glm::cross(up, east);
                    const double pitch = glm::degrees(std::asin(std::clamp(glm::dot(direction, up), -1.0, 1.0)));
                    const double heading_deg = std::fmod(glm::degrees(std::atan2(glm::dot(direction, east),
                                                                                glm::dot(direction, north))) + 360.0, 360.0);
                    sim.props.set_string("/controls/sas", "attitude");
                    sim.props.set_double("/controls/target-pitch", pitch);
                    sim.props.set_double("/controls/target-heading", heading_deg);
                    double level = 0.0;
                    if (elapsed >= 40.0 && delivered < dv)
                    {
                        if (glm::dot(ship->body().direction_to_world(thrust_axis), direction) < 0.995)
                        {
                            aligned = false;
                            break;
                        }
                        level = std::min(1.0, (dv - delivered) * ship->telemetry().mass / (thrust * dt));
                        delivered += thrust * level / ship->telemetry().mass * dt;
                    }
                    if (shuttle)
                    {
                        Controls controls;
                        controls.sas = "attitude";
                        controls.landing_attitude = true;
                        controls.landing_axis = thrust_axis;
                        controls.landing_direction = direction;
                        controls.hover = level;
                        ship->apply_controls(controls, dt);
                        sim.universe.world.step(dt);
                        ship->after_step(dt);
                    }
                    else
                    {
                        sim.props.set_double("/controls/hover", level);
                        sim.step(dt);
                    }
                    if (shadow_ship)
                    {
                        Controls controls;
                        controls.sas = "attitude";
                        controls.hover = level;
                        controls.gear = 1.0;
                        if (shuttle)
                        {
                            controls.landing_attitude = true;
                            controls.landing_axis = thrust_axis;
                            controls.landing_direction = direction;
                        }
                        else
                        {
                            controls.target_pitch = pitch;
                            controls.target_heading = heading_deg;
                        }
                        shadow_ship->apply_controls(controls, dt);
                        shadow_universe->world.max_substep = sim.universe.world.max_substep;
                        shadow_universe->world.step(dt);
                        shadow_ship->after_step(dt);
                    }
                }
                if (shadow_ship)
                {
                    const double position_error = glm::length(shadow_ship->body().position - ship->body().position);
                    const double velocity_error = glm::length(shadow_ship->body().velocity - ship->body().velocity);
                    const double fuel_error = std::abs(shadow_ship->telemetry().stage_fuel - ship->telemetry().stage_fuel);
                    std::printf("landing shadow burn: position error %.3f m, velocity error %.6f m/s, fuel error %.3f kg\n",
                                position_error, velocity_error, fuel_error);
                    if (!std::isfinite(position_error) || !std::isfinite(velocity_error) || !std::isfinite(fuel_error) ||
                        position_error > 10.0 || velocity_error > 0.2 || fuel_error > 10.0)
                    {
                        std::fprintf(stderr, "landing shadow burn selftest: copied world diverged\n");
                        return 1;
                    }
                }
                if (shuttle)
                {
                    Controls idle;
                    idle.sas = "stability";
                    ship->apply_controls(idle, dt);
                    sim.universe.world.step(dt);
                    ship->after_step(dt);
                }
                else
                {
                    sim.props.set_double("/controls/hover", 0.0);
                    sim.step(dt);
                }
                if (shadow_ship)
                {
                    Controls idle;
                    if (!shuttle)
                    {
                        idle.sas = "attitude";
                        idle.target_pitch = sim.props.get_double("/controls/target-pitch");
                        idle.target_heading = sim.props.get_double("/controls/target-heading");
                        idle.gear = 1.0;
                    }
                    shadow_ship->apply_controls(idle, dt);
                    shadow_universe->world.max_substep = sim.universe.world.max_substep;
                    shadow_universe->world.step(dt);
                    shadow_ship->after_step(dt);
                }
                const sl::FlightBody &primary = *sim.universe.world.body(body_id);
                const sl::dvec3 r = ship->body().position - primary.position;
                const sl::dvec3 v = ship->body().velocity - primary.velocity;
                const sl::dvec3 normal = glm::normalize(glm::cross(r, v));
                const double error = std::abs(glm::dot(normal, plan.arrival_target)) * body_spec.radius;
                const double expected_fuel = burn.fuel;
                const double fuel_used = initial_fuel - ship->telemetry().stage_fuel;
                const bool ok = aligned && std::abs(delivered - dv) < 0.1 && error < 1000.0 &&
                                std::abs(fuel_used - expected_fuel) < std::max(1.0, expected_fuel * 0.05);
                std::printf("landing plane burn selftest: %d failure(s), dv %.2f m/s, delivered %.2f m/s, fuel %.1f kg, plane error %.1f m\n",
                            ok ? 0 : 1, dv, delivered, fuel_used, error);
                if (ok && sim.props.get_bool("/selftest/landing-plane-descent"))
                {
                    const double start_time = sim.universe.time();
                    const double shadow_start_time = shadow_universe ? shadow_universe->time() : 0.0;
                    bool captured = false;
                    for (int step = 0; step < 18000; ++step)
                    {
                        const sl::FlightBody &primary_now = *sim.universe.world.body(body_id);
                        const sl::dvec3 r_now = ship->body().position - primary_now.position;
                        const sl::dvec3 v_now = ship->body().velocity - primary_now.velocity;
                        const double rmag = glm::length(r_now);
                        const sl::dvec3 up = r_now / rmag;
                        pad.direction = base_direction(sim.universe, body_id, pad);
                        pad.pole = sim.universe.pole(body_id);
                        sl::dvec3 ground;
                        base_pad(pad, body_spec.radius, ground, heading);
                        const double distance = rmag * std::acos(std::clamp(glm::dot(up, ground), -1.0, 1.0));
                        const Telemetry &tm = ship->telemetry();
                        const sl::dvec3 surface_v = ship->relative_velocity();
                        const double hspeed = glm::length(surface_v - up * glm::dot(surface_v, up));
                        if (distance < 1500.0 && tm.altitude > 300.0 && tm.altitude < 3000.0 &&
                            hspeed < 40.0 && std::abs(tm.vertical_speed) < 25.0)
                        {
                            std::printf("descent probe capture t=%.1f alt=%.0f distance=%.0f hspeed=%.1f vs=%.1f fuel=%.0f\n",
                                        sim.universe.time() - start_time, tm.altitude, distance, hspeed, tm.vertical_speed, tm.stage_fuel);
                            sim.props.set_double("/controls/hover", 0.0);
                            sim.props.set_bool("/autopilot/land/targeted", true);
                            sim.props.set_string("/autopilot/land/base", base_id);
                            sim.props.set_string("/autopilot/land/state", "off");
                            sim.props.set_bool("/autopilot/land/armed", true);
                            captured = true;
                            break;
                        }
                        const double remaining = std::max(650.0 - (sim.universe.time() - start_time), 60.0);
                        const LandingDescentCommand guidance =
                            landing_descent_command(r_now, v_now, ground, pad.pole, body_spec.spin,
                                                    body_spec.gm, body_spec.radius + pad.height + pad.pad_deck + 2500.0, remaining);
                        if (!guidance.valid || glm::length(guidance.acceleration) < 1e-8)
                        {
                            std::fprintf(stderr, "landing descent probe: invalid acceleration command\n");
                            break;
                        }
                        const sl::dvec3 command = guidance.acceleration;
                        const sl::dvec3 desired = glm::normalize(command);
                        sl::dvec3 east = glm::cross(primary_now.spin_axis, up);
                        if (glm::length(east) < 1e-8)
                            east = glm::cross(sl::dvec3(1.0, 0.0, 0.0), up);
                        east = glm::normalize(east);
                        const sl::dvec3 north = glm::cross(up, east);
                        const double pitch = glm::degrees(std::asin(std::clamp(glm::dot(desired, up), -1.0, 1.0)));
                        const double heading_deg = std::fmod(glm::degrees(std::atan2(glm::dot(desired, east),
                                                                                    glm::dot(desired, north))) + 360.0, 360.0);
                        sim.props.set_string("/controls/sas", "attitude");
                        sim.props.set_double("/controls/target-pitch", pitch);
                        sim.props.set_double("/controls/target-heading", heading_deg);
                        const double alignment = glm::dot(ship->body().direction_to_world(thrust_axis), desired);
                        const double descent_level = alignment > 0.98 ?
                            std::clamp(glm::length(command) * tm.mass / thrust, 0.0, 1.0) : 0.0;
                        if (shuttle)
                        {
                            Controls controls;
                            controls.sas = "attitude";
                            controls.landing_attitude = true;
                            controls.landing_axis = thrust_axis;
                            controls.landing_direction = desired;
                            controls.hover = descent_level;
                            ship->apply_controls(controls, dt);
                            sim.universe.world.step(dt);
                            ship->after_step(dt);
                        }
                        else
                        {
                            sim.props.set_double("/controls/hover", descent_level);
                            sim.step(dt);
                        }
                        if (shadow_ship)
                        {
                            const sl::FlightBody &primary_copy = *shadow_universe->world.body(body_id);
                            const sl::dvec3 r_copy = shadow_ship->body().position - primary_copy.position;
                            const sl::dvec3 v_copy = shadow_ship->body().velocity - primary_copy.velocity;
                            Base shadow_pad = pad;
                            shadow_pad.direction = base_direction(*shadow_universe, body_id, shadow_pad);
                            shadow_pad.pole = shadow_universe->pole(body_id);
                            sl::dvec3 ground_copy;
                            double heading_copy;
                            base_pad(shadow_pad, body_spec.radius, ground_copy, heading_copy);
                            const auto predicted_command =
                                landing_descent_command(r_copy, v_copy, ground_copy, shadow_pad.pole, body_spec.spin,
                                                        body_spec.gm, body_spec.radius + shadow_pad.height + shadow_pad.pad_deck + 2500.0,
                                                        std::max(650.0 - (shadow_universe->time() - shadow_start_time), 60.0));
                            if (!predicted_command.valid || glm::length(predicted_command.acceleration) < 1e-8)
                            {
                                std::fprintf(stderr, "landing shadow descent selftest: invalid command\n");
                                return 1;
                            }
                            const sl::dvec3 wanted = glm::normalize(predicted_command.acceleration);
                            Controls controls;
                            controls.sas = "attitude";
                            controls.gear = 1.0;
                            if (shuttle)
                            {
                                controls.landing_attitude = true;
                                controls.landing_axis = thrust_axis;
                                controls.landing_direction = wanted;
                            }
                            else
                            {
                                const sl::dvec3 copy_up = glm::normalize(r_copy);
                                sl::dvec3 copy_east = glm::cross(primary_copy.spin_axis, copy_up);
                                if (glm::length(copy_east) < 1e-8)
                                    copy_east = glm::cross(sl::dvec3(1, 0, 0), copy_up);
                                copy_east = glm::normalize(copy_east);
                                const sl::dvec3 copy_north = glm::cross(copy_up, copy_east);
                                controls.target_pitch = glm::degrees(std::asin(std::clamp(glm::dot(wanted, copy_up), -1.0, 1.0)));
                                controls.target_heading = std::fmod(glm::degrees(std::atan2(glm::dot(wanted, copy_east),
                                                                                           glm::dot(wanted, copy_north))) + 360.0, 360.0);
                            }
                            controls.hover = glm::dot(shadow_ship->body().direction_to_world(thrust_axis), wanted) > 0.98 ?
                                std::clamp(glm::length(predicted_command.acceleration) * shadow_ship->telemetry().mass / thrust,
                                           0.0, 1.0) : 0.0;
                            shadow_ship->apply_controls(controls, dt);
                            shadow_universe->world.max_substep = sim.universe.world.max_substep;
                            shadow_universe->world.step(dt);
                            shadow_ship->after_step(dt);
                        }
                        if (step % 1000 == 0)
                            std::printf("descent probe t=%.1f alt=%.0f distance=%.0f hspeed=%.1f vs=%.1f fuel=%.0f\n",
                                        sim.universe.time() - start_time, tm.altitude, distance, hspeed, tm.vertical_speed, tm.stage_fuel);
                        if (ship->crashed() || tm.stage_fuel < 1.0)
                            break;
                    }
                    std::printf("landing descent probe: %s\n", captured ? "captured" : "not captured");
                    if (shadow_ship && captured)
                    {
                        const double position_error = glm::length(shadow_ship->body().position - ship->body().position);
                        const double velocity_error = glm::length(shadow_ship->body().velocity - ship->body().velocity);
                        const double fuel_error = std::abs(shadow_ship->telemetry().stage_fuel - ship->telemetry().stage_fuel);
                        const sl::FlightBody &primary_copy = *shadow_universe->world.body(body_id);
                        const sl::dvec3 r_copy = shadow_ship->body().position - primary_copy.position;
                        const sl::dvec3 copy_up = glm::normalize(r_copy);
                        Base shadow_pad = pad;
                        shadow_pad.direction = base_direction(*shadow_universe, body_id, shadow_pad);
                        shadow_pad.pole = shadow_universe->pole(body_id);
                        sl::dvec3 ground_copy;
                        double heading_copy;
                        base_pad(shadow_pad, body_spec.radius, ground_copy, heading_copy);
                        const double distance = glm::length(r_copy) *
                                                std::acos(std::clamp(glm::dot(copy_up, ground_copy), -1.0, 1.0));
                        const sl::dvec3 surface_v = shadow_ship->relative_velocity();
                        const double horizontal = glm::length(surface_v - copy_up * glm::dot(surface_v, copy_up));
                        const Telemetry &shadow_tm = shadow_ship->telemetry();
                        const bool shadow_captured = distance < 1500.0 && shadow_tm.altitude > 300.0 &&
                                                     shadow_tm.altitude < 3000.0 && horizontal < 40.0 &&
                                                     std::abs(shadow_tm.vertical_speed) < 25.0;
                        std::printf("landing shadow descent: %s, position error %.3f m, velocity error %.6f m/s, fuel error %.3f kg\n",
                                    shadow_captured ? "captured" : "not captured", position_error, velocity_error, fuel_error);
                        if (!std::isfinite(position_error) || !std::isfinite(velocity_error) || !std::isfinite(fuel_error) ||
                            !shadow_captured || position_error > 100.0 || velocity_error > 2.0 || fuel_error > 100.0)
                        {
                            std::fprintf(stderr, "landing shadow descent selftest: copied world diverged\n");
                            return 1;
                        }
                        const double forecast_position = glm::length(forecast.position - ship->body().position);
                        const double forecast_velocity = glm::length(forecast.velocity - ship->body().velocity);
                        const double forecast_fuel = std::abs(forecast.fuel - ship->telemetry().stage_fuel);
                        std::printf("landing shadow forecast error: %.3f m, %.6f m/s, %.3f kg\n",
                                    forecast_position, forecast_velocity, forecast_fuel);
                        if (!forecast.captured || !std::isfinite(forecast_position) ||
                            !std::isfinite(forecast_velocity) || !std::isfinite(forecast_fuel) ||
                            forecast_position > 100.0 || forecast_velocity > 2.0 || forecast_fuel > 100.0)
                        {
                            std::fprintf(stderr, "landing shadow forecast: diverged from actual descent\n");
                            return 1;
                        }
                        const double orbit_position_error = glm::length(orbital_forecast.position - ship->body().position);
                        const double orbit_velocity_error = glm::length(orbital_forecast.velocity - ship->body().velocity);
                        const double orbit_fuel_error = std::abs(orbital_forecast.fuel - ship->telemetry().stage_fuel);
                        std::printf("landing candidate forecast error: %.3f m, %.6f m/s, %.3f kg\n",
                                    orbit_position_error, orbit_velocity_error, orbit_fuel_error);
                        if (!orbital_forecast.captured || !std::isfinite(orbit_position_error) ||
                            !std::isfinite(orbit_velocity_error) || !std::isfinite(orbit_fuel_error) ||
                            orbit_position_error > 100.0 || orbit_velocity_error > 2.0 || orbit_fuel_error > 100.0)
                        {
                            std::fprintf(stderr, "landing candidate forecast: diverged from actual descent\n");
                            return 1;
                        }
                    }
                    if (!captured)
                    {
                        sim.props.set_double("/controls/hover", 0.0);
                        Controls idle;
                        ship->apply_controls(idle, 0.0);
                        return 1;
                    }
                    for (int step = 0; step < 10000 && sim.props.get_bool("/autopilot/land/armed"); ++step)
                        sim.step(dt);
                    const Telemetry &tm = ship->telemetry();
                    const bool landed = sim.props.get_string("/autopilot/land/state") == "Autoland: landed" &&
                                        !ship->crashed() && sim.props.get_double("/autopilot/land/distance") < 50.0 &&
                                        tm.surface_speed < 2.0 && tm.stage_fuel > 100.0;
                    std::printf("landing descent probe result: %s distance %.1f speed %.1f fuel %.1f\n",
                                sim.props.get_string("/autopilot/land/state").c_str(),
                                sim.props.get_double("/autopilot/land/distance"), tm.surface_speed, tm.stage_fuel);
                    if (shadow_ship && landed &&
                        (std::abs(touchdown_forecast.distance - sim.props.get_double("/autopilot/land/distance")) > 100.0 ||
                         std::abs(touchdown_forecast.fuel - tm.stage_fuel) > 100.0))
                    {
                        std::fprintf(stderr, "landing touchdown forecast: diverged from actual landing\n");
                        return 1;
                    }
                    return landed ? 0 : 1;
                }
                return ok ? 0 : 1;
            }
            if (plan.wait > 0.0 && !sim.skip_time(plan.wait, why))
            {
                std::fprintf(stderr, "landing plane selftest: departure skip failed: %s\n", why.c_str());
                return 1;
            }
            const sl::FlightBody &departure_planet = *sim.universe.world.body(body_id);
            const double position_error = glm::length(ship->body().position - departure_planet.position - plan.departure_position);
            const double velocity_error = glm::length(ship->body().velocity - departure_planet.velocity - plan.departure_velocity);
            if (!sim.skip_time(flight_time, why))
            {
                std::fprintf(stderr, "landing plane selftest: arrival skip failed: %s\n", why.c_str());
                return 1;
            }
            pad.direction = base_direction(sim.universe, body_id, pad);
            pad.pole = sim.universe.pole(body_id);
            sl::dvec3 actual_target;
            base_pad(pad, body_spec.radius, actual_target, heading);
            const double target_error = glm::length(actual_target - plan.arrival_target) * body_spec.radius;
            const sl::dvec3 normal = glm::normalize(glm::cross(plan.departure_position,
                                                               plan.departure_velocity + plan.change.delta_v));
            const double plane_error = std::abs(glm::dot(normal, plan.arrival_target)) * body_spec.radius;
            const bool ok = position_error < 1.0 && velocity_error < 0.001 && target_error < 1.0 && plane_error < 0.001;
            std::printf("landing plane selftest: %d failure(s), wait %.1f s, dv %.1f m/s, errors %.4f m %.7f m/s %.4f m %.7f m\n",
                        ok ? 0 : 1, plan.wait, glm::length(plan.change.delta_v), position_error,
                        velocity_error, target_error, plane_error);
            return ok ? 0 : 1;
        }
        const std::string landing_binding = sim.props.get_string("/selftest/autoland-binding");
        if (!landing_binding.empty())
        {
            if (landing_binding == "stage")
            {
                ShipSpec spec = ship->spec();
                spec.stages.push_back(spec.stages.front());
                const int body_id = ship->telemetry().soi;
                const sl::FlightBody &body = *sim.universe.world.body(body_id);
                Ship *staged = sim.spawn(spec);
                staged->place_relative(body_id, ship->body().position - body.position,
                                       ship->body().velocity - body.velocity, ship->body().orientation);
                staged->after_step(0.0);
                sim.activate(sim.ship_count() - 1);
                ship = staged;
            }
            sim.props.set_bool("/autopilot/land/targeted", true);
            sim.props.set_double("/autopilot/land/latitude", ship->telemetry().latitude);
            sim.props.set_double("/autopilot/land/longitude", ship->telemetry().longitude);
            sim.props.set_string("/autopilot/land/state", "off");
            sim.props.set_bool("/autopilot/land/armed", true);
            sim.step(0.05);
            const bool started = sim.props.get_bool("/autopilot/land/armed") &&
                                 sim.props.get_string("/autopilot/land/state") == "Target approach";
            std::string skip_reason;
            const double before_skip = sim.universe.time();
            const bool skip_blocked = !sim.skip_time(10.0, skip_reason) &&
                                      skip_reason == "Cancel the active autopilot before skipping time" &&
                                      sim.universe.time() == before_skip;
            bool changed = false;
            if (landing_binding == "stage")
                changed = ship->stage_once();
            else if (landing_binding == "body")
            {
                const int moon = sim.universe.find("Moon");
                if (moon >= 0)
                {
                    ship->place_in_orbit(moon, 200000.0, 0.0);
                    ship->after_step(0.0);
                    changed = ship->telemetry().soi == moon;
                }
            }
            sim.step(0.05);
            const bool stopped = !sim.props.get_bool("/autopilot/land/armed") &&
                                 sim.props.get_string("/autopilot/land/state") == "Autoland aborted: body or stage changed" &&
                                 sim.props.get_double("/controls/hover") == 0.0;
            const bool ok = started && skip_blocked && changed && stopped;
            std::printf("autoland %s binding selftest: %d failure(s)\n", landing_binding.c_str(), ok ? 0 : 1);
            return ok ? 0 : 1;
        }
        if (sim.props.get_bool("/selftest/base-pad"))
        {
            auto base = std::find_if(sim.bases.begin(), sim.bases.end(), [](const Base &b) { return b.id == "gale"; });
            if (base == sim.bases.end())
            {
                std::fprintf(stderr, "base pad selftest: Gale base missing\n");
                return 1;
            }
            base->pad_x = 500.0;
            base->pad_z = 0.0;
            sim.props.set_bool("/autopilot/land/targeted", true);
            sim.props.set_string("/autopilot/land/base", "gale");
            sim.props.set_bool("/autopilot/land/armed", true);
            sim.step(0.05);
            const double distance = sim.props.get_double("/autopilot/land/distance");
            const bool ok = sim.props.get_bool("/autopilot/land/armed") && distance > 490.0 && distance < 510.0;
            std::printf("base pad selftest: %d failure(s), distance %.1f m\n", ok ? 0 : 1, distance);
            return ok ? 0 : 1;
        }
        if (sim.props.get_bool("/selftest/earth-communications"))
        {
            int failures = 0;
            const auto check = [&](bool ok, const char *what) {
                std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
                failures += ok ? 0 : 1;
            };
            const int earth = sim.universe.find("Earth");
            sl::FlightBody *target = earth >= 0 ? sim.universe.world.body(earth) : nullptr;
            if (!target)
            {
                check(false, "Earth exists");
                return 1;
            }
            Controls controls;
            controls.sas = "earth-communications";
            bool valid = false;
            const auto alignment = [&]() {
                const sl::dvec3 direction = ship->sas_direction(controls, valid);
                return valid ? glm::dot(ship->body().direction_to_world(sl::dvec3(0, 0, -1)), direction) : -1.0;
            };
            check(sim.props.get_string("/controls/sas") == controls.sas, "scenario enables Earth communications");
            check(sim.props.get_string("/sim/target") == "Jupiter", "navigation target remains Jupiter");
            check(alignment() > 0.999999999, "dish points at Earth initially");
            const sl::dvec3 position = target->position;
            const sl::dvec3 old_direction = ship->sas_direction(controls, valid);
            target->position += sl::dvec3(0, 1.0e11, 0);
            const sl::dvec3 new_direction = ship->sas_direction(controls, valid);
            check(valid && glm::length(new_direction - old_direction) > 0.01,
                  "Earth direction is recomputed as the body moves");
            target->position = position;
            target->active = false;
            ship->sas_direction(controls, valid);
            check(!valid, "unavailable Earth produces invalid direction");
            target->active = true;
            ship->body().orientation = glm::angleAxis(1.5707963267948966, sl::dvec3(0, 1, 0)) * ship->body().orientation;
            ship->body().angular_velocity = sl::dvec3(0);
            for (int i = 0; i < 2400; ++i)
                sim.step(0.05);
            const double final_alignment = alignment();
            std::printf("Earth communications alignment %.12f\n", final_alignment);
            check(final_alignment > 0.9999, "SAS reacquires Earth after a 90 degree disturbance");
            const sl::dvec3 turn_axis = ship->body().direction_to_world(sl::dvec3(0, 1, 0));
            ship->body().orientation = glm::angleAxis(3.141592653589793, turn_axis) * ship->body().orientation;
            ship->body().angular_velocity = sl::dvec3(0);
            for (int i = 0; i < 3600; ++i)
                sim.step(0.05);
            check(alignment() > 0.9999, "SAS reacquires Earth from the opposite direction");
            sim.props.set_int("/sim/warp-index", 8);
            sim.step(0.05);
            check(ship->on_rails(), "high warp suspends Earth attitude control on rails");
            sim.props.set_int("/sim/warp-index", 0);
            sim.step(0.05);
            check(!ship->on_rails() && sim.props.get_string("/controls/sas") == controls.sas,
                  "Earth hold resumes after returning to physics flight");
            const sl::dquat saved_orientation = ship->body().orientation;
            const Scenario saved = capture_scenario(sim, "Earth communications");
            Scenario parsed;
            std::string error;
            const bool parsed_ok = parse_scenario(scenario_to_json(saved), "communications-save.json", parsed, error);
            check(parsed_ok, "communications save parses");
            if (parsed_ok)
            {
                Ship *restored = app::apply_scenario(sim, parsed, app::TerrainSet{});
                check(restored && sim.props.get_string("/controls/sas") == controls.sas,
                      "saved flight restores Earth communications hold");
                check(restored && std::abs(glm::dot(saved_orientation, restored->body().orientation)) > 0.999999999,
                      "saved flight preserves attitude rather than reorienting");
            }
            std::printf("Earth communications selftest: %d failure(s)\n", failures);
            return failures ? 1 : 0;
        }
        if (sim.props.get_bool("/selftest/gravity-j2"))
        {
            int failures = 0;
            const auto check = [&](bool ok, const char *what) {
                std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
                failures += ok ? 0 : 1;
            };
            const int earth = sim.universe.find("Earth"), mars = sim.universe.find("Mars");
            {
                Universe invalid;
                std::string error;
                const bool loaded = invalid.load_text(
                    R"({"name":"Invalid","bodies":[{"name":"Star","kind":"star","gravity_model":"__missing_gravity_20261107.json"}]})",
                    "data/__gravity_test_system__.json", error);
                check(!loaded && error.find("cannot open gravity model") != std::string::npos,
                      "missing gravity model is reported");
            }
            check(earth >= 0 && mars >= 0 &&
                      sim.universe.world.body(earth)->gravity_j2 > 0.001 &&
                      sim.universe.world.body(mars)->gravity_j2 > 0.001,
                  "Earth and Mars carry J2 data");
            for (const char *name : {"Mercury", "Venus", "Earth", "Moon", "Mars", "Ceres", "Vesta"})
            {
                const int id = sim.universe.find(name);
                sl::FlightBody *body = id >= 0 ? sim.universe.world.body(id) : nullptr;
                check(body && body->gravity_harmonics &&
                          body->gravity_harmonics->degree == (std::string(name) == "Ceres" ? 70 : 10),
                      name);
                if (body && body->gravity_harmonics)
                {
                    const sl::dquat original = body->gravity_orientation;
                    const auto frame = body->gravity_frame;
                    body->gravity_frame = {};
                    body->gravity_orientation = glm::angleAxis(-body->spin_rate * sim.universe.world.time(), sl::dvec3(0, 1, 0));
                    const sl::dvec3 point = body->position + body->gravity_harmonics->reference_radius * sl::dvec3(1.05, 0.35, -0.25);
                    sim.universe.world.nonspherical_gravity = true;
                    const sl::dvec3 enabled = sim.universe.world.gravity_at(point);
                    sim.universe.world.nonspherical_gravity = false;
                    const sl::dvec3 delta = enabled - sim.universe.world.gravity_at(point);
                    std::printf("gravity-model %s %.17g %.17g %.17g\n", name, delta.x, delta.y, delta.z);
                    body->gravity_orientation = original;
                    body->gravity_frame = frame;
                    if (frame)
                        for (double t : {0.0, 86400.0})
                        {
                            const sl::dquat q = frame(t);
                            const sl::dvec3 prime = q * sl::dvec3(1, 0, 0);
                            const sl::dvec3 pole = q * sl::dvec3(0, 1, 0);
                            std::printf("gravity-frame %s %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g\n",
                                        name, sim.universe.epoch_jd, t, prime.x, prime.y, prime.z, pole.x, pole.y, pole.z);
                        }
                }
            }
            sl::PropertyTree settings;
            settings_defaults(settings);
            check(!settings.get_bool("/sim/settings/nonspherical-gravity"), "J2 defaults off");
            settings.set_bool("/sim/settings/nonspherical-gravity", true);
            settings_save(settings);
            sl::PropertyTree restored;
            settings_defaults(restored);
            settings_load(restored);
            check(restored.get_bool("/sim/settings/nonspherical-gravity"), "J2 setting persists");
            sim.props.set_bool("/sim/settings/nonspherical-gravity", true);
            sim.step(0.05);
            check(sim.universe.world.nonspherical_gravity && !ship->on_rails(), "J2 enabled during physics");
            const sl::dvec3 point = ship->body().position;
            const sl::dvec3 with_j2 = sim.universe.world.gravity_at(point);
            sim.universe.world.nonspherical_gravity = false;
            const sl::dvec3 without_j2 = sim.universe.world.gravity_at(point);
            check(glm::length(with_j2 - without_j2) > 1e-5, "configured J2 changes gravity");
            sim.universe.world.nonspherical_gravity = true;
            sim.props.set_int("/sim/warp-index", 8);
            sim.step(0.05);
            check(ship->on_rails(), "high warp uses Keplerian rails with J2 suspended");
            sim.props.set_int("/sim/warp-index", 0);
            sim.step(0.05);
            check(!ship->on_rails() && sim.universe.world.nonspherical_gravity, "J2 resumes after rails");
            std::string why;
            const sl::FlightBody *parent = sim.universe.world.body(ship->telemetry().soi);
            sl::OrbitElements orbit;
            const bool valid_orbit = sl::orbit_from_state(parent->gm, ship->body().position - parent->position,
                                                           ship->body().velocity - parent->velocity, orbit);
            const sl::OrbitState expected = sl::state_from_orbit(parent->gm, orbit, 60.0);
            const bool skipped = sim.skip_time(60.0, why);
            check(skipped && !sim.messages().empty() && sim.messages().back().find("nonspherical gravity suspended") != std::string::npos,
                  "time skip reports suspended nonspherical gravity");
            parent = sim.universe.world.body(ship->telemetry().soi);
            check(skipped && valid_orbit && glm::length(ship->body().position - parent->position - expected.position) < 0.1,
                  "time skip remains Keplerian with J2 enabled");
            sim.props.set_bool("/sim/settings/nonspherical-gravity", false);
            sim.step(0.05);
            check(!sim.universe.world.nonspherical_gravity, "J2 switches off at runtime");
            std::printf("J2 selftest: %d failure(s)\n", failures);
            return failures ? 1 : 0;
        }
        if (options.orbitcomp)
            return run_orbitcomp_test(sim, ship);
        if (options.save)
            return run_save_test(sim, ship);
        if (options.timed_burn)
            return run_timed_burn_test(sim, ship);
        if (options.orbital_sas)
            return run_orbital_sas_test(sim, ship);
        if (options.ui_windows)
            return run_ui_windows_test(sim);
        if (options.vessel_ascent)
            return run_vessel_ascent_test(sim);
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
        bool dropped = false, ascent_was_active = false, landing_was_active = false;
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
            sim.advance(dt);
            const kosmos::Telemetry &tm = ship->telemetry();
            if (ascent_was_active && !sim.vessel_ascent_active())
                std::printf("ascent result: %s ap=%.3f km pe=%.3f km bound=%d fuel=%.1f\n",
                            sim.props.get_string("/autopilot/vessel-ascent/state").c_str(),
                            tm.apoapsis / km, tm.periapsis / km, tm.bound ? 1 : 0, tm.stage_fuel);
            ascent_was_active = sim.vessel_ascent_active();
            if (landing_was_active && !sim.props.get_bool("/autopilot/land/armed") &&
                sim.props.get_bool("/autopilot/land/targeted"))
                std::printf("target landing result: %s distance=%.1f m speed=%.1f fuel=%.1f crashed=%d\n",
                            sim.props.get_string("/autopilot/land/state").c_str(),
                            sim.props.get_double("/autopilot/land/distance"), tm.surface_speed, tm.stage_fuel, ship->crashed() ? 1 : 0);
            landing_was_active = sim.props.get_bool("/autopilot/land/armed");
            const double report_every = ship->spec().aircraft() ? 2.0 : 10.0;
            if (sim.universe.time() >= next_report)
            {
                next_report = sim.universe.time() + report_every;
                if (ship->spec().aircraft())
                    std::printf("t=%5.0f ias=%5.1f alt=%6.1f vs=%5.1f pitch=%5.1f roll=%5.1f aoa=%5.1f slip=%5.1f hdg=%5.1f landed=%d fuel=%4.0f gear=%.2f%s\n", sim.universe.time(), tm.surface_speed,
                                tm.altitude, tm.vertical_speed, tm.pitch, tm.roll, tm.alpha, tm.sideslip, tm.heading, tm.landed ? 1 : 0, tm.stage_fuel, ship->gear_position(), ship->crashed() ? " CRASHED" : "");
                if (ship->spec().aircraft() && sim.props.get_bool("/nav/nav[0]/valid"))
                    std::printf("   nav0: loc %.2f gs %.2f dist %.0f\n", sim.props.get_double("/nav/nav[0]/ils/localizer"), sim.props.get_double("/nav/nav[0]/ils/glideslope"), sim.props.get_double("/nav/nav[0]/distance"));
                if (!ship->spec().aircraft())
                std::printf("t=%6.0f %-24s alt=%8.4f km speed=%7.1f vs=%7.1f ap=%8.1f pe=%8.1f pitch=%5.1f stage=%d fuel=%8.0f m=%7.0f warp=%g active=%d docked=%d links=%zu%s\n",
                            sim.universe.time(), sim.props.get_string("/autopilot/status").c_str(), tm.altitude / km, tm.surface_speed,
                            tm.vertical_speed, tm.apoapsis / km, tm.periapsis / km, tm.pitch, ship->active_stage(), tm.stage_fuel,
                            tm.mass, sim.current_warp(), sim.props.get_int("/sim/active-ship"), sim.nav.docked(sim.props.get_int("/sim/active-ship")) ? 1 : 0,
                            sim.nav.dock_links().size(),
                            ship->crashed() ? " CRASHED" : "");
                if (sim.vessel_ascent_active())
                    std::printf("   ascent: %s q=%.1f Pa throttle=%.3f hover=%.3f ap=%.1f km pe=%.1f km heading=%.1f\n",
                                sim.props.get_string("/autopilot/vessel-ascent/state").c_str(), tm.dynamic_pressure,
                                sim.props.get_double("/controls/throttle"), sim.props.get_double("/controls/hover"),
                                tm.apoapsis / km, tm.periapsis / km, tm.heading);
                if (sim.props.get_bool("/target/valid"))
                    std::printf("   target %s: distance %.0f km\n", sim.props.get_string("/sim/target").c_str(), sim.props.get_double("/target/distance") / km);
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
