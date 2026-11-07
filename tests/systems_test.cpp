#undef NDEBUG
#include "systems.h"

#include "ship.h"
#include "ship_visuals.h"
#include "transfer_plan.h"
#include "terrain.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include <algorithm>
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
        PartSpec part;
        sl::ModelPart mesh;
        mesh.name = "outer";
        mesh.material_name = "petal";
        assert(app::part_cull_mode(part, mesh) == sl::CullMode::none);
        part.single_sided = {"outer"};
        assert(app::part_cull_mode(part, mesh) == sl::CullMode::back);
        part.single_sided = {"petal"};
        assert(app::part_cull_mode(part, mesh) == sl::CullMode::back);
        part.single_sided = {"other"};
        assert(app::part_cull_mode(part, mesh) == sl::CullMode::none);
    }
    {
        const glm::vec2 uv(0.25f, 0.1f);
        assert(app::vc_screen_hit_uv(uv, false) == uv);
        assert(app::vc_screen_hit_uv(uv, true) == glm::vec2(0.25f, 0.9f));
    }
    {
        for (const char *file : {"/addons/cvel-shuttle-a/ship.json", "/addons/cvel-shuttle-a-nuclear/ship.json", "/addons/cvel-shuttle-aq/ship.json"})
            for (const char *body_name : {"Moon", "Earth"})
                for (int start : {0, 1, 2})
                {
                    Universe universe;
                    std::string error;
                    assert(universe.load(std::string(KOSMOS_ROOT) + "/data/sol.json", error));
                    ShipSpec spec;
                    assert(load_ship_spec(std::string(KOSMOS_ROOT) + file, spec, error));
                    assert(spec.cockpit.vc_model.find("shuttlea_vc.obj") != std::string::npos);
                    assert(!spec.cockpit.vc_hotspots.empty() && !spec.cockpit.vc_animations.empty());
                    assert(spec.cockpit.vc_gauges.size() == 2);
                    const auto screens = app::Visuals::parse_vc_screens((std::filesystem::path(spec.directory) / spec.cockpit.vc_model).string());
                    assert(screens[0].size() == 2 && screens[1].size() == 2);
                    assert(spec.cockpit.panel_image.empty());
                    assert(!spec.cockpit.instruments.empty() && !spec.cockpit.controls.empty());
                    assert(spec.stages.size() == 1 && spec.stages[0].parts.size() == 1);
                    assert(!spec.stages[0].parts[0].model.empty());
                    Ship ship(universe, spec);
                    const int planet = universe.find(body_name);
                    assert(planet >= 0 && ship.body().gear.size() == 6);
                    const sl::dvec3 up(1.0, 0.0, 0.0);
                    if (start == 2)
                    {
                        Ship reference(universe, spec);
                        reference.place_on_surface(planet, up, 90.0, 0.0);
                        ship.place_in_orbit(planet, 200000.0, 0.0, 0.0);
                        ship.after_step(0.0);
                        ship.set_gear_position(1.0);
                        ship.place_relative(planet,
                                            reference.body().position - universe.position(planet) + up * 3.0,
                                            reference.body().velocity - universe.world.body(planet)->velocity,
                                            reference.body().orientation);
                        ship.set_attached_mass(20000.0);
                    }
                    else if (start == 1)
                        ship.place_airborne(planet, up, 90.0, 0.0, 3.0, 0.0);
                    else
                        ship.place_on_surface(planet, up, 90.0, 0.0);
                    for (int i = 0; i < 3000; ++i)
                    {
                        universe.world.step(0.02);
                        ship.after_step(0.02);
                    }
                    for (const auto &contact : spec.stages[0].gear)
                    {
                        const sl::dvec3 pad = ship.origin() + ship.body().direction_to_world({contact.x, -2.917, contact.z});
                        const double height = glm::length(pad - universe.position(planet)) - universe.bodies[planet].radius;
                        if (std::abs(height) >= 0.2)
                            std::fprintf(stderr, "%s %s start=%d pad height=%.3f\n", file, body_name, start, height);
                        assert(std::abs(height) < 0.2);
                    }
                    assert(!ship.crashed());
                }
    }
    {
        ShipSpec spec;
        std::string error;
        assert(load_ship_spec(std::string(KOSMOS_ROOT) + "/addons/orbiter-stations/wheel.json", spec, error));
        assert(spec.stages.size() == 1 && spec.stages[0].parts.size() == 2);
        assert(spec.stages[0].parts[0].model.empty());
        assert(spec.stages[0].parts[0].profile.size() == 9);
        assert(spec.stages[0].parts[1].prop == "stage");
        assert(spec.stages[0].radius == 257);
    }
    {
        for (const char *file : {"cargo.json", "fuelpod.json", "lifepod.json"})
        {
            ShipSpec spec;
            std::string error;
            assert(load_ship_spec(std::string(KOSMOS_ROOT) + "/addons/cvel-shuttle-a/" + file, spec, error));
            assert(spec.stages.size() == 1 && spec.stages[0].parts.size() == 1);
            assert(spec.stages[0].parts[0].prop == "stage");
            assert(!spec.ports.empty());
        }
    }
    {
        Universe universe;
        std::string error;
        assert(universe.load(std::string(KOSMOS_ROOT) + "/data/sol.json", error));
        ShipSpec spec;
        assert(load_ship_spec(std::string(KOSMOS_ROOT) + "/addons/cvel-shuttle-a/ship.json", spec, error));
        const int moon = universe.find("Moon");
        sl::TerrainConfig config;
        config.shape = sl::TerrainShape::sphere;
        config.radius = universe.bodies[moon].radius;
        auto source = std::make_shared<sl::FunctionSource>(
            [](double, double) { return 0.0; }, -100000.0, 100000.0,
            [](const sl::dvec3 &direction, double radius) { return radius * 0.02 * direction.z; });
        sl::Terrain terrain(config, source);
        universe.world.body(moon)->terrain = &terrain;
        Ship ship(universe, spec);
        ship.place_on_surface(moon, {1.0, 0.0, 0.0}, 90.0, 0.0);
        for (int i = 0; i < 3000; ++i)
        {
            universe.world.step(0.02);
            ship.after_step(0.02);
        }
        for (const auto &contact : spec.stages[0].gear)
        {
            const sl::dvec3 pad = ship.origin() + ship.body().direction_to_world({contact.x, -2.917, contact.z});
            const sl::dvec3 relative = pad - universe.position(moon);
            const double height = glm::length(relative) - config.radius - terrain.height_at_direction(relative);
            assert(std::abs(height) < 0.3);
        }
        assert(!ship.crashed());
        universe.world.body(moon)->terrain = nullptr;
    }
    {
        for (const char *file : {"/addons/cvel-deltaglider/ship.json", "/addons/cvel-deltaglider-nuclear/ship.json"})
        {
            ShipSpec spec;
            std::string error;
            assert(load_ship_spec(std::string(KOSMOS_ROOT) + file, spec, error));
            bool found = false;
            for (const StageSpec &stage : spec.stages)
                for (const PartSpec &part : stage.parts)
                    if (part.model.find("deltaglider.obj") != std::string::npos)
                    {
                        found = true;
                    }
            assert(found);
            assert(spec.cockpit.vc_model.find("deltaglider_vc.obj") != std::string::npos);
            assert(spec.cockpit.vc_instruments.size() == 2);
            const auto screens = app::Visuals::parse_vc_screens((std::filesystem::path(spec.directory) / spec.cockpit.vc_model).string());
            assert(screens[0].size() == 2 && screens[1].size() == 2);
            assert(spec.cockpit.panel_image.empty());
            assert(!spec.cockpit.instruments.empty());
            assert(!spec.cockpit.controls.empty());
        }
    }
    {
        Universe universe;
        std::string error;
        assert(universe.load(std::string(KOSMOS_ROOT) + "/data/sol.json", error));
        for (const char *file : {"iss.json", "iss_nuclear.json", "mir.json", "mir_nuclear.json"})
        {
            ShipSpec spec;
            assert(load_ship_spec(std::string(KOSMOS_ROOT) + "/addons/orbiter-stations/" + file, spec, error));
            assert(spec.stages.size() == 1 && spec.stages[0].parts.size() == 1);
            if (std::string(file).find("mir") == 0)
                assert(spec.stages[0].parts[0].model.find("mir.obj") != std::string::npos);
            else
                assert(spec.stages[0].parts[0].model.empty() && spec.stages[0].parts[0].prop == "stage");
            assert(spec.ports.size() == (std::string(file).find("mir") == 0 ? 2 : 5));
            Ship station(universe, spec);
            assert(!station.stage_once());
        }
    }
    {
        Universe universe;
        std::string error;
        assert(universe.load(std::string(KOSMOS_ROOT) + "/data/sol.json", error));
        ShipSpec spec;
        assert(load_ship_spec(std::string(KOSMOS_ROOT) + "/addons/probe/ship.json", spec, error));
        assert(spec.stages.size() == 1 && spec.stages[0].parts.size() == 1);
        assert(spec.stages[0].parts[0].model.empty() && spec.stages[0].parts[0].prop == "stage");
        assert(spec.stages[0].engines.size() == 1);
        Ship probe(universe, spec);
        assert(!probe.stage_once());
    }
    {
        Universe universe;
        std::string error;
        assert(universe.load(std::string(KOSMOS_ROOT) + "/data/sol.json", error));
        ShipSpec spec;
        assert(load_ship_spec(std::string(KOSMOS_ROOT) + "/addons/cvel-dragonfly/ship.json", spec, error));
        assert(spec.stages.size() == 1 && spec.stages[0].parts.size() == 1);
        assert(spec.stages[0].parts[0].model.find("dragonfly.obj") != std::string::npos);
        assert(spec.cockpit.panel_image.empty() && !spec.cockpit.instruments.empty() && !spec.cockpit.controls.empty());
        assert(spec.stages[0].engines.size() == 14 && !spec.ports.empty());
        Ship tug(universe, spec);
        assert(!tug.stage_once());
    }
    {
        Universe universe;
        std::string error;
        assert(universe.load(std::string(KOSMOS_ROOT) + "/data/sol.json", error));
        for (const char *file : {"ship.json", "lunar.json"})
        {
            ShipSpec spec;
            assert(load_ship_spec(std::string(KOSMOS_ROOT) + "/addons/salyut/" + file, spec, error));
            assert(spec.stages.size() == 1 && spec.stages[0].parts.size() == 1);
            assert(spec.stages[0].parts[0].model.empty() && spec.stages[0].parts[0].prop == "stage");
            assert(!spec.ports.empty() && spec.stages[0].engines.size() == 2);
            Ship station(universe, spec);
            assert(!station.stage_once());
        }
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
        std::string error;
        for (const char *file : {"cvel-energia-m/ship.json", "cvel-proton/ship.json"})
        {
            ShipSpec spec;
            assert(load_ship_spec(std::string(KOSMOS_ROOT) + "/addons/" + file, spec, error));
            int fairings = 0;
            for (const StageSpec &stage : spec.stages)
                for (const PartSpec &part : stage.parts)
                {
                    assert(part.model.empty());
                    assert(part.prop == "stage" || part.prop == "fairing");
                    if (part.prop == "fairing")
                        ++fairings;
                    if (stage.name == "Solid rocket motors")
                        assert(part.offset.z == 0.0);
                }
            assert(fairings == 1);
        }
    }
    {
        std::string error;
        ShipSpec spec;
        assert(load_ship_spec(std::string(KOSMOS_ROOT) + "/addons/cvel-atlas/ship.json", spec, error));
        assert(spec.stages.size() == 4);
        assert(spec.stages[0].parts.size() == 2);
        assert(spec.stages[1].parts[0].model == "models/atlasiii_fairing1.obj");
        assert(spec.stages[2].parts[0].model == "models/atlasiii_fairing2.obj");
        assert(spec.stages[3].parts[0].model == "models/atlasiii_centaur.obj");
        assert(load_ship_spec(std::string(KOSMOS_ROOT) + "/addons/cvel-delta-iv/ship.json", spec, error));
        assert(spec.stages.size() == 5 && spec.stages[0].parts.size() == 2);
        assert(spec.stages[0].parts[0].model == "models/deltaivbooster_1.obj");
        assert(spec.stages[0].parts[1].model == "models/deltaivbooster_2.obj");
        assert(spec.stages[1].parts[0].model == "models/deltaivstg1.obj");
        assert(spec.stages[2].parts[0].model == "models/deltaivplf_1.obj");
        assert(spec.stages[3].parts[0].model == "models/deltaivplf_2.obj");
        assert(spec.stages[4].parts[0].model == "models/deltaivstg2.obj");
        Universe universe;
        assert(universe.load(std::string(KOSMOS_ROOT) + "/data/sol.json", error));
        Ship rocket(universe, spec);
        assert(rocket.stage_once() && rocket.active_stage() == 1);
        assert(rocket.stage_once() && rocket.active_stage() == 2);
        assert(rocket.stage_once() && rocket.active_stage() == 4);
        assert(rocket.debris().size() == 4);
    }
    {
        Universe universe;
        std::string error;
        assert(universe.load(std::string(KOSMOS_ROOT) + "/data/sol.json", error));
        ShipSpec spec;
        assert(load_ship_spec(std::string(KOSMOS_ROOT) + "/addons/cvel-ares/ship.json", spec, error));
        assert(spec.stages.size() == 1 && spec.stages[0].parts.size() == 1);
        assert(spec.stages[0].parts[0].model.empty() && spec.stages[0].parts[0].prop == "stage");
        assert(spec.stages[0].engines.size() == 4 && spec.stages[0].gear.size() == 4);
        Ship lander(universe, spec);
        assert(!lander.stage_once());
    }
    {
        Universe universe;
        std::string error;
        assert(universe.load(std::string(KOSMOS_ROOT) + "/data/sol.json", error));
        for (const char *file : {"cvel-centaur/ship.json", "cvel-transtage/ship.json"})
        {
            ShipSpec spec;
            assert(load_ship_spec(std::string(KOSMOS_ROOT) + "/addons/" + file, spec, error));
            assert(spec.stages.size() == 1 && !spec.stages[0].parts.empty());
            for (const PartSpec &part : spec.stages[0].parts)
                assert(part.model.empty() && part.prop == "stage");
            Ship rocket(universe, spec);
            assert(!rocket.stage_once());
        }
    }
    {
        Universe universe;
        std::string error;
        assert(universe.load(std::string(KOSMOS_ROOT) + "/data/sol.json", error));
        ShipSpec orbiter, launcher;
        assert(load_ship_spec(std::string(KOSMOS_ROOT) + "/addons/cvel-buran/ship.json", orbiter, error));
        assert(load_ship_spec(std::string(KOSMOS_ROOT) + "/addons/cvel-buran/energia.json", launcher, error));
        assert(orbiter.stages.size() == 1 && orbiter.stages[0].parts.size() == 1);
        assert(orbiter.stages[0].parts[0].model.empty() && orbiter.stages[0].parts[0].prop == "aeroplane");
        assert(launcher.stages.size() == 2 && launcher.stages[0].parts.size() == 4);
        for (const PartSpec &part : launcher.stages[0].parts)
        {
            assert(part.model.empty() && part.prop == "stage" && part.offset.z == 0.0);
            int matches = 0;
            for (const EngineSpec &engine : launcher.stages[0].engines)
                if (engine.position.x == part.offset.x && engine.position.y == part.offset.y)
                    ++matches;
            assert(matches == 1);
        }
        assert(launcher.stages[1].parts.size() == 1 && launcher.stages[1].parts[0].prop == "stage");
        Ship rocket(universe, launcher);
        assert(rocket.stage_once() && rocket.active_stage() == 1);
        assert(!rocket.stage_once() && rocket.debris().size() == 1);
    }
    {
        Universe universe;
        std::string error;
        assert(universe.load(std::string(KOSMOS_ROOT) + "/data/sol.json", error));
        ShipSpec spec;
        assert(load_ship_spec(std::string(KOSMOS_ROOT) + "/addons/cvel-proton/ship.json", spec, error));
        assert(spec.stages.size() == 5 && spec.stages[2].with_next);
        assert(spec.stages[2].parts.size() == 1 && spec.stages[2].parts[0].prop == "fairing");
        assert(spec.stages[3].parts.empty());
        Ship rocket(universe, spec);
        assert(rocket.stage_once() && rocket.active_stage() == 1);
        assert(rocket.stage_once() && rocket.active_stage() == 2);
        assert(rocket.stage_once() && rocket.active_stage() == 4);
        assert(!rocket.stage_once() && rocket.debris().size() == 4);
    }
    {
        Universe universe;
        std::string error;
        assert(universe.load(std::string(KOSMOS_ROOT) + "/data/sol.json", error));
        ShipSpec spec;
        assert(load_ship_spec(std::string(KOSMOS_ROOT) + "/addons/cvel-energia-m/ship.json", spec, error));
        assert(spec.stages.size() == 4 && spec.stages[0].parts.size() == 2);
        assert(spec.stages[0].parts[0].offset.x == 5.95 && spec.stages[0].parts[1].offset.x == -5.95);
        assert(spec.stages[0].parts[0].offset.z == 0.0 && spec.stages[0].parts[1].offset.z == 0.0);
        assert(spec.stages[2].parts.size() == 1 && spec.stages[2].parts[0].prop == "fairing");
        assert(spec.stages[3].parts.empty());
        Ship rocket(universe, spec);
        assert(rocket.stage_once() && rocket.active_stage() == 1);
        assert(rocket.stage_once() && rocket.active_stage() == 2);
        assert(rocket.stage_once() && rocket.active_stage() == 3);
        assert(!rocket.stage_once() && rocket.debris().size() == 3);
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
        assert(rocket.debris().size() == 5);
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
        {
            ShipSpec spec;
            assert(load_ship_spec(std::string(KOSMOS_ROOT) + "/tests/fixtures/radial_pair.json", spec, error));
            assert(spec.stages[0].radial_pair);
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
            assert(rocket.stage_once());
            assert(rocket.active_stage() == 4 && rocket.debris().size() == 5);
            assert(rocket.debris()[3].part == -1 && rocket.debris()[4].part == -1);
        }
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
        // The shipped Soyuz capsules keep their crew healthy on their own supplies.
        struct Case
        {
            const char *file;
            int drop_stage; // stage to jettison after the first stretch, or -1
            double pressure_kpa;
            double days;
        };
        const Case cases[] = {{"/addons/cvel-soyuz/tma.json", 0, 101.3, 3.0},
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
