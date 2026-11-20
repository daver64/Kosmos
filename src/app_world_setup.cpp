#include "app_state.h"
#include "orbiter_tile_terrain.h"
#include "bitmap_release.h"
#include "link/link_server.h"
#ifdef KOSMOS_EXPERIMENTAL_ORBITAL_RELIEF
#include "orbiter_relief_colour_loader.h"
#endif

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace kosmos::app
{
    namespace
    {
        // A cloud map baked with planetbake, if there is one: clouds.dds/.png (or NAME_clouds) in the body's tile pack folder, or
        // <body>_clouds in data/textures-hires. The caller falls back to the procedural clouds when this returns null.
        sl::Bitmap *baked_clouds(const std::string &root, const Body &body, int max_size)
        {
            namespace fs = std::filesystem;
            std::string path;
            if (!body.tiles.empty())
            {
                const std::string folder = root + "/data/textures-hires/" + body.tiles + "/";
                path = find_texture_in_directory("", folder, "clouds", {".dds", ".png"});
                std::error_code ec;
                if (path.empty() && fs::is_directory(folder, ec))
                    for (const fs::directory_entry &entry : fs::directory_iterator(folder, ec))
                    {
                        const std::string name = entry.path().filename().string();
                        if (name.size() > 11 && (name.compare(name.size() - 11, 11, "_clouds.dds") == 0 || name.compare(name.size() - 11, 11, "_clouds.png") == 0))
                            path = entry.path().string();
                    }
            }
            if (path.empty())
            {
                std::string lower = body.name;
                std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                path = find_texture_in_directory(root, "/data/textures-hires/", lower + "_clouds", {".dds", ".png"});
                if (path.empty())
                    path = find_texture_in_directory(root, "/data/kosmos-hires/maps/", lower + "_clouds", {".dds", ".png"});
            }
            if (path.empty())
                return nullptr;
            sl::Bitmap *bitmap = load_surface_texture(path, max_size);
            if (bitmap)
                std::fprintf(stderr, "kosmos: clouds for %s from %s\n", body.name.c_str(), path.c_str());
            return bitmap;
        }
    }

    // Everything drawn or collided with that belongs to one star system; a quantum jump drops it and builds the next.
    void App::leave_system()
    {
        sim.mfd.set_live_bitmap("map-surface", nullptr);
        if (map_texture)
            sl::destroy_bitmap(map_texture);
        map_texture = nullptr;
        map_texture_body = -1;
#ifdef KOSMOS_EXPERIMENTAL_ORBITAL_MICRODETAIL
        orbiter_microtexture_array.reset();
        orbiter_microtexture_assets = {};
        orbiter_microtexture_body = -1;
#endif
        body_terrains.clear();
        terrain_set.items.clear();
        terrain = nullptr;
        if (cloud_shell)
            sl::destroy_mesh(cloud_shell);
        cloud_shell = nullptr;
        if (home_cloud_texture)
            sl::destroy_bitmap(home_cloud_texture);
        home_cloud_texture = nullptr;
        home_cloud_height = cloud_height;
        for (sl::Mesh *m : vis.body_cloud_meshes)
            if (m)
                sl::destroy_mesh(m);
        for (sl::Bitmap *b : vis.body_cloud_textures)
            if (b)
                sl::destroy_bitmap(b);
        vis.body_cloud_meshes.clear();
        vis.body_cloud_textures.clear();
        std::sort(vis.body_textures.begin(), vis.body_textures.end());
        vis.body_textures.erase(std::unique(vis.body_textures.begin(), vis.body_textures.end()), vis.body_textures.end());
        for (sl::Bitmap *b : vis.body_textures)
            sl::destroy_bitmap(b);
        for (sl::Bitmap *b : vis.body_normals)
            if (b)
                sl::destroy_bitmap(b);
        for (sl::Mesh *m : vis.rings)
            if (m)
                sl::destroy_mesh(m);
        vis.body_textures.clear();
        vis.body_normals.clear();
        for (sl::Model *m : vis.body_meshes)
            if (m)
                sl::destroy_model(m);
        vis.body_meshes.clear();
        vis.earth_tiles.reset();
#ifdef KOSMOS_EXPERIMENTAL_ORBITAL_RELIEF
        vis.earth_relief_tiles.reset();
#endif
        vis.earth_cloud_tiles.reset();
        vis.rings.clear();
        vis.surface_textured.clear();
    }

    // Dates and bodies that depend on where the planets stand: re-run when the start date changes without a rebuild of the system.
    void App::place_worlds()
    {
        earth_body = &sim.universe.bodies[static_cast<std::size_t>(earth_id)];
        sim.universe.bodies[static_cast<std::size_t>(earth_id)].rotation_offset += options.earth_turn;
        // A tidally locked body with terrain does not spin, so its far side is fixed: face Earth at time zero.
        for (std::size_t i = 0; i < sim.universe.bodies.size(); ++i)
        {
            kosmos::Body &b = sim.universe.bodies[i];
            if (!b.terrain.empty() && static_cast<int>(i) != earth_id && b.parent >= 0 && (!b.has_meridian || sim.universe.epoch_override <= 0.0))
            {
                const sl::dvec3 to_parent = sim.universe.position(b.parent) - sim.universe.position(static_cast<int>(i));
                // Longitude 0 faces the parent: the Moon toward Earth, an undated Mars toward the Sun.
                const sl::dvec3 pole = sim.universe.pole(static_cast<int>(i));
                const sl::dvec3 lon0 = b.axis_tilt * sl::dvec3(1.0, 0.0, 0.0);
                b.rotation_offset = std::atan2(glm::dot(pole, glm::cross(lon0, to_parent)), glm::dot(lon0, to_parent));
            }
            if (b.gravity_harmonics && !b.gravity_frame_defined)
                sim.universe.world.body(static_cast<int>(i))->gravity_orientation =
                    b.axis_tilt * glm::angleAxis(b.rotation_offset, sl::dvec3(0.0, 1.0, 0.0));
        }
        loading.note("Placing bases and beacons");
        sim.nav.truncate_beacons(base_beacon_mark);
        for (kosmos::Base &base : sim.bases)
        {
            const int body = sim.universe.find(base.body);
            if (body >= 0)
            {
                base.direction = kosmos::base_direction(sim.universe, body, base);
                base.pole = sim.universe.pole(body);
                base.height = base.elevation;
                kosmos::register_base_beacons(sim.nav, sim.universe, body, base);
            }
        }
    }

    // A scenario on another date in the current system keeps the terrain, textures and cloud layers; only the planets' positions
    // and the turn of each terrain body (baked into the terrain when it was built) are brought to the new date.
    bool App::change_date(double epoch_jd, std::string &message)
    {
        std::vector<double> built(body_terrains.size(), 0.0);
        for (std::size_t k = 0; k < body_terrains.size(); ++k)
        {
            const sl::FlightBody *fb = sim.universe.world.body(body_terrains[k]->body);
            built[k] = sim.universe.rotation_angle(body_terrains[k]->body) - (fb->spin_phase + fb->spin_rate * sim.universe.world.time());
        }
        // The terrain was levelled under each base at the height sampled when it was built; place_worlds would reset it to the configured elevation.
        std::vector<double> base_heights;
        for (const kosmos::Base &base : sim.bases)
            base_heights.push_back(base.height);
        const double old_epoch = sim.start_epoch_jd;
        sim.start_epoch_jd = epoch_jd;
        if (!sim.change_system(sim.system_index(), message))
        {
            sim.start_epoch_jd = old_epoch;
            return false;
        }
        place_worlds();
        for (std::size_t k = 0; k < sim.bases.size() && k < base_heights.size(); ++k)
            sim.bases[k].height = base_heights[k];
        for (std::size_t k = 0; k < body_terrains.size(); ++k)
        {
            sl::FlightBody *fb = sim.universe.world.body(body_terrains[k]->body);
            fb->spin_phase = sim.universe.bodies[static_cast<std::size_t>(body_terrains[k]->body)].rotation_offset - built[k];
            fb->terrain = body_terrains[k]->terrain.get();
        }
        return true;
    }

    bool App::set_date(double epoch_jd, std::string &error)
    {
        if (!std::isfinite(epoch_jd) || epoch_jd <= 0.0)
        {
            error = "Invalid date.";
            return false;
        }
        if (eva.active || !sim.active_ship() || sim.active_ship()->spec().building())
        {
            error = "Return to a ship's controls before changing the date.";
            return false;
        }
        if (netplay.mode() != NetPlay::Mode::none || flight_replay.playing || flight_replay.capturing ||
            (link_state.link_server && link_state.link_server->lockstep_active()))
        {
            error = "Disconnect multiplayer and stop flight recording, playback and lockstep before changing the date.";
            return false;
        }
        for (int i = 0; i < sim.ship_count(); ++i)
        {
            const Ship &vessel = *sim.ship(i);
            if (!vessel.spec().building() &&
                (vessel.telemetry().soi < 0 || vessel.telemetry().soi >= static_cast<int>(sim.universe.bodies.size())))
            {
                error = "A vessel has no reference body; its state cannot be restored.";
                return false;
            }
        }
        const double previous_date = sim.universe.julian_date();
        Scenario saved = capture_scenario(sim, "Date change");
        saved.weather = sim.props.get_string("/sim/weather-force");
        std::vector<std::vector<double>> gear_compression;
        std::vector<bool> surface_fixed, surface_resting;
        for (int i = 0; i < sim.ship_count(); ++i)
        {
            const Ship &vessel = *sim.ship(i);
            if (vessel.spec().building())
                continue;
            std::vector<double> compression;
            for (const sl::GearContact &gear : vessel.body().gear)
                compression.push_back(gear.compression);
            gear_compression.push_back(std::move(compression));
            surface_fixed.push_back(vessel.telemetry().landed || vessel.surface_attached());
            surface_resting.push_back(vessel.surface_resting());
        }
        const std::size_t saved_links = static_cast<std::size_t>(std::count_if(saved.ships.begin(), saved.ships.end(),
            [](const ScenarioShip &entry) { return entry.dock_to >= 0; }));
        if (saved_links != sim.nav.dock_links().size())
        {
            error = "This docking network cannot be restored by a scenario; undock it before changing the date.";
            return false;
        }
        bool changed;
        do
        {
            changed = false;
            for (std::size_t i = 0; i < saved.ships.size(); ++i)
            {
                const int leader = saved.ships[i].dock_to;
                if (leader < 0 || surface_fixed[i] == surface_fixed[static_cast<std::size_t>(leader)])
                    continue;
                if (saved.ships[i].body != saved.ships[static_cast<std::size_t>(leader)].body)
                {
                    error = "A docked vessel has a different reference body; its state cannot be restored.";
                    return false;
                }
                surface_fixed[i] = surface_fixed[static_cast<std::size_t>(leader)] = true;
                changed = true;
            }
        } while (changed);
        std::vector<sl::dquat> old_frames;
        std::vector<sl::dvec3> old_spins;
        for (std::size_t i = 0; i < sim.universe.bodies.size(); ++i)
        {
            const sl::FlightBody &body = *sim.universe.world.body(static_cast<int>(i));
            old_frames.push_back(sim.universe.orientation(static_cast<int>(i)));
            old_spins.push_back(body.spin_axis * body.spin_rate);
        }
        if (!change_date(epoch_jd, error))
            return false;
        ship = nullptr;
        vis.clear_parts();
        Scenario shifted = saved;
        for (std::size_t i = 0; i < shifted.ships.size(); ++i)
        {
            ScenarioShip &entry = shifted.ships[i];
            if (!surface_fixed[i] || !entry.has_state)
                continue;
            entry.landed = true;
            const int body_id = sim.universe.find(entry.body);
            const sl::FlightBody &body = *sim.universe.world.body(body_id);
            const sl::dquat turn = sim.universe.orientation(body_id) * glm::inverse(old_frames[static_cast<std::size_t>(body_id)]);
            const sl::dvec3 old_position(entry.state_position[0], entry.state_position[1], entry.state_position[2]);
            const sl::dvec3 old_velocity(entry.state_velocity[0], entry.state_velocity[1], entry.state_velocity[2]);
            const sl::dvec3 position = turn * old_position;
            const sl::dvec3 velocity = turn * (old_velocity - glm::cross(old_spins[static_cast<std::size_t>(body_id)], old_position)) +
                                       glm::cross(body.spin_axis * body.spin_rate, position);
            const sl::dquat orientation = turn * sl::dquat(entry.state_orientation[0], entry.state_orientation[1],
                                                            entry.state_orientation[2], entry.state_orientation[3]);
            entry.state_position = {position.x, position.y, position.z};
            entry.state_velocity = {velocity.x, velocity.y, velocity.z};
            entry.state_orientation = {orientation.w, orientation.x, orientation.y, orientation.z};
        }
        const auto restore_gear = [&]() {
            if (static_cast<std::size_t>(sim.ship_count()) < gear_compression.size())
                return false;
            for (std::size_t i = 0; i < gear_compression.size(); ++i)
            {
                Ship &vessel = *sim.ship(static_cast<int>(i));
                if (vessel.body().gear.size() != gear_compression[i].size())
                    return false;
                for (std::size_t j = 0; j < gear_compression[i].size(); ++j)
                    vessel.body().gear[j].compression = gear_compression[i][j];
                vessel.refresh_telemetry();
                if (surface_resting[i])
                    vessel.hold_scenario_landed();
            }
            return true;
        };
        ship = apply_scenario(sim, shifted, terrain_set);
        if (!ship || !restore_gear())
        {
            ship = nullptr;
            std::string restore_error;
            if (change_date(previous_date, restore_error))
            {
                ship = apply_scenario(sim, saved, terrain_set);
                if (ship && !restore_gear())
                {
                    restore_error = "landing gear state did not match";
                    ship = nullptr;
                }
            }
            if (!ship)
            {
                error = "Could not restore the flight after changing the date: " + restore_error;
                std::fprintf(stderr, "kosmos: %s\n", error.c_str());
                running = false;
                return false;
            }
            sim.weather.date_changed(sim.universe);
            const Telemetry &telemetry = ship->telemetry();
            sim.weather.step(sim.universe, sim.props, telemetry.soi, telemetry.latitude, telemetry.longitude, telemetry.altitude, 0.0);
            sim.props.set_bool("/sim/paused", true);
            rig.cam_distance = ship->spec().camera_distance;
            rig.interior_cam = rig.base_view = rig.user_cam = -1;
            cockpit_view = cockpit3d = false;
            arsenal.reset();
            ship_weapons.reset();
            error = "Could not restore the flight at the requested date; restored the previous date.";
            sim.log(error);
            return false;
        }
        sim.weather.date_changed(sim.universe);
        const Telemetry &telemetry = ship->telemetry();
        sim.weather.step(sim.universe, sim.props, telemetry.soi, telemetry.latitude, telemetry.longitude, telemetry.altitude, 0.0);
        sim.props.set_bool("/sim/paused", true);
        rig.cam_distance = ship->spec().camera_distance;
        rig.interior_cam = rig.base_view = rig.user_cam = -1;
        cockpit_view = cockpit3d = false;
        arsenal.reset();
        ship_weapons.reset();
        sim.log("Date set to " + format_utc(sim.universe.julian_date()) + "; simulation paused");
        return true;
    }

    int App::test_date_change()
    {
        if (sim.props.get_bool("/selftest/date-daylight"))
        {
            double day = 0.0, night = 0.0;
            if (!parse_utc_datetime("2024-03-20 16:00:00", day) ||
                !parse_utc_datetime("2024-03-20 04:00:00", night))
                return 1;
            const int body_id = ship->telemetry().soi;
            const std::size_t link_count = sim.nav.dock_links().size();
            if (!ship->surface_attached() || body_id != sim.universe.find("Earth") ||
                (sim.props.get_bool("/selftest/date-docked") && link_count < 2))
            {
                std::fprintf(stderr, "date-daylight selftest: expected a surface-placed Earth vessel and requested docks\n");
                return 1;
            }
            const bool resting = ship->surface_resting();
            std::vector<bool> linked(static_cast<std::size_t>(sim.ship_count()), false);
            linked[static_cast<std::size_t>(sim.props.get_int("/sim/active-ship"))] = true;
            bool changed;
            do
            {
                changed = false;
                for (const auto &link : sim.nav.dock_links())
                {
                    const std::size_t a = static_cast<std::size_t>(link[0]), b = static_cast<std::size_t>(link[2]);
                    if (linked[a] != linked[b])
                    {
                        linked[a] = linked[b] = true;
                        changed = true;
                    }
                }
            } while (changed);
            std::vector<std::string> names;
            std::vector<sl::dvec3> positions;
            for (std::size_t i = 0; i < linked.size(); ++i)
                if (linked[i])
                {
                    const Ship &vessel = *sim.ship(static_cast<int>(i));
                    names.push_back(vessel.name);
                    positions.push_back(glm::inverse(sim.universe.orientation(body_id)) *
                                        (vessel.body().position - sim.universe.position(body_id)));
                }
            const auto max_position_error = [&]() {
                double largest = 0.0;
                if (sim.nav.dock_links().size() != link_count ||
                    static_cast<std::size_t>(sim.ship_count()) < positions.size() ||
                    (resting && !ship->surface_resting()))
                    return 1e30;
                for (std::size_t i = 0; i < positions.size(); ++i)
                {
                    const Ship &vessel = *sim.ship(static_cast<int>(i));
                    if (vessel.name != names[i])
                        return 1e30;
                    const sl::dvec3 local = glm::inverse(sim.universe.orientation(body_id)) *
                                            (vessel.body().position - sim.universe.position(body_id));
                    largest = std::max(largest, glm::length(local - positions[i]));
                }
                return largest;
            };
            std::string error;
            const auto sun_height = [&]() {
                const sl::dvec3 up = glm::normalize(ship->body().position - sim.universe.position(body_id));
                const sl::dvec3 sun = glm::normalize(sim.universe.position(0) - ship->body().position);
                return glm::dot(up, sun);
            };
            if (!set_date(day, error))
            {
                std::fprintf(stderr, "date-daylight selftest: %s\n", error.c_str());
                return 1;
            }
            const double daylight = sun_height();
            const double day_error = max_position_error();
            if (!set_date(night, error))
            {
                std::fprintf(stderr, "date-daylight selftest: %s\n", error.c_str());
                return 1;
            }
            const double darkness = sun_height();
            const double night_error = max_position_error();
            const bool sunlight_ok = sim.props.get_bool("/selftest/date-surface-only") || (daylight > 0.5 && darkness < -0.5);
            const bool ok = sunlight_ok && day_error < 0.05 && night_error < 0.05;
            std::printf("%s: sun elevation sin %.3f %.3f, surface errors %.3f %.3f m\n",
                        ok ? "ok" : "FAIL", daylight, darkness, day_error, night_error);
            if (ok && sim.props.get_bool("/selftest/date-noon-last") && !set_date(day, error))
            {
                std::fprintf(stderr, "date-daylight selftest: %s\n", error.c_str());
                return 1;
            }
            if (ok && sim.props.get_bool("/selftest/date-launch"))
            {
                const double initial_radius = glm::length(ship->body().position - sim.universe.position(body_id));
                sim.props.set_bool("/sim/paused", false);
                sim.props.set_double("/controls/throttle", 1.0);
                for (int i = 0; i < 100; ++i)
                    sim.step(0.1);
                const double rise = glm::length(ship->body().position - sim.universe.position(body_id)) - initial_radius;
                const bool launched = rise > 10.0 && !ship->surface_resting();
                std::printf("%s: launch after date changes, rise %.1f m\n", launched ? "ok" : "FAIL", rise);
                if (!launched)
                    return 1;
            }
            return ok ? 0 : 1;
        }
        int failures = 0;
        const auto check = [&](bool ok, const char *description) {
            std::printf("%s: %s\n", ok ? "ok" : "FAIL", description);
            failures += ok ? 0 : 1;
        };
        const bool ground_test = sim.props.get_bool("/selftest/date-ground");
        for (int i = 0; i < (ground_test ? 30 : 1); ++i)
            sim.step(0.1);
        const int body_id = ship->telemetry().soi;
        if (body_id < 0 || body_id >= static_cast<int>(sim.universe.bodies.size()))
        {
            std::fprintf(stderr, "date-change selftest: ship has no reference body\n");
            return 1;
        }
        const bool landed = ship->telemetry().landed;
        if (ground_test && !landed)
        {
            std::fprintf(stderr, "date-change selftest: ship did not settle on the ground\n");
            return 1;
        }
        const double before = sim.universe.julian_date();
        const sl::dvec3 position = ship->body().position - sim.universe.position(body_id);
        const sl::FlightBody &parent = *sim.universe.world.body(body_id);
        const sl::dvec3 velocity = ship->body().velocity - parent.velocity;
        const sl::dvec3 spin = parent.spin_axis * parent.spin_rate;
        const sl::dquat frame_before = sim.universe.orientation(body_id);
        const sl::dquat orientation = ship->body().orientation;
        const sl::dvec3 earth_before = sim.system_index() == 0 ? sim.universe.position(sim.universe.find("Earth")) : sl::dvec3(0.0);
        const double fuel = ship->telemetry().stage_fuel;
        const std::string name = ship->name;
        const std::size_t links = sim.nav.dock_links().size();
        if (sim.props.get_bool("/selftest/date-docked") && links == 0)
        {
            std::fprintf(stderr, "date-change selftest: expected a docked vessel\n");
            return 1;
        }
        sim.props.set_string("/sim/weather-force", "TEST 011200Z 00000KT CAVOK 15/10 Q1013");
        eva.active = true;
        std::string error;
        check(!set_date(before + 120.0, error) && !error.empty() && sim.universe.julian_date() == before,
              "date change is blocked on foot without changing the flight");
        eva.active = false;
        error.clear();
        if (!set_date(before + 120.0, error))
        {
            std::fprintf(stderr, "date-change selftest: %s\n", error.c_str());
            return 1;
        }
        const sl::dvec3 new_position = ship->body().position - sim.universe.position(body_id);
        const sl::FlightBody &new_parent = *sim.universe.world.body(body_id);
        const sl::dquat turn = sim.universe.orientation(body_id) * glm::inverse(frame_before);
        const sl::dvec3 expected = landed ? turn * position : position;
        const sl::dvec3 expected_velocity = landed ? turn * (velocity - glm::cross(spin, position)) +
                                                        glm::cross(new_parent.spin_axis * new_parent.spin_rate, expected) : velocity;
        const sl::dquat expected_orientation = landed ? turn * orientation : orientation;
        check(std::abs(sim.universe.julian_date() - before - 120.0) < 1e-8, "requested UTC date");
        check(glm::length(new_position - expected) < 0.05, landed ? "landed vessel remains body-fixed" : "orbit remains body-relative");
        check(glm::length(ship->body().velocity - new_parent.velocity - expected_velocity) < 0.01 &&
                  std::abs(std::abs(glm::dot(ship->body().orientation, expected_orientation)) - 1.0) < 1e-6,
              "vessel velocity and orientation restored");
        if (sim.system_index() == 0)
            check(glm::length(sim.universe.position(sim.universe.find("Earth")) - earth_before) > 1e6,
                  "planetary positions rebuilt");
        check(ship->name == name && std::abs(ship->telemetry().stage_fuel - fuel) < 0.01 &&
                  sim.nav.dock_links().size() == links,
              "vessel fuel and docking restored");
        check(sim.props.get_bool("/sim/paused") && sim.props.get_double("/controls/throttle") == 0.0 &&
                  sim.props.get_string("/sim/weather-force") == "TEST 011200Z 00000KT CAVOK 15/10 Q1013" &&
                  (!Weather::supports_body(sim.universe, body_id) ||
                   sim.props.get_string("/environment/metar") == "TEST 011200Z 00000KT CAVOK 15/10 Q1013"),
              "simulation paused, throttle cut and weather updated");
        if (!sim.props.get_bool("/selftest/date-render"))
        {
            if (landed)
                check(ship->telemetry().landed, "landing contact survives while paused");
            error.clear();
            const bool restored = set_date(before, error);
            const sl::dvec3 roundtrip_expected = landed
                ? sim.universe.orientation(body_id) * glm::inverse(frame_before) * position : position;
            check(restored && std::abs(sim.universe.julian_date() - before) < 1e-8 &&
                      glm::length(ship->body().position - sim.universe.position(body_id) - roundtrip_expected) < 0.05 &&
                      (!landed || ship->telemetry().landed),
                  "changing back restores the original date and vessel position");
            if (!restored)
                std::fprintf(stderr, "date-change selftest: %s\n", error.c_str());
        }
        return failures == 0 ? 0 : 1;
    }

    // Swaps in the best cached far-view image of every invented world at the width now chosen in the settings; the larger widths
    // that are missing are made in the background and swapped in as they finish.
    void App::reload_world_textures()
    {
        for (std::size_t i = 0; i < sim.universe.bodies.size() && i < vis.body_textures.size(); ++i)
        {
            const kosmos::Body &b = sim.universe.bodies[i];
            if (b.minor || !b.style.valid() || !find_existing_surface_image(sim.root(), b).empty())
                continue;
            if (std::find(vis.surface_textured.begin(), vis.surface_textured.end(), static_cast<int>(i)) == vis.surface_textured.end())
                continue;
            const std::string image = generated_surface_image(b);
            if (image.empty())
                continue;
            if (sl::Bitmap *image_bitmap = load_surface_texture(image, options.max_texture))
            {
                sl::destroy_bitmap(vis.body_textures[i]);
                vis.body_textures[i] = image_bitmap;
            }
        }
        world_jobs.cancel();
        world_jobs_pending = true;
    }

    // Queues the missing larger images, nearest world first, and swaps in the ones the background jobs have finished.
    void App::update_world_textures()
    {
        if (world_jobs_pending && ship)
        {
            world_jobs_pending = false;
            const sl::dvec3 eye = ship->origin();
            std::vector<std::pair<double, std::size_t>> order;
            for (std::size_t i = 0; i < sim.universe.bodies.size(); ++i)
            {
                const kosmos::Body &b = sim.universe.bodies[i];
                if (b.minor || !b.style.valid() || !find_existing_surface_image(sim.root(), b).empty() || generated_missing_widths(b).empty())
                    continue;
                order.push_back({glm::length(sim.universe.position(static_cast<int>(i)) - eye), i});
            }
            std::sort(order.begin(), order.end());
            // Every world's 2k first, nearest first, then the full width.
            for (const int pass : {0, 1})
                for (const auto &entry : order)
                {
                    const kosmos::Body &b = sim.universe.bodies[entry.second];
                    const std::vector<int> missing = generated_missing_widths(b);
                    if (pass < static_cast<int>(missing.size()))
                        world_jobs.add({sim.system_index(), b.name, missing[static_cast<std::size_t>(pass)],
                                        kosmos::far_image_path(b.style, missing[static_cast<std::size_t>(pass)]).string()});
                }
        }
        for (const kosmos::WorldTextureJobs::Job &job : world_jobs.poll())
        {
            if (job.system != sim.system_index())
                continue;
            const int id = sim.universe.find(job.body);
            std::error_code ec;
            if (id < 0 || !std::filesystem::exists(job.path, ec))
                continue;
            const std::size_t i = static_cast<std::size_t>(id);
            const kosmos::Body &b = sim.universe.bodies[i];
            if (!find_existing_surface_image(sim.root(), b).empty())
                continue;
            // Only ever swap upwards, and never past the width chosen in the settings.
            const std::string best = generated_surface_image(b);
            if (best != job.path || i >= vis.body_textures.size())
                continue;
            if (sl::Bitmap *image_bitmap = load_surface_texture(job.path, options.max_texture))
            {
                sl::destroy_bitmap(vis.body_textures[i]);
                vis.body_textures[i] = image_bitmap;
            }
        }
    }

    void App::load_planet_images()
    {
        world_jobs.cancel();
        world_jobs_pending = true;
        loading.note("Loading planet images");
        sl::Bitmap *shared_rock = nullptr;
        for (std::size_t i = 0; i < sim.universe.bodies.size(); ++i)
        {
            if (loading.started)
                loading.pump();
            const kosmos::Body &b = sim.universe.bodies[i];
            const sl::Colour average{static_cast<std::uint8_t>(b.colour[0] * 255.0f), static_cast<std::uint8_t>(b.colour[1] * 255.0f),
                                     static_cast<std::uint8_t>(b.colour[2] * 255.0f), 255};
            // The anonymous belt rocks share one texture instead of generating ninety.
            if (b.minor && shared_rock)
                vis.body_textures.push_back(nullptr);
            else
            {
                vis.body_textures.push_back(b.fallback_texture == "moon" ? fx::make_moon_texture() : fx::make_detail_texture(average, 31 + static_cast<int>(i) * 7, 0.45f, 0.15f, 256));
                if (b.minor)
                    shared_rock = vis.body_textures.back();
            }
            sl::Model *shape = nullptr;
            // A mesh under data/textures-hires/ is optional; without it the body stays a sphere.
            if (!b.mesh.empty() && std::filesystem::exists(sim.root() + "/data/" + b.mesh))
            {
                shape = sl::load_model(sim.root() + "/data/" + b.mesh);
                if (!shape || shape->parts.empty() || !shape->parts[0].mesh)
                {
                    std::fprintf(stderr, "[kosmos] cannot load body mesh %s: %s\n", b.mesh.c_str(), sl::last_error().c_str());
                    if (shape)
                        sl::destroy_model(shape);
                    shape = nullptr;
                }
            }
            vis.body_meshes.push_back(shape);
            if (!b.minor)
            {
                loading.note("Loading the surface of " + b.name);
                const std::string image = find_surface_image(sim.root(), b);
                if (!image.empty())
                    if (sl::Bitmap *image_bitmap = load_surface_texture(image, options.max_texture))
                    {
                        sl::destroy_bitmap(vis.body_textures.back());
                        vis.body_textures.back() = image_bitmap;
                        vis.surface_textured.push_back(static_cast<int>(i));
                    }
            }
            {
                sl::Bitmap *normal = nullptr;
                std::string lower = b.name;
                for (char &c : lower)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                std::string celestia_normal = find_texture_in_directory(sim.root(), "/data/textures-hires/", lower + "_normal",
                                                                         {".dds", ".png", ".jpg", ".jpeg"});
                if (celestia_normal.empty())
                    celestia_normal = find_texture_in_directory(sim.root(), "/data/kosmos-hires/maps/", lower + "_normal",
                                                                 {".dds", ".png", ".jpg", ".jpeg"});
                if (celestia_normal.empty())
                    celestia_normal = find_image_in_folder(sim.root() + "/import/celestia/hires/" + lower + "_normal");
                if (celestia_normal.empty())
                    celestia_normal = find_texture_in_directory(sim.root(), "/data/textures/", lower + "_normal",
                                                                 {".dds", ".png", ".jpg", ".jpeg"});
                if (!b.minor && !options.no_relief && !celestia_normal.empty())
                    normal = load_surface_texture(celestia_normal, options.max_texture);
                if (normal)
                {
                    normal->smooth = true;
                    normal->mipmaps = true;
                }
                vis.body_normals.push_back(normal);
            }
            vis.rings.push_back(b.ring_outer > 0.0 ? make_ring_mesh(b.ring_inner / b.ring_outer, b.ring_colour) : nullptr);
        }
        for (std::size_t i = 0; i < sim.universe.bodies.size(); ++i)
            if (!vis.body_textures[i])
                vis.body_textures[i] = shared_rock;

    }

#ifdef KOSMOS_EXPERIMENTAL_ORBITAL_RELIEF
    std::unique_ptr<sl::PlanetTiles> App::make_orbiter_relief_tiles(const BodyTerrain &terrain)
    {
        if (!terrain.orbiter_imagery || !terrain.has_orbiter_elevation)
            return nullptr;
        const auto image = terrain.orbiter_imagery;
        const auto meshes = terrain.terrain_meshes;
        const auto heights = terrain.terrain->source();
        const kosmos::Body &body = sim.universe.bodies[static_cast<std::size_t>(terrain.body)];
        if (!heights)
        {
            std::fprintf(stderr, "[kosmos] %s Orbiter relief unavailable: terrain source unavailable\n", body.name.c_str());
            return nullptr;
        }
        const glm::dquat terrain_frame = terrain.frame.orientation;
        sl::PlanetTilesOptions relief;
        relief.has_colour = [image](int level, int column, int row)
        {
            return image->has_tile(level + 4, row, column);
        };
        relief.has_colour_children = [image](int level, int column, int row)
        {
            return image->has_children(level + 4, row, column);
        };
        relief.load_colour = [image, meshes, terrain_frame](int level, int column, int row) -> sl::Bitmap *
        {
            return kosmos::load_orbiter_relief_colour(*image, meshes.get(), terrain_frame, level, column, row);
        };
        const double radius = body.radius;
        relief.elevation = [heights, terrain_frame, radius](const glm::dvec3 &direction)
        {
            return kosmos::orbiter_tile_height(*heights, terrain_frame, direction, radius, 1000.0);
        };
        relief.elevation_lod = [heights, terrain_frame, radius](const glm::dvec3 &direction, double cell_size)
        {
            return kosmos::orbiter_tile_height(*heights, terrain_frame, direction, radius, cell_size * radius);
        };
        relief.max_elevation = std::max({std::fabs(heights->min_height()), std::fabs(heights->max_height()), 1.0}) / radius;
        relief.normal_sample_spacing = 1000.0 / radius;
        relief.match_elevation_edges = true;
        relief.adaptive_elevation_diagonals = true;
        relief.adaptive_elevation_normals = true;
        relief.colour_max_level = 18;
        relief.tile_size = 512;
        relief.segments = 24;
        relief.geometry_pixels = 32.0f;
        relief.max_relief_patches = options.orbital_relief_patches;
        relief.max_relief_meshes = options.orbital_relief_meshes;
        relief.max_relief_mesh_jobs = 4;
        relief.max_cached_tiles = options.orbital_relief_textures == 0 ? 512 :
                                  std::max(8, options.orbital_relief_textures / 4 * 3);
        relief.max_resident_tiles = options.orbital_relief_textures;
        relief.max_failed_tiles = 256;
        relief.max_loads_in_flight = 4;
        relief.feather = 0.0f;
        relief.fade_seconds = 0.0f;
        relief.skirt = 0.0f;
        auto result = sl::PlanetTiles::create(relief);
        if (!result)
            std::fprintf(stderr, "[kosmos] %s Orbiter relief unavailable: %s\n", body.name.c_str(), sl::last_error().c_str());
        else
            std::fprintf(stderr, "[kosmos] %s Orbiter relief budgets: patches %d, meshes %d, textures %d (0 = unlimited)\n",
                         body.name.c_str(), relief.max_relief_patches, relief.max_relief_meshes, relief.max_resident_tiles);
        return result;
    }
#endif

    void App::load_earth_tiles()
    {
        // Tiled Earth imagery (optional, not in the repository: see the Developers Manual) replaces the single image on the sphere.
        if (!options.no_tiles)
        {
            const int home_body = sim.universe.home_id();
            const std::string pack = home_body >= 0 ? sim.universe.bodies[static_cast<std::size_t>(home_body)].tiles : std::string();
            if (home_body >= 0)
            {
                if (sim.universe.bodies[static_cast<std::size_t>(home_body)].imagery == "celestia" || !pack.empty())
                {
                    loading.note("Opening the Earth imagery tiles");
                    const std::string hires = sim.root() + "/import/celestia/hires/";
                    sl::PlanetTilesOptions tiles;
                    // Skirts show as bright lines along tile edges from orbit.
                    tiles.skirt = 0.0f;
                    // Full-strength, tight glint blows out into a white disc from orbit.
                    tiles.specular_strength = 0.45f;
                    tiles.specular_min_roughness = 0.3f;
                    if (!pack.empty())
                    {
                        const std::string base = sim.root() + "/data/textures-hires/" + pack + "/";
                        tiles.colour = find_tile_source(base + "colour");
                        tiles.specular = find_tile_source(base + "specular");
                        if (!options.no_relief)
                            tiles.normal = find_tile_source(base + "normal");
                    }
                    else
                    {
                        tiles.colour = find_tile_source(hires + "earth");
                        tiles.specular = find_tile_source(hires + "earth_specular");
                        if (!options.no_relief)
                            tiles.normal = find_tile_source(hires + "earth_normal/textures/medres/earth-normals-hs-32k");
                        if (tiles.colour.empty())
                        {
                            tiles.colour = find_managed_earth_tiles(sim.root(), sim.universe.bodies[static_cast<std::size_t>(home_body)]);
                            if (!tiles.colour.empty() && !options.no_relief && tiles.normal.empty())
                                tiles.normal = find_tile_source(sim.root() + "/data/kosmos-hires/maps/normal");
                        }
                        if (tiles.colour.empty() && sim.universe.bodies[static_cast<std::size_t>(home_body)].name == "Earth")
                        {
                            const std::string defaults = sim.root() + "/data/orbiter-defaults/earth/";
                            tiles.colour = find_tile_source(defaults + "colour");
                            tiles.specular = find_tile_source(defaults + "specular");
                            tiles.normal.clear();
                        }
                    }
                    if (!tiles.colour.empty())
                    {
                        vis.earth_tiles = sl::PlanetTiles::create(tiles);
                        if (!vis.earth_tiles)
                            std::fprintf(stderr, "kosmos: tiled Earth unavailable: %s\n", sl::last_error().c_str());
                        else
                            std::fprintf(stderr, "kosmos: Earth tiles: colour=%s, specular=%s\n",
                                         tiles.colour.c_str(), tiles.specular.empty() ? "(none)" : tiles.specular.c_str());
                    }
                }
#ifdef KOSMOS_EXPERIMENTAL_ORBITAL_RELIEF
                if (!body_terrains.empty() && body_terrains[0] && body_terrains[0]->body == home_body)
                    vis.earth_relief_tiles = make_orbiter_relief_tiles(*body_terrains[0]);
#endif
                if (vis.earth_tiles)
                {
                    const std::size_t index = static_cast<std::size_t>(home_body);
                    if (index < vis.body_textures.size())
                    {
                        sl::destroy_bitmap(vis.body_textures[index]);
                        vis.body_textures[index] = nullptr;
                    }
                    if (index < vis.body_normals.size())
                    {
                        sl::destroy_bitmap(vis.body_normals[index]);
                        vis.body_normals[index] = nullptr;
                    }
                }
            }
        }
    }

    // The cloud layer as tiles, when the pack has them: Orbiter's cloud tree reaches about 600 m a pixel, far past one image.
    void App::load_earth_cloud_tiles()
    {
        vis.earth_cloud_tiles.reset();
        if (options.no_tiles || options.no_clouds)
            return;
        const std::string source = find_tile_source(sim.root() + "/data/textures-hires/earth_clouds");
        if (source.empty())
            return;
        sl::PlanetTilesOptions clouds;
        clouds.colour = source;
        clouds.skirt = 0.0f;
        clouds.segments = 16;
        clouds.max_cached_tiles = 384;
        clouds.fade_seconds = 0.6f;
        // A thin shell seen edge-on at the limb would smear into streaks above the horizon, so it fades as the view grazes it.
        clouds.limb_fade = 0.3f;
        vis.earth_cloud_tiles = sl::PlanetTiles::create(clouds);
        if (!vis.earth_cloud_tiles)
            std::fprintf(stderr, "kosmos: tiled clouds unavailable: %s\n", sl::last_error().c_str());
    }

    bool App::build_terrains()
    {
        try
        {
            body_terrains.push_back(make_body_terrain(sim, earth_id, true));
            for (std::size_t i = 0; i < sim.universe.bodies.size(); ++i)
                if (static_cast<int>(i) != earth_id && (!sim.universe.bodies[i].terrain.empty() || sim.universe.bodies[i].style.has_ground()))
                    if (std::unique_ptr<BodyTerrain> extra = make_body_terrain(sim, static_cast<int>(i), false))
                        body_terrains.push_back(std::move(extra));
        }
        catch (const std::exception &e)
        {
            std::fprintf(stderr, "Terrain unavailable: %s\n", e.what());
            return false;
        }
        return true;
    }

    void App::build_cloud_layers()
    {
        // Weather for the other worlds that have it: a generated cloud layer on its own shell.
        vis.body_cloud_meshes.assign(sim.universe.bodies.size(), nullptr);
        vis.body_cloud_textures.assign(sim.universe.bodies.size(), nullptr);
        for (std::size_t i = 0; i < sim.universe.bodies.size(); ++i)
        {
            const kosmos::Body &b = sim.universe.bodies[i];
            if (b.cloud_cover <= 0.0 || static_cast<int>(i) == earth_id)
                continue;
            vis.body_cloud_textures[i] = baked_clouds(sim.root(), b, options.max_texture);
            if (!vis.body_cloud_textures[i])
                vis.body_cloud_textures[i] = fx::make_cloud_texture(2048, 1024, b.cloud_cover, 101 + static_cast<std::uint32_t>(i), 7.0);
            vis.body_cloud_meshes[i] = sl::create_sphere_mesh(static_cast<float>(b.radius + b.cloud_height), 128, 64, {255, 255, 255, 255});
        }
        loading.note("Building the cloud layers");
        if (!earth_body->real_world && earth_body->cloud_cover > 0.0)
        {
            home_cloud_texture = baked_clouds(sim.root(), *earth_body, options.max_texture);
            if (!home_cloud_texture)
                home_cloud_texture = fx::make_cloud_texture(2048, 1024, earth_body->cloud_cover, 101 + static_cast<std::uint32_t>(earth_id), 7.0);
            home_cloud_height = earth_body->cloud_height;
        }
    }

    bool App::enter_system()
    {
        load_planet_images();
#ifndef KOSMOS_EXPERIMENTAL_ORBITAL_RELIEF
        load_earth_tiles();
#endif
        load_earth_cloud_tiles();

        // The home planet is a streaming procedural terrain whose heights the launch gear also collides with.
        earth_id = sim.universe.home_id();
        if (earth_id < 0)
        {
            std::fprintf(stderr, "kosmos: system has no home planet with terrain\n");
            return false;
        }
        place_worlds();
        body_terrains.clear();
        terrain_set.items.clear();
        if (!build_terrains())
            return false;
        terrain = body_terrains[0]->terrain.get();
#ifdef KOSMOS_EXPERIMENTAL_ORBITAL_RELIEF
        load_earth_tiles();
#endif
        // Each terrain is built in the body's frame as it stands now; from here it turns with the planet.
        for (const std::unique_ptr<BodyTerrain> &bt : body_terrains)
            if (sl::FlightBody *fb = sim.universe.world.body(bt->body))
                fb->spin_phase = -fb->spin_rate * sim.universe.world.time();
        for (const std::unique_ptr<BodyTerrain> &bt : body_terrains)
        {
            sim.universe.world.body(bt->body)->terrain = bt->terrain.get();
            terrain_set.items.emplace_back(bt->body, bt->terrain.get());
        }
        build_cloud_layers();
        const float shell_radius = static_cast<float>(earth_body->radius + home_cloud_height);
        constexpr int shell_segments = 256, shell_rings = 128;
        cloud_shell = sl::create_sphere_mesh(shell_radius, shell_segments, shell_rings, {255, 255, 255, 255});
        // The same vertices again on the CPU, so the shell can be faded toward its limb each frame.
        cloud_shell_vertices.clear();
        for (int ring = 0; ring <= shell_rings; ++ring)
            for (int segment = 0; segment <= shell_segments; ++segment)
            {
                const float phi = 3.14159265f * static_cast<float>(ring) / shell_rings, theta = 6.2831853f * static_cast<float>(segment) / shell_segments;
                const glm::vec3 normal{std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta)};
                sl::Vertex3D v;
                for (int k = 0; k < 3; ++k)
                {
                    v.position[k] = normal[k] * shell_radius;
                    v.normal[k] = normal[k];
                }
                v.uv[0] = static_cast<float>(segment) / shell_segments;
                v.uv[1] = static_cast<float>(ring) / shell_rings;
                cloud_shell_vertices.push_back(v);
            }
        return true;
    }

    // The star system: terrain, bodies, bases, and the functions that enter and leave a system.
    int App::init_systems()
    {
        generated_texture_width = sim.props.get_int("/sim/settings/world-texture");
        if (!enter_system())
        {
            sl::shutdown();
            return 1;
        }
    return keep_going;
    }

    // The ship to fly and the HUD.
    int App::init_ship()
    {
        loading.note("Assembling the ship");
        ship = start_scenario(sim, options, terrain_set);
        if (!ship)
        {
            if (!options.scenario.empty() && sim.find_scenario(options.scenario))
                std::fprintf(stderr, "kosmos: unable to start scenario \"%s\"\n", options.scenario.c_str());
            else
                std::fprintf(stderr, "No Kestrel addon found under %s\n", options.root.c_str());
            sl::shutdown();
            return 1;
        }
        if (!options.scenario.empty())
            if (const kosmos::Scenario *sc = sim.find_scenario(options.scenario))
                active_scenario_path = sc->path;

        if (!options.hud.empty())
        {
            sl::HudMode start_mode = sl::HudMode::off;
            if (sl::hud_parse_mode(options.hud, start_mode))
                hud.set_mode(start_mode);
            else
                std::fprintf(stderr, "unknown --hud mode '%s' (off, orbit, surface, docking)\n", options.hud.c_str());
        }
        sound_on = true;
        first_frame = true;
        show_orbit = true;
        show_info = options.show_info && !options.hide_info;
        running = true;
        shot_saved = false;
        map_mode = options.map;
        galaxy_mode = options.galaxy_view;
        quantum.jump_to = -1;
        quantum.quantum_window = options.quantum_window;
        quantum.quantum_selected = -1;
        gview.refresh(sim.galaxy, sim.system_index());
        cockpit_view = options.cockpit;
        mouse_captured = false;
        pointer_free = false; // ` toggles between mouselook and a free pointer for the menus when on foot
        if (!options.target.empty())
            sim.props.set_string("/sim/target", options.target);
        apply_sets(sim.props, options.sets);
        mfd_display = options.mfd_display_explicit ? options.mfd_display : (ship->spec().cockpit.external_mfds ? 1 : 0);
    return keep_going;
    }
} // namespace kosmos::app
