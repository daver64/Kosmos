#include "selftest.h"

#include "gui_input.h"
#include "menu.h"
#include "ordnance.h"
#include "scenario_start.h"
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
        sim.step(0.05);
        check(std::abs(capture_scenario(sim, "Date test").epoch_jd - sim.universe.julian_date()) < 1.0e-9,
              "captured scenario retains current date");
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

    static int run_ui_windows_test()
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
            sim.mfd.press(0, 0);
            sim.step(0.05);
            check(sim.vessel_ascent_active() && sim.props.get_double("/controls/hover") > 0.0,
                  "MFD starts ascent with docked cargo");
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
            sim.mfd.press(0, 0);
            sim.step(0.05);
            check(sim.vessel_ascent_active() && displays("Liftoff") && sim.props.get_double("/controls/throttle") > 0.0,
                  "MFD START engages Deltaglider ascent");
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
                      sim.props.get_double("/controls/throttle") == 0.0,
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
        if (options.orbitcomp)
            return run_orbitcomp_test(sim, ship);
        if (options.save)
            return run_save_test(sim, ship);
        if (options.timed_burn)
            return run_timed_burn_test(sim, ship);
        if (options.orbital_sas)
            return run_orbital_sas_test(sim, ship);
        if (options.ui_windows)
            return run_ui_windows_test();
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
        bool dropped = false, ascent_was_active = false;
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
