#undef NDEBUG
#include "systems.h"

#include "nav.h"
#include "ship.h"
#include "transfer_plan.h"
#include "terrain.h"

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

    void check_ship_models(const char *file, std::size_t expected_models)
    {
        ShipSpec spec;
        std::string error;
        assert(load_ship_spec(std::string(KOSMOS_ROOT) + file, spec, error));
        std::size_t models = 0;
        for (const StageSpec &stage : spec.stages)
            for (const PartSpec &part : stage.parts)
            {
                if (part.model.empty())
                    continue;
                ++models;
                const std::filesystem::path model = std::filesystem::path(spec.directory) / part.model;
                std::ifstream obj(model);
                assert(obj);
                bool faces = false, material_library = false;
                std::string line;
                while (std::getline(obj, line))
                {
                    if (line.rfind("f ", 0) == 0)
                        faces = true;
                    if (line.rfind("mtllib ", 0) != 0)
                        continue;
                    std::ifstream mtl(model.parent_path() / line.substr(7));
                    assert(mtl);
                    material_library = true;
                    std::string material_line;
                    while (std::getline(mtl, material_line))
                        if (material_line.rfind("map_Kd ", 0) == 0)
                            assert(std::filesystem::is_regular_file(model.parent_path() / material_line.substr(7)));
                }
                assert(faces && material_library);
            }
        assert(models == expected_models);
    }
} // namespace

