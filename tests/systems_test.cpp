#undef NDEBUG
#include "systems.h"

#include "ship.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#include <cassert>
#include <cmath>
#include <cstdio>

using namespace kosmos;

namespace
{
    SystemsSpec parse(const char *text)
    {
        SystemsSpec spec;
        parse_systems(nlohmann::json::parse(text), {"Lower", "Capsule"}, spec);
        return spec;
    }
} // namespace

int main()
{
    {
        Universe universe;
        std::string error;
        assert(universe.load(std::string(KOSMOS_ROOT) + "/data/sol.json", error));
        ShipSpec spec;
        spec.name = "PropellantStateTest";
        spec.rcs_propellant = 12.0;
        StageSpec lower;
        lower.name = "Lower";
        lower.propellant = 100.0;
        lower.gear.push_back({0.0, -1.0, 0.0});
        StageSpec upper;
        upper.name = "Upper";
        upper.propellant = 50.0;
        spec.stages = {lower, upper};

        Ship source(universe, spec);
        assert(source.stage_once());
        source.body().tanks[1].mass = 20.0;
        source.set_gear_position(0.25);
        const ShipPropellantState state = source.propellant_state();

        Ship arrival(universe, spec);
        arrival.restore_propellant_state(state);
        assert(arrival.active_stage() == 1);
        assert(arrival.body().tanks[0].mass == 0.0);
        assert(arrival.body().tanks[1].mass == 20.0);
        assert(arrival.propellant_state().rcs_propellant == 12.0);
        assert(arrival.gear_position() == 0.25);
    }

    // Parsing: stages default to the last one, tanks start full, vessels read their box.
    SystemsSpec spec = parse(R"({
      "systems": [
        {"type":"battery","capacity_wh":1000,"charge":0.5},
        {"type":"tank","gas":"oxygen","capacity_kg":10},
        {"type":"tank","gas":"nitrogen","capacity_kg":10,"stage":"Lower"},
        {"type":"regulator","vessel":"Cabin"},
        {"type":"scrubber","vessel":"Cabin","kg_per_day":10},
        {"type":"solar","area_m2":10,"efficiency":0.2,"facing":[0,1,0]},
        {"type":"load","name":"avionics","watts":100}
      ],
      "vessels": [{"name":"Cabin","size":[2,2,2.5],"crew":2,"pressure_kpa":101.3}]
    })");
    assert(spec.components.size() == 7 && spec.components[0].stage == 1 && spec.components[2].stage == 0);
    assert(spec.vessels.size() == 1 && spec.vessels[0].crew == 2);

    {
        // Sunlit solar panels carry the bus and charge the battery; in eclipse the battery drains.
        Systems sys(spec, LifeSupportMode::warn);
        SystemsContext sun;
        sun.sun_dir = {0.0, 1.0, 0.0};
        sun.sun_flux = 1.0;
        const double wh0 = sys.battery_wh();
        sun.mission_time = 60.0;
        sys.step(60.0, sun);
        assert(sys.powered());
        assert(sys.battery_wh() > wh0);
        assert(sys.supply_w() > 2000.0 && sys.supply_w() < 2800.0); // 10 m2 * 0.2 * 1361 W/m2

        SystemsContext dark = sun;
        dark.sun_flux = 0.0;
        const double wh1 = sys.battery_wh();
        dark.mission_time = 120.0;
        sys.step(60.0, dark);
        assert(sys.battery_wh() < wh1);
    }
    {
        // A sun-tracking array ignores its facing and its output follows the flux: full at 1 AU, a quarter at 2 AU, none in eclipse.
        SystemsSpec s = parse(R"({"systems":[{"type":"solar","area_m2":100,"efficiency":0.2,"facing":[0,1,0],"tracking":true}]})");
        Systems sys(s, LifeSupportMode::warn);
        SystemsContext c;
        c.sun_dir = {1.0, 0.0, 0.0};
        c.sun_flux = 1.0;
        sys.step(1.0, c);
        const double full = sys.supply_w();
        assert(full > 27000.0 && full < 27400.0);
        c.sun_flux = 0.25;
        sys.step(1.0, c);
        assert(std::abs(sys.supply_w() - full * 0.25) < 1.0);
        c.sun_flux = 0.0;
        sys.step(1.0, c);
        assert(sys.supply_w() == 0.0);
    }
    {
        // Load shedding: with no power source the battery empties and low-priority loads drop first.
        SystemsSpec s = parse(R"({"systems":[{"type":"battery","capacity_wh":10,"max_w":1000},
            {"type":"load","name":"essential","watts":100,"priority":0},{"type":"load","name":"lux","watts":100,"priority":2}]})");
        Systems sys(s, LifeSupportMode::warn);
        SystemsContext c;
        c.sun_flux = 0.0;
        double t = 0.0;
        for (int i = 0; i < 400; ++i)
        {
            t += 1.0;
            c.mission_time = t;
            sys.step(1.0, c);
        }
        assert(sys.battery_wh() < 1e-6);
        assert(!sys.powered());
        assert(sys.alarm_text().find("POWER") != std::string::npos);
    }
    {
        // Metabolism fills the cabin with CO2 until a powered scrubber catches up; the regulator keeps O2.
        Systems sys(spec, LifeSupportMode::warn);
        SystemsContext c;
        c.sun_flux = 1.0;
        const double p0 = sys.vessels()[0].pressure_kpa();
        double t = 0.0;
        for (int i = 0; i < 3600; ++i)
        {
            t += 10.0;
            c.mission_time = t;
            sys.step(10.0, c);
        }
        assert(std::fabs(sys.vessels()[0].pressure_kpa() - p0) < 1.0);
        assert(sys.vessels()[0].ppco2_kpa() < 1.0);
        assert(sys.vessels()[0].health >= 0.999);
    }
    {
        // A breach vents the cabin: pressure falls, the alarm sounds, and in fatal mode the crew is lost.
        Systems sys(spec, LifeSupportMode::fatal);
        assert(sys.breach(0, 5.0));
        SystemsContext c;
        c.sun_flux = 1.0;
        double t = 0.0;
        double last = sys.vessels()[0].pressure_kpa();
        bool alarmed = false;
        for (int i = 0; i < 20000 && !sys.crew_lost(); ++i)
        {
            t += 10.0;
            c.mission_time = t;
            sys.step(10.0, c);
            alarmed = alarmed || sys.alarm_text().find("LEAK") != std::string::npos;
        }
        (void)last;
        assert(alarmed);
        assert(sys.crew_lost() || sys.vessels()[0].health < 1.0);
    }
    {
        // With life support off nothing can hurt the crew, and gas totals stay finite under big time steps.
        Systems sys(spec, LifeSupportMode::off);
        sys.breach(0, 50.0);
        SystemsContext c;
        for (int i = 1; i <= 50; ++i)
        {
            c.mission_time = i * 1e5;
            sys.step(1e5, c);
        }
        assert(!sys.crew_lost() && sys.vessels()[0].health == 1.0);
        assert(std::isfinite(sys.vessels()[0].pressure_kpa()));
    }
    {
        // A dropped stage takes its equipment and compartments with it.
        Systems sys(spec, LifeSupportMode::warn);
        sys.drop_stage(0);
        assert(sys.tank_kg("nitrogen") == 0.0 && sys.tank_kg("oxygen") > 0.0);
        sys.drop_stage(1);
        assert(!sys.vessels()[0].alive);
    }

    {
        // A model with "vessel_", "crew_" and "vent_" parts defines the vessel; each OBJ usemtl group is a named part.
        const std::filesystem::path dir = std::filesystem::temp_directory_path() / "kosmos_systems_test";
        std::filesystem::create_directories(dir);
        {
            std::ofstream obj(dir / "lab.obj");
            int base = 0;
            const auto box = [&](const char *name, double x0, double y0, double z0, double x1, double y1, double z1) {
                obj << "o " << name << "\nusemtl " << name << "\n";
                const double v[8][3] = {{x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0}, {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}};
                for (const auto &p : v)
                    obj << "v " << p[0] << " " << p[1] << " " << p[2] << "\n";
                const int f[6][4] = {{1, 2, 3, 4}, {5, 8, 7, 6}, {1, 5, 6, 2}, {2, 6, 7, 3}, {3, 7, 8, 4}, {4, 8, 5, 1}};
                for (const auto &q : f)
                    obj << "f " << q[0] + base << " " << q[1] + base << " " << q[2] + base << " " << q[3] + base << "\n";
                base += 8;
            };
            box("hull", -1.5, -1.5, -4, 1.5, 1.5, 4);
            box("vessel_Lab", -1.2, -1.2, -3.5, 1.2, 1.2, 3.5);
            box("crew_a", -0.3, -0.3, -1.1, 0.3, 0.3, -0.9);
            box("crew_b", -0.3, -0.3, 0.9, 0.3, 0.3, 1.1);
            box("vent_a", 1.15, -0.1, -0.1, 1.25, 0.1, 0.1);
        }
        {
            std::ofstream json(dir / "ship.json");
            json << R"({"name":"TagTest","stages":[{"name":"Lab","dry_mass":8000,"z":0,"length":8,"radius":1.5,
                "parts":[{"model":"lab.obj","offset":[0,0,10]}]}],"vessels":[{"name":"Lab","pressure_kpa":90}]})";
        }
        ShipSpec ship;
        std::string error;
        assert(load_ship_spec((dir / "ship.json").string(), ship, error));
        assert(ship.systems.vessels.size() == 1);
        const VesselSpec &v = ship.systems.vessels[0];
        assert(v.from_mesh && v.crew == 2 && v.crew_at.size() == 2 && v.vents.size() == 1);
        assert(std::fabs(v.size.x - 2.4) < 1e-3 && std::fabs(v.size.z - 7.0) < 1e-3);
        assert(std::fabs(v.centre.z - 10.0) < 1e-3 && std::fabs(v.pressure_kpa - 90.0) < 1e-9);
        std::filesystem::remove_all(dir);
    }
    {
        // An enabled electrolyser turns water into oxygen and hydrogen (8:1 by mass) and is off by default.
        SystemsSpec s = parse(R"({"systems":[{"type":"battery","capacity_wh":5000,"max_w":2000},
            {"type":"tank","gas":"water","capacity_kg":10},{"type":"tank","gas":"oxygen","capacity_kg":10,"kg":0},
            {"type":"tank","gas":"hydrogen","capacity_kg":10,"kg":0},{"type":"electrolyser","watts":1000,"enabled":true}]})");
        Systems sys(s, LifeSupportMode::warn);
        SystemsContext c;
        for (int i = 1; i <= 360; ++i)
        {
            c.mission_time = i * 10.0;
            sys.step(10.0, c);
        }
        const double water_used = 10.0 - sys.tank_kg("water");
        assert(std::fabs(water_used - 0.18) < 0.01);
        assert(std::fabs(sys.tank_kg("oxygen") - water_used * 8.0 / 9.0) < 1e-6);
        assert(std::fabs(sys.tank_kg("hydrogen") - water_used / 9.0) < 1e-6);
        SystemsSpec off = parse(R"({"systems":[{"type":"tank","gas":"water","capacity_kg":10},{"type":"electrolyser","watts":1000}]})");
        Systems idle(off, LifeSupportMode::warn);
        idle.step(100.0, c);
        assert(idle.tank_kg("water") == 10.0);
    }
    {
        // An Apollo-style stack: the service module's fuel cells and cryogenic tanks carry a 34.5 kPa pure-oxygen cabin for days;
        // when the service module goes, the command module's own batteries and oxygen keep the crew healthy.
        SystemsSpec s = parse(R"({"systems":[
            {"type":"fuel_cell","watts":4200,"stage":"Lower"},
            {"type":"tank","gas":"oxygen","capacity_kg":295,"stage":"Lower"},
            {"type":"tank","gas":"hydrogen","capacity_kg":25.4,"stage":"Lower"},
            {"type":"battery","capacity_wh":3360,"max_w":2000},
            {"type":"tank","gas":"oxygen","capacity_kg":2},
            {"type":"load","name":"avionics","watts":500,"priority":0},
            {"type":"regulator","vessel":"Cabin"},
            {"type":"scrubber","vessel":"Cabin","kg_per_day":6,"sorbent_kg":50}],
          "vessels":[{"name":"Cabin","size":[2,1.9,1.62],"crew":3,"pressure_kpa":34.5,"o2_fraction":1.0}]})");
        Systems sys(s, LifeSupportMode::fatal);
        SystemsContext c;
        double t = 0.0;
        for (int i = 0; i < 4 * 8640; ++i)
        {
            t += 10.0;
            c.mission_time = t;
            sys.step(10.0, c);
        }
        assert(sys.powered() && !sys.crew_lost());
        assert(sys.vessels()[0].health >= 0.999 && sys.vessels()[0].hazard.empty());
        assert(std::fabs(sys.vessels()[0].pressure_kpa() - 34.5) < 1.0 && sys.vessels()[0].ppo2_kpa() > 33.0);
        assert(sys.tank_kg("hydrogen") < 25.4 && sys.tank_kg("oxygen") < 295.0);

        sys.drop_stage(0);
        assert(sys.tank_kg("hydrogen") == 0.0);
        for (int i = 0; i < 360; ++i)
        {
            t += 10.0;
            c.mission_time = t;
            sys.step(10.0, c);
        }
        assert(sys.powered() && !sys.crew_lost() && sys.vessels()[0].health >= 0.999);
        assert(sys.battery_wh() > 0.0 && sys.tank_kg("oxygen") > 0.0);
    }
    {
        // The shipped capsules keep their crew healthy for days on their own supplies; the Apollo stack does it from the service
        // module until it is jettisoned and then from the command module alone.
        struct Case
        {
            const char *file;
            int drop_stage; // stage to jettison after the first stretch, or -1
            double pressure_kpa;
            double days;
        };
        const Case cases[] = {{"/addons/cvel-saturn-v/ship.json", 3, 34.5, 3.0},
                              {"/addons/cvel-saturn-v/command_module.json", -1, 34.5, 0.1}, // entry batteries only
                              {"/addons/cvel-soyuz/tma.json", 0, 101.3, 3.0},
                              {"/addons/cvel-soyuz/descent.json", -1, 101.3, 0.1},
                              {"/addons/cvel-soyuz/fg.json", -1, 101.3, 3.0}};
        for (const Case &k : cases)
        {
            ShipSpec ship;
            std::string error;
            assert(load_ship_spec(std::string(KOSMOS_ROOT) + k.file, ship, error));
            assert(ship.systems.vessels.size() == 1 && ship.systems.vessels[0].crew == 3);
            Systems sys(ship.systems, LifeSupportMode::fatal);
            SystemsContext c;
            c.sun_flux = 1.0;
            c.sun_dir = {0.0, 1.0, 0.0};
            double t = 0.0;
            for (int i = 0; i < static_cast<int>(k.days * 8640); ++i)
            {
                t += 10.0;
                c.mission_time = t;
                sys.step(10.0, c);
            }
            assert(sys.powered() && !sys.crew_lost());
            assert(sys.vessels()[0].health >= 0.999);
            assert(std::fabs(sys.vessels()[0].pressure_kpa() - k.pressure_kpa) < 2.0);
            if (k.drop_stage >= 0)
            {
                sys.drop_stage(k.drop_stage);
                for (int i = 0; i < 360; ++i)
                {
                    t += 10.0;
                    c.mission_time = t;
                    sys.step(10.0, c);
                }
                assert(sys.powered() && !sys.crew_lost() && sys.vessels()[0].health >= 0.999);
                assert(sys.vessels()[0].alive && sys.tank_kg("oxygen") > 0.0);
            }
        }
    }
    std::puts("systems_test ok");
    return 0;
}