int main()
{
    check_ship_models("/addons/cvel-atlas/ship.json", 5);
    check_ship_models("/addons/cvel-centaur/ship.json", 1);
    check_ship_models("/addons/cvel-transtage/ship.json", 2);
    check_ship_models("/addons/cvel-energia-m/ship.json", 5);
    check_ship_models("/addons/cvel-buran/energia.json", 5);
    {
        Universe universe;
        std::string error;
        assert(universe.load(std::string(KOSMOS_ROOT) + "/data/sol.json", error));
        ShipSpec energia, centaur, fregat, probe;
        assert(load_ship_spec(std::string(KOSMOS_ROOT) + "/addons/cvel-energia-m/ship.json", energia, error));
        assert(load_ship_spec(std::string(KOSMOS_ROOT) + "/addons/cvel-centaur/ship.json", centaur, error));
        assert(load_ship_spec(std::string(KOSMOS_ROOT) + "/addons/cvel-fregat/ship.json", fregat, error));
        assert(load_ship_spec(std::string(KOSMOS_ROOT) + "/addons/probe/ship.json", probe, error));
        std::ifstream input(std::string(KOSMOS_ROOT) + "/scenarios/CVEL/CVEL-Energia/energia_m_baikonur.json");
        assert(input);
        const auto scenario = nlohmann::json::parse(input);
        const auto &ships = scenario.at("ships");
        assert(ships.size() == 4);
        const ShipSpec *stack[] = {&energia, &centaur, &fregat, &probe};
        for (std::size_t i = 0; i < ships.size(); ++i)
        {
            assert(ships[i].at("ship") == stack[i]->name);
            if (i == 0)
                continue;
            const auto &dock = ships[i].at("dock");
            assert(dock.at("to") == i - 1);
            const int parent_port = dock.at("port");
            const int own_port = dock.at("own_port");
            assert(parent_port >= 0 && static_cast<std::size_t>(parent_port) < stack[i - 1]->ports.size());
            assert(own_port >= 0 && static_cast<std::size_t>(own_port) < stack[i]->ports.size());
            assert(stack[i - 1]->ports[static_cast<std::size_t>(parent_port)].direction.z == -1.0);
            assert(stack[i]->ports[static_cast<std::size_t>(own_port)].direction.z == 1.0);
        }
        assert(energia.ports[0].position.z == -9.7 && energia.ports[0].stage == 3);
        std::vector<std::unique_ptr<Ship>> fleet;
        for (const ShipSpec *spec : stack)
            fleet.push_back(std::make_unique<Ship>(universe, *spec));
        sl::PropertyTree props;
        NavSystem nav;
        nav.init(props);
        for (std::size_t i = 1; i < ships.size(); ++i)
        {
            const auto &dock = ships[i].at("dock");
            nav.link(dock.at("to"), dock.at("port"), static_cast<int>(i), dock.at("own_port"));
        }
        nav.couple(universe, fleet, 0, 0.0);
        assert(nav.dock_links().size() == 3);
        const sl::dvec3 origin = fleet[0]->origin();
        assert(std::abs((fleet[1]->origin() - origin).z + 19.2) < 1e-6);
        assert(std::abs((fleet[2]->origin() - origin).z + 20.0) < 1e-6);
        assert(std::abs((fleet[3]->origin() - origin).z + 21.67) < 1e-6);
        const double payload_mass = fleet[1]->body().mass() + fleet[2]->body().mass() + fleet[3]->body().mass();
        const double full_mass = fleet[0]->body().mass();
        assert(fleet[0]->stage_once() && fleet[0]->active_stage() == 3);
        assert(fleet[0]->debris().size() == 4 && fleet[0]->port_attached(0));
        assert(energia.stages[3].name == "Core stage" && !energia.stages[3].engines.empty());
        assert(!fleet[0]->stage_once());
        assert(nav.undock(fleet, 0, 0) && nav.dock_links().size() == 2);
        nav.couple(universe, fleet, 1, 0.0);
        assert(fleet[0]->body().mass() < full_mass - payload_mass);
        assert(nav.undock(fleet, 1, 1) && nav.dock_links().size() == 1);
        nav.couple(universe, fleet, 2, 0.0);
        assert(nav.undock(fleet, 2, 1) && nav.dock_links().empty());
    }
    {
        Universe universe;
        std::string error;
        assert(universe.load(std::string(KOSMOS_ROOT) + "/data/sol.json", error));
        ShipSpec spec;
        assert(load_ship_spec(std::string(KOSMOS_ROOT) + "/addons/cvel-ares/ship.json", spec, error));
        Ship ship(universe, spec);
        assert(spec.animation_controls.size() == 1 && spec.animation_controls[0].property == "legs");
        Controls legs;
        legs.sas = "off";
        assert(ship.animation_position(0) == 0.0);
        legs.anim = {1.0};
        ship.apply_controls(legs, 4.0);
        assert(std::abs(ship.animation_position(0) - 0.5) < 1e-6);
        ship.apply_controls(legs, 4.0);
        assert(std::abs(ship.animation_position(0) - 1.0) < 1e-6);
        legs.anim = {0.0};
        ship.apply_controls(legs, 8.0);
        assert(ship.animation_position(0) == 0.0);
        const int mars = universe.find("Mars");
        assert(mars >= 0);
        ship.body().position = universe.position(mars) + sl::dvec3(0.0, 0.0, universe.bodies[mars].radius + 200000.0);
        ship.body().orientation = sl::dquat(1.0, 0.0, 0.0, 0.0);
        ship.after_step(0.01);
        const double sphere_altitude = universe.world.altitude_of(ship.body().position);
        assert(sphere_altitude > 199999.0 && sphere_altitude < 200001.0);
        assert(ship.telemetry().altitude < sphere_altitude && ship.telemetry().altitude > sphere_altitude - 100.0);
        const double bare_clearance = ship.telemetry().altitude;
        {
            sl::TerrainConfig config;
            config.shape = sl::TerrainShape::sphere;
            config.radius = universe.bodies[mars].radius;
            auto source = std::make_shared<sl::FunctionSource>([](double, double) { return 1000.0; }, 1000.0, 1000.0);
            sl::Terrain terrain(config, source);
            universe.world.body(mars)->terrain = &terrain;
            ship.after_step(0.01);
            assert(std::abs(ship.telemetry().altitude - (bare_clearance - 1000.0)) < 0.01);
            universe.world.body(mars)->terrain = nullptr;
        }
        sl::dvec3 up, east, north;
        ship.local_frame(up, east, north);
        const double heading_rad = glm::radians(350.0);
        ship.body().orientation = glm::quatLookAt(north * std::cos(heading_rad) + east * std::sin(heading_rad), up);
        ship.after_step(0.01);
        assert(std::abs(ship.telemetry().heading - 350.0) < 0.1);
        ship.body().orientation = glm::quatLookAt(up, north);
        ship.after_step(0.01);
        assert(std::abs(ship.telemetry().heading - 350.0) < 0.1);
        const double north_rad = glm::radians(359.8);
        ship.body().orientation = glm::quatLookAt(north * std::cos(north_rad) + east * std::sin(north_rad), up);
        ship.after_step(0.01);
        assert(ship.telemetry().heading == 0.0);
        ship.body().orientation = sl::dquat(1.0, 0.0, 0.0, 0.0);
        Controls controls;
        controls.sas = "radial-out";
        ship.apply_controls(controls, 0.1);
        universe.world.step(0.1);
        assert(glm::length(ship.body().angular_velocity) > 0.001);
    }
    {
        Universe universe;
        std::string error;
        assert(universe.load(std::string(KOSMOS_ROOT) + "/data/sol.json", error));
        ShipSpec spec;
        assert(load_ship_spec(std::string(KOSMOS_ROOT) + "/addons/cvel-atlas/ship.json", spec, error));
        Ship rocket(universe, spec);
        const int earth = universe.find("Earth");
        assert(earth >= 0);
        rocket.body().position = universe.position(earth) + sl::dvec3(0.0, universe.bodies[earth].radius + 200000.0, 0.0);
        assert(rocket.stage_once());
        assert(rocket.active_stage() == 3 && rocket.debris().size() == 3);
        assert(!rocket.stage_once() && rocket.active_stage() == 3);
        const double fuel = rocket.body().tanks[3].mass;
        Controls controls;
        controls.throttle = 1.0;
        rocket.apply_controls(controls, 1.0);
        rocket.after_step(0.01);
        assert(rocket.telemetry().thrust > 99000.0 && fuel > 0.0);
        universe.world.step(1.0);
        assert(rocket.body().tanks[3].mass < fuel - 20.0);
    }
    {
        Universe universe;
        std::string error;
        assert(universe.load(std::string(KOSMOS_ROOT) + "/data/sol.json", error));
        ShipSpec spec;
        assert(load_ship_spec(std::string(KOSMOS_ROOT) + "/addons/cvel-titan/titan3e.json", spec, error));
        Ship rocket(universe, spec);
        assert(rocket.stage_once() && rocket.active_stage() == 1);
        assert(rocket.stage_once() && rocket.active_stage() == 2);
        assert(rocket.stage_once() && rocket.active_stage() == 5);
        assert(rocket.debris().size() == 6);
    }
    {
        Universe universe;
        std::string error;
        assert(universe.load(std::string(KOSMOS_ROOT) + "/data/sol.json", error));
        for (const char *file : {
                 "/addons/cvel-buran/energia.json",
                 "/addons/cvel-delta-iv/ship.json",
                 "/addons/cvel-energia-m/ship.json",
                 "/addons/cvel-saturn-v23/ship.json",
                 "/addons/cvel-saturn-v23/payload.json",
                 "/addons/cvel-soyuz/fg.json",
                 "/addons/cvel-titan/titan3c.json",
                 "/addons/cvel-titan/titan3d.json",
                 "/addons/cvel-titan/titan3e.json",
                 "/addons/cvel-titan/titan34d.json",
                 "/addons/cvel-titan/titan4a.json",
                 "/addons/cvel-titan/titan4b.json"})
        {
            ShipSpec spec;
            assert(load_ship_spec(std::string(KOSMOS_ROOT) + file, spec, error));
            const StageSpec &boosters = spec.stages[0];
            assert(boosters.radial_parts && boosters.parts.size() >= 2 && boosters.separation_speed > 0.0);
            Ship rocket(universe, spec);
            rocket.body().orientation = glm::angleAxis(0.7, glm::normalize(sl::dvec3(1.0, 2.0, 3.0)));
            rocket.body().velocity = {100.0, -20.0, 3.0};
            const sl::dvec3 velocity = rocket.body().velocity;
            const sl::dmat3 rotation = rocket.body().rotation();
            const double mass = rocket.body().mass();
            assert(rocket.stage_once());
            assert(rocket.debris().size() >= boosters.parts.size());
            sl::dvec3 momentum = rocket.body().velocity * rocket.body().mass();
            for (std::size_t i = 0; i < rocket.debris().size(); ++i)
            {
                const Debris &debris = rocket.debris()[i];
                const sl::FlightBody *body = universe.world.body(debris.body);
                assert(body);
                momentum += body->velocity * body->mass();
                if (i >= boosters.parts.size())
                    continue;
                const sl::dvec3 offset = boosters.parts[i].offset;
                const sl::dvec3 radial = glm::normalize(sl::dvec3(offset.x, offset.y, 0.0));
                assert(debris.stage == 0 && debris.part == static_cast<int>(i));
                assert(glm::length(body->velocity - velocity - rotation * radial * boosters.separation_speed) < 1e-8);
            }
            const double momentum_error = glm::length(momentum - velocity * mass);
            if (momentum_error >= 1e-5)
                std::fprintf(stderr, "%s: separation momentum error %.9f\n", file, momentum_error);
            assert(momentum_error < 1e-5);
        }
    }
    {
        Universe universe;
        std::string error;
        assert(universe.load(std::string(KOSMOS_ROOT) + "/data/sol.json", error));
        for (const char *file : {"titan3c.json", "titan34d.json"})
        {
            ShipSpec spec;
            assert(load_ship_spec(std::string(KOSMOS_ROOT) + "/addons/cvel-titan/" + file, spec, error));
            const StageSpec &stage = spec.stages.back();
            assert(stage.name == "Transtage" && stage.engines.size() == 2);
            assert(stage.engines[0].position.y == 0.72 && stage.engines[1].position.y == -0.72);
            assert(stage.engines[0].thrust + stage.engines[1].thrust == 73428.0);
            assert(stage.engines[0].isp == 316.0 && stage.engines[1].isp == 316.0);
            Ship rocket(universe, spec);
            while (rocket.active_stage() < rocket.stage_count() - 1)
                assert(rocket.stage_once());
            const int earth = universe.find("Earth");
            assert(earth >= 0);
            rocket.body().position = universe.position(earth) + sl::dvec3(0.0, universe.bodies[earth].radius + 200000.0, 0.0);
            const double fuel = rocket.body().tanks.back().mass;
            Controls controls;
            controls.throttle = 1.0;
            rocket.apply_controls(controls, 1.0);
            rocket.after_step(0.01);
            assert(std::fabs(rocket.telemetry().thrust - 73428.0) < 1.0);
            universe.world.step(1.0);
            const double consumed = fuel - rocket.body().tanks.back().mass;
            assert(consumed > 23.0 && consumed < 24.0);
        }
    }
    {
        Universe universe;
        std::string error;
        assert(universe.load(std::string(KOSMOS_ROOT) + "/data/sol.json", error));
        const int earth = universe.find("Earth"), sun = universe.find("Sun");
        assert(earth >= 0 && sun >= 0);
        const sl::dvec3 earth_position = universe.position(earth);
        const sl::dvec3 toward_sun = glm::normalize(universe.position(sun) - earth_position);
        for (const char *file : {"tma.json", "fg.json"})
        {
            ShipSpec spec;
            assert(load_ship_spec(std::string(KOSMOS_ROOT) + "/addons/cvel-soyuz/" + file, spec, error));
            Ship ship(universe, spec);
            assert(ship.systems());
            const auto solar_output = [&ship] {
                for (const SystemComponent &component : ship.systems()->components())
                    if (component.type == "solar")
                        return component.output_w;
                assert(false);
                return 0.0;
            };
            ship.body().position = earth_position + toward_sun * 10000000.0;
            ship.body().orientation = sl::dquat(sl::dvec3(0.0, 1.0, 0.0), toward_sun);
            const double battery = ship.systems()->battery_wh();
            ship.after_step(1.0);
            assert(solar_output() == 0.0);
            assert(ship.systems()->powered() && ship.systems()->battery_wh() < battery);

            Controls controls;
            controls.anim = {1.0};
            ship.apply_controls(controls, 5.0);
            assert(std::abs(ship.animation_position(0) - 0.5) < 1e-9);
            ship.after_step(5.0);
            assert(solar_output() == 0.0);
            ship.apply_controls(controls, 5.0);
            ship.after_step(5.0);
            const double full = solar_output();
            assert(full > 3000.0 && full < 4500.0);
            const double before_deployed = ship.systems()->battery_wh();
            ship.after_step(2.0);
            assert(ship.systems()->battery_wh() > before_deployed);
            const ShipCarryState carried = ship.carry_state();
            Ship restored(universe, spec);
            restored.restore_carry_state(carried);
            assert(std::abs(restored.animation_position(0) - 1.0) < 1e-9);
            universe.stellar_luminosity = 0.5;
            ship.after_step(1.0);
            assert(std::abs(solar_output() - full * 0.5) < 1e-6);
            universe.stellar_luminosity = 1.0;

            ship.body().orientation = sl::dquat(sl::dvec3(0.0, 1.0, 0.0), -toward_sun);
            ship.after_step(1.0);
            assert(solar_output() < 1e-6);
            const sl::dvec3 edge_on = glm::normalize(glm::cross(toward_sun, sl::dvec3(0.0, 1.0, 0.0)));
            ship.body().orientation = sl::dquat(sl::dvec3(0.0, 1.0, 0.0), edge_on);
            ship.after_step(1.0);
            assert(solar_output() < 1e-6);
            ship.body().orientation = sl::dquat(sl::dvec3(0.0, 1.0, 0.0), toward_sun);
            ship.body().position = earth_position - toward_sun * 10000000.0;
            const double before_eclipse = ship.systems()->battery_wh();
            ship.after_step(1.0);
            assert(solar_output() == 0.0 && ship.systems()->battery_wh() < before_eclipse);
            ship.body().position = earth_position + toward_sun * 10000000.0;
            const int moon = universe.find("Moon");
            assert(moon >= 0);
            const sl::dvec3 moon_position = universe.world.body(moon)->position;
            universe.world.body(moon)->position = ship.body().position + toward_sun * 10000000.0;
            ship.after_step(1.0);
            assert(solar_output() == 0.0);
            universe.world.body(moon)->position = moon_position;
            controls.anim = {0.0};
            ship.apply_controls(controls, 2.0);
            ship.after_step(2.0);
            assert(solar_output() == 0.0);
            if (std::string(file) == "tma.json")
            {
                controls.anim = {1.0};
                Ship warped(universe, spec), stepped(universe, spec);
                for (Ship *candidate : {&warped, &stepped})
                {
                    candidate->body().position = earth_position + toward_sun * 10000000.0;
                    candidate->body().orientation = sl::dquat(sl::dvec3(0.0, 1.0, 0.0), toward_sun);
                    candidate->systems()->set_battery_fraction(0.5);
                }
                warped.apply_controls(controls, 100.0);
                warped.after_step(100.0);
                stepped.apply_controls(controls, 9.0);
                stepped.after_step(9.0);
                stepped.apply_controls(controls, 91.0);
                stepped.after_step(91.0);
                assert(std::abs(warped.systems()->battery_wh() - stepped.systems()->battery_wh()) < 1e-6);
                assert(warped.stage_once());
                warped.after_step(1.0);
                assert(warped.systems()->components()[0].output_w == 0.0);
            }
        }
    }
    {
        Universe universe;
        std::string error;
        assert(universe.load(std::string(KOSMOS_ROOT) + "/data/sol.json", error));
        ShipSpec spec;
        assert(load_ship_spec(std::string(KOSMOS_ROOT) + "/addons/cvel-saturn-v23/ship.json", spec, error));
        assert(spec.stages[0].radial_parts && spec.stages[0].parts.size() == 4);
        Ship rocket(universe, spec);
        const sl::dvec3 velocity = rocket.body().velocity;
        const sl::dmat3 rotation = rocket.body().rotation();
        assert(rocket.stage_once());
        assert(rocket.active_stage() == 1 && rocket.debris().size() == 4);
        for (std::size_t i = 0; i < rocket.debris().size(); ++i)
        {
            const Debris &debris = rocket.debris()[i];
            const sl::FlightBody *body = universe.world.body(debris.body);
            const sl::dvec3 radial = glm::normalize(sl::dvec3(spec.stages[0].parts[i].offset.x, spec.stages[0].parts[i].offset.y, 0.0));
            assert(body && debris.part == static_cast<int>(i));
            assert(glm::length(body->velocity - velocity - rotation * radial * spec.stages[0].separation_speed) < 1e-8);
        }
    }
    {
        Universe universe;
        std::string error;
        assert(universe.load(std::string(KOSMOS_ROOT) + "/data/sol.json", error));
        ShipSpec spec;
        assert(load_ship_spec(std::string(KOSMOS_ROOT) + "/addons/cvel-buran/energia.json", spec, error));
        assert(spec.stages[0].radial_parts && spec.stages[0].parts.size() == 4);
        Ship rocket(universe, spec);
        rocket.body().orientation = glm::angleAxis(0.7, sl::dvec3(0.0, 0.0, 1.0));
        rocket.body().velocity = {100.0, -20.0, 3.0};
        const sl::dvec3 velocity = rocket.body().velocity;
        const sl::dvec3 origin = rocket.origin();
        const sl::dmat3 rotation = rocket.body().rotation();
        const double mass = rocket.body().mass();
        const double dropped_mass = spec.stages[0].dry_mass + rocket.body().tanks[0].mass;
        assert(rocket.stage_once());
        assert(rocket.active_stage() == 1 && rocket.debris().size() == 4);
        sl::dvec3 momentum = rocket.body().velocity * rocket.body().mass();
        for (int i = 0; i < 4; ++i)
        {
            const Debris &debris = rocket.debris()[static_cast<std::size_t>(i)];
            const sl::FlightBody *body = universe.world.body(debris.body);
            assert(body && debris.part == i && debris.stage == 0);
            assert(std::abs(body->mass() - dropped_mass / 4.0) < 1e-8);
            const sl::dvec3 radial = glm::normalize(sl::dvec3(spec.stages[0].parts[i].offset.x, spec.stages[0].parts[i].offset.y, 0.0));
            assert(glm::length(body->velocity - velocity - rotation * radial * spec.stages[0].separation_speed) < 1e-8);
            assert(glm::length(body->position - origin - rotation * debris.centre) < 1e-8);
            momentum += body->velocity * body->mass();
        }
        assert(glm::length(momentum - velocity * mass) < 1e-6);
    }
    {
        Universe universe;
        std::string error;
        assert(universe.load(std::string(KOSMOS_ROOT) + "/data/sol.json", error));
        ShipSpec buran_spec;
        assert(load_ship_spec(std::string(KOSMOS_ROOT) + "/addons/cvel-buran/ship.json", buran_spec, error));
        ShipSpec baseline_spec = buran_spec;
        baseline_spec.surfaces.erase(
            std::remove_if(baseline_spec.surfaces.begin(), baseline_spec.surfaces.end(),
                           [](const SurfaceSpec &surface) { return surface.control == "elevator" && surface.position.x != 0.0; }),
            baseline_spec.surfaces.end());
        Ship buran(universe, buran_spec), baseline(universe, baseline_spec);
        const int earth = universe.find("Earth");
        assert(earth >= 0);
        buran.body().position = universe.position(earth) + sl::dvec3(0.0, universe.bodies[earth].radius + 10000.0, 0.0);
        baseline.body().position = buran.body().position + sl::dvec3(1000.0, 0.0, 0.0);
        const sl::dvec3 flight_velocity = universe.world.body(earth)->velocity + sl::dvec3(0.0, 0.0, -250.0);
        buran.body().velocity = baseline.body().velocity = flight_velocity;
        buran.after_step(0.01);
        baseline.after_step(0.01);
        Controls pitch_up;
        pitch_up.pitch = 1.0;
        pitch_up.sas = "off";
        buran.apply_controls(pitch_up, 0.1);
        baseline.apply_controls(pitch_up, 0.1);
        universe.world.step(0.1);
        assert(buran.body().angular_velocity.x > baseline.body().angular_velocity.x * 1.5);
    }
    {
        Universe universe;
        std::string error;
        assert(universe.load(std::string(KOSMOS_ROOT) + "/data/sol.json", error));
        ShipSpec spec;
        spec.name = "RadialPairTest";
        StageSpec boosters;
        boosters.name = "Boosters";
        boosters.dry_mass = 100.0;
        boosters.propellant = 200.0;
        boosters.radial_pair = true;
        boosters.separation_speed = 2.0;
        PartSpec left, right;
        left.offset = {-3.0, 0.0, 0.0};
        right.offset = {3.0, 0.0, 0.0};
        boosters.parts = {left, right};
        StageSpec core;
        core.name = "Core";
        core.dry_mass = 80.0;
        core.propellant = 100.0;
        StageSpec upper;
        upper.name = "Upper";
        upper.dry_mass = 20.0;
        spec.stages = {boosters, core, upper};

        Ship rocket(universe, spec);
        rocket.body().orientation = glm::angleAxis(0.7, sl::dvec3(0.0, 0.0, 1.0));
        rocket.body().velocity = {100.0, -20.0, 3.0};
        const sl::dvec3 velocity = rocket.body().velocity;
        const sl::dvec3 origin = rocket.origin();
        const sl::dmat3 rotation = rocket.body().rotation();
        const double mass = rocket.body().mass();
        const double dropped_mass = spec.stages[0].dry_mass + rocket.body().tanks[0].mass;
        assert(rocket.stage_once());
        assert(rocket.active_stage() == 1 && rocket.debris().size() == 2);
        sl::dvec3 momentum = rocket.body().velocity * rocket.body().mass();
        for (int i = 0; i < 2; ++i)
        {
            const Debris &debris = rocket.debris()[static_cast<std::size_t>(i)];
            const sl::FlightBody *body = universe.world.body(debris.body);
            assert(body && debris.part == i && debris.stage == 0);
            assert(std::abs(body->mass() - dropped_mass / 2.0) < 1e-8);
            const sl::dvec3 radial = glm::normalize(sl::dvec3(spec.stages[0].parts[i].offset.x, spec.stages[0].parts[i].offset.y, 0.0));
            assert(glm::length(body->velocity - velocity - rotation * radial * spec.stages[0].separation_speed) < 1e-8);
            assert(glm::length(body->position - origin - rotation * debris.centre) < 1e-8);
            momentum += body->velocity * body->mass();
        }
        assert(glm::length(momentum - velocity * mass) < 1e-6);
        assert(glm::length(rocket.body().velocity - velocity) < 1e-8);
        assert(rocket.stage_once());
        assert(rocket.debris().size() == 3 && rocket.debris().back().part == -1);
    }
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
        SystemComponent battery;
        battery.type = "battery";
        battery.capacity = 1000.0;
        battery.amount = 1000.0;
        battery.stage = 1;
        spec.systems.components.push_back(battery);

        Ship source(universe, spec);
        assert(source.stage_once());
        source.body().tanks[1].mass = 20.0;
        source.set_gear_position(0.25);
        assert(source.systems());
        source.systems()->set_battery_fraction(0.3);
        const ShipCarryState state = source.carry_state();

        Ship arrival(universe, spec);
        assert(arrival.systems());
        arrival.restore_carry_state(state);
        assert(std::abs(arrival.systems()->components()[0].amount - 300.0) < 1e-9);
        assert(arrival.active_stage() == 1);
        assert(arrival.body().tanks[0].mass == 0.0);
        assert(arrival.body().tanks[1].mass == 20.0);
        assert(arrival.carry_state().rcs_propellant == 12.0);
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
            c.animation_positions.assign(ship.animation_controls.size(), 1.0);
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
    {
        // Earth to Mars about the Sun: the textbook Hohmann numbers, and windows one synodic period apart.
        const double mu = 1.32712440018e20, r1 = 1.496e11, r2 = 2.279e11;
        const HohmannPlan plan = plan_hohmann(mu, r1, r2, 0.5);
        assert(plan.has_window);
        assert(std::fabs(plan.dv1 - 2945.0) < 30.0 && std::fabs(plan.dv2 - 2650.0) < 30.0);
        assert(std::fabs(plan.tof / 86400.0 - 259.0) < 2.0);
        assert(std::fabs(plan.synodic / 86400.0 - 780.0) < 5.0);
        assert(plan.wait >= 0.0 && plan.wait < plan.synodic);
        const double ns = std::sqrt(mu / (r1 * r1 * r1)), nt = std::sqrt(mu / (r2 * r2 * r2));
        const double gap = std::remainder(plan.phase + (nt - ns) * plan.wait - plan.lead, 2.0 * 3.14159265358979323846);
        assert(std::fabs(gap) < 1e-6);
        assert(std::fabs(window_wait(plan, 2) - plan.wait - 2.0 * plan.synodic) < 1e-3);
        assert(plan_hohmann(mu, r1, r1, 0.0).has_window == false);
        assert(plan_hohmann(0.0, r1, r2, 0.0).has_window == false);
        assert(plan_hohmann(mu, r2, r1, 0.0).dv1 < 0.0);
    }
    {
        Universe universe;
        std::string error;
        assert(universe.load(std::string(KOSMOS_ROOT) + "/data/sol.json", error));
        ShipSpec probe;
        assert(load_ship_spec(std::string(KOSMOS_ROOT) + "/addons/probe/ship.json", probe, error));
        assert(probe.cockpit.present && probe.cockpit.eye.z > 0.0 && probe.cockpit.yaw == 180.0);
        assert(!probe.rcs_effects && probe.rcs_thrust == 20.0);
        Ship ship(universe, probe);
        Controls controls;
        controls.rcs_x = 1.0;
        ship.apply_controls(controls, 0.1);
        assert(glm::length(ship.rcs_push()) > 0.0);
    }
    {
        Universe universe;
        std::string error;
        assert(universe.load(std::string(KOSMOS_ROOT) + "/data/sol.json", error));
        ShipSpec voyager;
        assert(load_ship_spec(std::string(KOSMOS_ROOT) + "/addons/nasa-voyager/ship.json", voyager, error));
        assert(voyager.instruments.size() == 4);
        assert(voyager.instruments[0].type == "visual" && voyager.instruments[2].type == "ir" && voyager.instruments[3].type == "spectrometer");
        assert(voyager.instruments[0].fov > 0.0 && voyager.instruments[0].resolution >= 64 && !voyager.instruments[0].load.empty());
        Ship ship(universe, voyager);
        assert(ship.instruments().size() == 4 && ship.instruments()[0].frames == voyager.instruments[0].frames);
        ship.instrument_shoot(0);
        assert(ship.instruments()[0].pending == 1);
        assert(std::fabs(glm::length(ship.instrument_direction(0)) - 1.0) < 1e-9);
    }
    std::puts("systems_test ok");
    return 0;
}
