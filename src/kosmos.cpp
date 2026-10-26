#include "sl.h"
#include "props3d.h"
#include "flight.h"
#include "example_fx.h"
#include "sim.h"
#include "lua_console.h"
#include "menu.h"
#include "netplay.h"
#include "planet_data.h"
#include "blender_edit.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <thread>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif
#include <vector>

// Kosmos: a space flight simulator. The first milestone flies a two-stage launcher from the pad to Earth orbit.
//
//   B: wheel brakes   F: cycle flaps (aircraft)
//   Z/X: full / cut throttle   Shift/Ctrl: raise / lower throttle   Space: stage
//   W/S: pitch   A/D: yaw   Q/E: roll   Arrows: RCS translate   T: cycle attitude hold   P: ascent autopilot
//   , / . : time warp   [ / ] : zoom   Right-drag: look   O: orbit line   M: sound   R: reset   Esc: exit

#ifndef KOSMOS_ROOT
#define KOSMOS_ROOT "."
#endif

namespace
{
    using kosmos::Controls;
    using kosmos::Ship;
    using kosmos::Sim;

    constexpr double km = 1000.0;
    constexpr double pi = 3.14159265358979323846;
    constexpr int screen_w = 1280;
    constexpr int screen_h = 720;
    constexpr double cloud_height = 60.0e3;
    const char *const sas_modes[] = {"stability", "prograde", "retrograde", "radial-out", "radial-in", "normal", "antinormal", "off"};
    constexpr int sas_mode_count = sizeof(sas_modes) / sizeof(sas_modes[0]);

    struct Options
    {
        std::string root = KOSMOS_ROOT;
        double selftest = 0.0;
        int max_texture = 16384;
        double selftest_warp = 0.0;
        double selftest_throttle = -1.0;
        /** Hold this throttle while taking a --shot (for a picture of the exhaust); negative leaves the controls alone. */
        double shot_throttle = -1.0;
        /** Ignore the tiled Earth imagery and use the single image (--no-tiles). */
        bool no_tiles = false;
        bool camera_window = false;
        bool settings_window = false;
        bool property_window = false;
        bool lua_window = false;
        bool menu_visible = false, scenario_editor = false, base_editor = false;
        std::string view_base; // "base:index" to start looking through a base camera
        std::string shot;
        double shot_at = 10.0;
        double shot_time = 0.0;
        double earth_turn = 0.0;
        bool autopilot = false;
        bool orbit_start = false;
        bool hide_info = false;
        bool no_relief = false;
        std::string blender;
        bool part_window = false;
        // Multiplayer
        bool net_host = false, net_window = false;
        int net_port = kosmos::NetPlay::default_port;
        std::string net_join, net_name, net_advertise, net_irc = "salyut.ddns.net", net_channel = "#kosmos";
        bool map = false;
        bool cockpit = false;
        bool cockpit3d = false;
        int mfd_display = 0; // 0 off, 1 panels, 2 heads-up
        std::string mfd_left = "orbit", mfd_right = "surface";
        int ship_index = 0;
        std::string ship_name = "Kestrel";
        std::string scenario;
        std::string target;
        bool list_scenarios = false;
        double map_scale = 0.0;
        std::string map_focus;
        std::string orbit_body;
        double cam_pitch = 0.25;
        double cam_yaw = 0.7;
        double cam_distance = 0.0;
        bool cam_sunward = false;
        /** Put the camera on the far side of the ship from the star, turned aside, so the star is in view (for pictures). */
        bool cam_antisun = false;
        double cam_fov = 50.0;
        double orbit_altitude = 0.0;
        bool orbit_half = false;
        float ambient = 0.3f;
        bool sunlit = false;
        double orbit_phase = 0.0;
        int system = 0;
        std::string system_name;
        bool list_stars = false;
        bool check_data = false;
        std::string export_base, export_file;
        bool galaxy_view = false;
        bool quantum_window = false;
        int test_jump = -1; // --test-jump N: quantum-jump to star N a second into the run (for testing)
    };

    // ---------------------------------------------------------------------------------------------------------
    // Launch site and scenario
    // ---------------------------------------------------------------------------------------------------------

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
        const double th = universe.rotation_angle(body);
        return {bx * std::cos(th) + bz * std::sin(th), by, -bx * std::sin(th) + bz * std::cos(th)};
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

    // The terrains of the bodies that have one: the home planet and any with Orbiter elevation data.
    struct TerrainSet
    {
        std::vector<std::pair<int, const sl::Terrain *>> items;
        const sl::Terrain *find(int body) const
        {
            for (const auto &item : items)
                if (item.first == body)
                    return item.second;
            return nullptr;
        }
    };

    // Returns the ship to fly, or nullptr (with a message on stderr) if the scenario cannot be built.
    Ship *apply_scenario(Sim &sim, const kosmos::Scenario &sc, const TerrainSet &terrains)
    {
        sim.clear_ships();
        const int home = sim.universe.home_id();
        for (const kosmos::ScenarioShip &entry : sc.ships)
        {
            const kosmos::ShipSpec *spec = sim.find_ship_spec(entry.ship);
            const int body = sim.universe.find(entry.body);
            if (!spec || body < 0)
            {
                std::fprintf(stderr, "kosmos: scenario %s: unknown %s \"%s\"\n", sc.id.c_str(), spec ? "body" : "ship", spec ? entry.body.c_str() : entry.ship.c_str());
                continue;
            }
            const sl::Terrain *terrain = terrains.find(body);
            Ship *ship = sim.spawn(*spec);
            if (!entry.label.empty())
                ship->name = entry.label;
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
                        kosmos::base_pad(*base, radius, dir, heading);
                    height = base->height;
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
        if (sim.ship_count() == 0)
            return nullptr;
        sim.activate(std::min(sc.active, sim.ship_count() - 1));
        sim.props.set_string("/sim/target", sc.target);
        for (int i = 0; i < kosmos::NavSystem::receivers; ++i)
            sim.props.set_double("/nav/nav[" + std::to_string(i) + "]/frequency", i < static_cast<int>(sc.nav.size()) ? sc.nav[static_cast<std::size_t>(i)] : 0.0);
        sim.props.set_double("/controls/throttle", 0.0);
        sim.props.set_string("/controls/sas", "stability");
        sim.props.set_bool("/autopilot/enabled", false);
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
        const double angle = std::atan2(r.x, r.z) - std::atan2(s.x, s.z);
        const sl::dquat spin = glm::angleAxis(angle, sl::dvec3(0.0, 1.0, 0.0));
        for (int i = 0; i < sim.ship_count(); ++i)
        {
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
                height = home_base->height;
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
        sim.props.set_string("/controls/sas", "stability");
        sim.props.set_bool("/autopilot/enabled", options.autopilot);
        sim.props.set_int("/sim/warp-index", 0);
        return sim.active_ship();
    }

    int run_selftest(Sim &sim, const Options &options)
    {
        Ship *ship = start_scenario(sim, options, TerrainSet());
        if (!ship)
        {
            std::fprintf(stderr, "No Kestrel addon found under %s\n", sim.root().c_str());
            return 1;
        }
        const double dt = 0.05;
        double next_report = 0.0;
        for (double t = 0.0; t < options.selftest; t += dt)
        {
            if (options.selftest_warp > 0.0 && sim.props.get_string("/autopilot/status") == "Coasting to apoapsis")
                sim.props.set_int("/sim/warp-index", static_cast<int>(options.selftest_warp));
            if (ship->spec().aircraft() && options.selftest_throttle >= 0.0)
            {
                // A scripted flight: rotate at 40 m/s, then hold a gentle climb.
                const kosmos::Telemetry &now = ship->telemetry();
                sim.props.set_double("/controls/throttle", options.selftest_throttle);
                sim.props.set_double("/controls/pitch", now.surface_speed > 40.0 && now.pitch < 8.0 ? 0.4 : (now.pitch > 12.0 ? -0.2 : 0.0));
            }
            sim.step(dt);
            const kosmos::Telemetry &tm = ship->telemetry();
            const double report_every = ship->spec().aircraft() ? 2.0 : 10.0;
            if (sim.universe.time() >= next_report)
            {
                next_report = sim.universe.time() + report_every;
                if (ship->spec().aircraft())
                    std::printf("t=%5.0f ias=%5.1f alt=%6.1f vs=%5.1f pitch=%5.1f aoa=%5.1f slip=%5.1f hdg=%5.1f landed=%d fuel=%4.0f%s\n", sim.universe.time(), tm.surface_speed,
                                tm.altitude, tm.vertical_speed, tm.pitch, tm.alpha, tm.sideslip, tm.heading, tm.landed ? 1 : 0, tm.stage_fuel, ship->crashed() ? " CRASHED" : "");
                if (ship->spec().aircraft() && sim.props.get_bool("/nav/nav[0]/valid"))
                    std::printf("   nav0: loc %.2f gs %.2f dist %.0f\n", sim.props.get_double("/nav/nav[0]/ils/localizer"), sim.props.get_double("/nav/nav[0]/ils/glideslope"), sim.props.get_double("/nav/nav[0]/distance"));
                if (!ship->spec().aircraft())
                std::printf("t=%6.0f %-24s alt=%8.1f km speed=%7.1f vs=%7.1f ap=%8.1f pe=%8.1f pitch=%5.1f stage=%d fuel=%8.0f m=%7.0f warp=%g%s\n",
                            sim.universe.time(), sim.props.get_string("/autopilot/status").c_str(), tm.altitude / km, tm.surface_speed,
                            tm.vertical_speed, tm.apoapsis / km, tm.periapsis / km, tm.pitch, ship->active_stage(), tm.stage_fuel,
                            tm.mass, sim.current_warp(), ship->crashed() ? " CRASHED" : "");
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

    // ---------------------------------------------------------------------------------------------------------
    // Visuals
    // ---------------------------------------------------------------------------------------------------------

    // A soft star disc: a bright core, a warm halo and a faint wide glow, as alpha for an additive billboard.
    sl::Bitmap *make_glow_texture(int size = 256)
    {
        sl::Bitmap *bitmap = sl::create_bitmap(size, size);
        if (!bitmap)
            return nullptr;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                const double dx = (x + 0.5) / size * 2.0 - 1.0, dy = (y + 0.5) / size * 2.0 - 1.0;
                const double r = std::sqrt(dx * dx + dy * dy);
                const double core = 1.0 - fx::smoothstep(0.05, 0.09, r);
                const double a = std::clamp(core + 0.8 * std::exp(-r * 6.0) + 0.4 * std::exp(-r * 2.6), 0.0, 1.0) * (1.0 - fx::smoothstep(0.8, 1.0, r));
                sl::putpixel(bitmap, x, y, {255, static_cast<std::uint8_t>(215 + 40 * core), static_cast<std::uint8_t>(110 + 140 * core), static_cast<std::uint8_t>(a * 255.0)});
            }
        bitmap->smooth = true;
        return bitmap;
    }

    // A star's photosphere as an equirectangular map, grey so the star's colour can tint it. Layer 0 is the opaque surface:
    // domain-warped granulation, broad swirls and dark spots. Layer 1 is white filaments in the alpha channel, drawn
    // additively on a second, counter-rotating shell so the surface seems to churn.
    sl::Bitmap *make_star_surface(int layer, std::uint32_t seed, int width = 512)
    {
        const int height = width / 2;
        sl::Bitmap *bitmap = sl::create_bitmap(width, height);
        if (!bitmap)
            return nullptr;
        using fx::detail::sphere_fbm;
        for (int y = 0; y < height; ++y)
        {
            const double lat = 1.5707963 - (y + 0.5) / height * 3.1415927;
            for (int x = 0; x < width; ++x)
            {
                const double lon = (x + 0.5) / width * 6.2831853;
                const sl::dvec3 d(std::cos(lat) * std::cos(lon), std::sin(lat), std::cos(lat) * std::sin(lon));
                const sl::dvec3 warp(sphere_fbm(d, 1.7, 3, seed + 1) - 0.5, sphere_fbm(d, 1.7, 3, seed + 2) - 0.5, sphere_fbm(d, 1.7, 3, seed + 3) - 0.5);
                // A second warp on the first gives the curling, swirled look rather than plain clouds.
                const sl::dvec3 p1 = d + 1.1 * warp;
                const sl::dvec3 warp2(sphere_fbm(p1, 3.0, 2, seed + 4) - 0.5, sphere_fbm(p1, 3.0, 2, seed + 5) - 0.5, sphere_fbm(p1, 3.0, 2, seed + 6) - 0.5);
                const sl::dvec3 p = d + 1.1 * warp + 0.7 * warp2;
                if (layer == 0)
                {
                    const double cells = sphere_fbm(p, 6.0, 3, seed + 7);
                    const double swirl = sphere_fbm(p, 3.2, 3, seed + 8);
                    const double spots = fx::smoothstep(0.60, 0.70, sphere_fbm(d, 2.6, 3, seed + 9));
                    double v = 0.82 + 0.9 * (cells - 0.5) + 0.7 * (swirl - 0.5);
                    v *= 1.0 - 0.6 * spots;
                    // Lift the floor so the granulation stays bright; the additive pass in draw_photosphere supplies the HDR-style glow.
                    v = 0.6 + 0.4 * std::clamp(v, 0.0, 1.0);
                    const auto g = static_cast<std::uint8_t>(255.0 * v);
                    sl::putpixel(bitmap, x, y, {g, g, g, 255});
                }
                else
                {
                    const double ridge = 1.0 - std::fabs(2.0 * sphere_fbm(p, 2.5, 3, seed + 11) - 1.0);
                    const double f = std::pow(ridge, 4.0) * (0.5 + 0.8 * sphere_fbm(d, 2.0, 2, seed + 12));
                    sl::putpixel(bitmap, x, y, {255, 255, 255, static_cast<std::uint8_t>(255.0 * std::clamp(f, 0.0, 1.0))});
                }
            }
        }
        bitmap->smooth = true;
        return bitmap;
    }

    // The corona: streamers that fall off with distance from the limb, as alpha. The disc edge sits at 1/scale of the half-width.
    sl::Bitmap *make_star_corona(std::uint32_t seed, double scale, int size = 256)
    {
        sl::Bitmap *bitmap = sl::create_bitmap(size, size);
        if (!bitmap)
            return nullptr;
        using fx::detail::value_noise3;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                const double dx = (x + 0.5) / size * 2.0 - 1.0, dy = (y + 0.5) / size * 2.0 - 1.0;
                const double r = std::sqrt(dx * dx + dy * dy) * scale; // in star radii
                const double theta = std::atan2(dy, dx);
                const double streak = 0.35 + 1.1 * (0.6 * value_noise3(std::cos(theta) * 2.6, std::sin(theta) * 2.6, r * 0.18, seed) +
                                                    0.4 * value_noise3(std::cos(theta) * 7.0, std::sin(theta) * 7.0, r * 0.3, seed + 3));
                const double fall = r < 1.0 ? 1.0 : std::exp(-(r - 1.0) * 1.15) * 0.8 + 0.2 * std::exp(-(r - 1.0) * 0.3);
                const double edge = 1.0 - fx::smoothstep(0.82, 1.0, r / scale);
                const double a = std::clamp(fall * streak * edge * 0.9, 0.0, 1.0);
                sl::putpixel(bitmap, x, y, {255, 255, 255, static_cast<std::uint8_t>(a * 255.0)});
            }
        bitmap->smooth = true;
        return bitmap;
    }

    // Diffraction spikes: a bright centre and thin rays along the axes, as alpha.
    sl::Bitmap *make_star_rays(int size = 256)
    {
        sl::Bitmap *bitmap = sl::create_bitmap(size, size);
        if (!bitmap)
            return nullptr;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                const double dx = (x + 0.5) / size * 2.0 - 1.0, dy = (y + 0.5) / size * 2.0 - 1.0;
                const double r = std::sqrt(dx * dx + dy * dy);
                const double theta = std::atan2(dy, dx);
                const double spike = std::pow(std::fabs(std::cos(2.0 * theta)), 90.0);
                const double a = (0.9 * spike * std::exp(-r * 4.2) + 0.5 * std::exp(-r * 14.0)) * (1.0 - fx::smoothstep(0.85, 1.0, r));
                sl::putpixel(bitmap, x, y, {255, 255, 255, static_cast<std::uint8_t>(std::clamp(a, 0.0, 1.0) * 255.0)});
            }
        bitmap->smooth = true;
        return bitmap;
    }

    // Limb darkening: black at the edge of the disc, clear at the centre, nothing beyond. Alpha-blended over the photosphere.
    sl::Bitmap *make_star_limb(int size = 256)
    {
        sl::Bitmap *bitmap = sl::create_bitmap(size, size);
        if (!bitmap)
            return nullptr;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                const double dx = (x + 0.5) / size * 2.0 - 1.0, dy = (y + 0.5) / size * 2.0 - 1.0;
                const double r = std::sqrt(dx * dx + dy * dy);
                const double a = r < 1.0 ? 0.8 * std::pow(r, 4.0) : 0.0;
                sl::putpixel(bitmap, x, y, {0, 0, 0, static_cast<std::uint8_t>(a * 255.0)});
            }
        bitmap->smooth = true;
        return bitmap;
    }

    sl::Bitmap *make_puff_texture(int size = 64)
    {
        sl::Bitmap *bitmap = sl::create_bitmap(size, size);
        if (!bitmap)
            return nullptr;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                const double dx = (x + 0.5) / size * 2.0 - 1.0, dy = (y + 0.5) / size * 2.0 - 1.0;
                const double r = std::sqrt(dx * dx + dy * dy);
                const double a = std::pow(std::max(0.0, 1.0 - r), 2.0);
                sl::putpixel(bitmap, x, y, {255, 255, 255, static_cast<std::uint8_t>(a * 255.0)});
            }
        bitmap->smooth = true;
        return bitmap;
    }

    // Stars, a Milky Way band and a few nebulae as soft sprites on a unit sphere about the camera.
    sl::Mesh *make_starfield()
    {
        std::mt19937 rng(20240611);
        auto uniform = [&](double lo, double hi) { return std::uniform_real_distribution<double>(lo, hi)(rng); };
        std::vector<sl::Vertex3D> vertices;
        std::vector<std::uint32_t> indices;
        auto add = [&](const sl::dvec3 &dir, double radius, const glm::vec3 &colour, float alpha)
        {
            const sl::dvec3 helper = std::fabs(dir.y) < 0.9 ? sl::dvec3(0, 1, 0) : sl::dvec3(1, 0, 0);
            const sl::dvec3 u = glm::normalize(glm::cross(dir, helper)), v = glm::cross(dir, u);
            const float corner[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
            const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
            for (int i = 0; i < 4; ++i)
            {
                sl::Vertex3D vertex;
                const sl::dvec3 p = dir + u * (corner[i][0] * radius) + v * (corner[i][1] * radius);
                for (int c = 0; c < 3; ++c)
                {
                    vertex.position[c] = static_cast<float>(p[c]);
                    vertex.colour[c] = colour[c];
                }
                vertex.colour[3] = alpha;
                vertex.uv[0] = 0.5f + 0.5f * corner[i][0];
                vertex.uv[1] = 0.5f - 0.5f * corner[i][1];
                vertices.push_back(vertex);
            }
            for (const std::uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u})
                indices.push_back(base + k);
        };
        auto random_dir = [&]()
        {
            const double z = uniform(-1.0, 1.0), a = uniform(0.0, 2.0 * pi), r = std::sqrt(1.0 - z * z);
            return sl::dvec3(r * std::cos(a), z, r * std::sin(a));
        };
        for (int i = 0; i < 4500; ++i)
        {
            const double brightness = std::pow(uniform(0.0, 1.0), 3.0), temperature = uniform(0.0, 1.0);
            const glm::vec3 colour = temperature < 0.2 ? glm::vec3(1.0f, 0.75f, 0.55f) : temperature > 0.75 ? glm::vec3(0.65f, 0.78f, 1.0f) : glm::vec3(1.0f, 0.97f, 0.9f);
            add(random_dir(), 0.0008 + 0.0017 * brightness, colour, static_cast<float>(0.3 + 0.4 * brightness));
        }
        return sl::create_mesh(vertices, indices);
    }

    // The Milky Way band, dust lanes and a few faint nebulae as an equirectangular map for an inside-out sphere. Drawn additively, so
    // every channel is capped low: it should be a dim backdrop that never competes with the stars or the Sun.
    sl::Bitmap *make_sky_texture(int width = 1024)
    {
        const int height = width / 2;
        sl::Bitmap *bitmap = sl::create_bitmap(width, height);
        if (!bitmap)
            return nullptr;
        using fx::detail::sphere_fbm;
        const sl::dvec3 n = glm::normalize(sl::dvec3(0.35, 0.8, 0.5));
        const sl::dvec3 bu = glm::normalize(glm::cross(n, sl::dvec3(0, 0, 1))), bv = glm::cross(n, bu);
        struct Nebula
        {
            sl::dvec3 centre;
            glm::vec3 colour;
            double size;
            std::uint32_t seed;
        };
        const Nebula nebulae[] = {{glm::normalize(sl::dvec3(-0.7, 0.2, 0.6)), {0.85f, 0.3f, 0.45f}, 0.34, 201},
                                  {glm::normalize(sl::dvec3(0.5, -0.3, -0.8)), {0.3f, 0.45f, 0.9f}, 0.4, 202},
                                  {glm::normalize(sl::dvec3(0.2, 0.6, -0.7)), {0.5f, 0.3f, 0.8f}, 0.3, 203},
                                  {glm::normalize(sl::dvec3(0.9, 0.1, 0.3)), {0.25f, 0.7f, 0.65f}, 0.36, 204}};
        for (int y = 0; y < height; ++y)
        {
            const double lat = 1.5707963 - (y + 0.5) / height * 3.1415927;
            for (int x = 0; x < width; ++x)
            {
                const double lon = (x + 0.5) / width * 6.2831853;
                const sl::dvec3 d(std::cos(lat) * std::cos(lon), std::sin(lat), std::cos(lat) * std::sin(lon));
                const double b = glm::dot(d, n);
                const double a = std::atan2(glm::dot(d, bv), glm::dot(d, bu));
                const double core = 0.5 + 0.5 * std::cos(a - 1.0);
                const double band = std::exp(-(b * b) / (0.16 * 0.16)) * (0.3 + 0.7 * core) + 0.25 * std::exp(-(b * b) / (0.45 * 0.45));
                const double clumps = sphere_fbm(d, 6.0, 5, 101);
                const double lanes = fx::smoothstep(0.52, 0.68, sphere_fbm(d, 9.0, 4, 102));
                const double lum = band * (0.35 + 0.9 * clumps) * (1.0 - 0.65 * lanes);
                const glm::vec3 tone = glm::mix(glm::vec3(0.55f, 0.62f, 0.9f), glm::vec3(0.95f, 0.78f, 0.6f), static_cast<float>(core));
                glm::vec3 colour = tone * static_cast<float>(lum * 0.16);
                for (const Nebula &neb : nebulae)
                {
                    const double angle = std::acos(std::clamp(glm::dot(d, neb.centre), -1.0, 1.0));
                    const double cloud = fx::smoothstep(0.35, 0.75, sphere_fbm(d, 4.0, 5, neb.seed));
                    colour += neb.colour * static_cast<float>(std::exp(-(angle * angle) / (neb.size * neb.size)) * cloud * 0.09);
                }
                colour = glm::min(colour, glm::vec3(0.22f));
                sl::putpixel(bitmap, x, y, {static_cast<std::uint8_t>(colour.r * 255.0f), static_cast<std::uint8_t>(colour.g * 255.0f), static_cast<std::uint8_t>(colour.b * 255.0f), 255});
            }
        }
        bitmap->smooth = true;
        return bitmap;
    }

    // A camera-facing disc for an atmosphere limb: rings by closest-approach altitude, rebuilt each frame with update_limb_mesh().
    constexpr int limb_rings = 64, limb_segments = 160;
    sl::Mesh *make_limb_mesh()
    {
        std::vector<sl::Vertex3D> vertices((limb_rings + 1) * (limb_segments + 1));
        std::vector<std::uint32_t> indices;
        for (int i = 0; i < limb_rings; ++i)
            for (int j = 0; j < limb_segments; ++j)
            {
                const std::uint32_t a = i * (limb_segments + 1) + j, b = a + limb_segments + 1;
                for (const std::uint32_t k : {a, b, a + 1, a + 1, b, b + 1})
                    indices.push_back(k);
            }
        return sl::create_mesh(vertices, indices);
    }

    // Fills the limb mesh. rel is the planet centre relative to the eye. Each ring is a ray grazing the planet at a given
    // altitude; the glow falls off exponentially with that altitude, and is strongest on the sunlit side and reddened near
    // the terminator.
    void update_limb_mesh(sl::Mesh *mesh, const glm::vec3 &rel, double radius, double thickness, double scale_height, const glm::vec3 &to_sun, const glm::vec3 &colour)
    {
        const double d = glm::length(glm::dvec3(rel));
        const glm::vec3 view = rel / static_cast<float>(d);
        const glm::vec3 e1 = glm::normalize(glm::cross(std::fabs(view.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0), view));
        const glm::vec3 e2 = glm::cross(view, e1);
        const double height_scale = std::max(scale_height * 2.5, thickness * 0.12);
        std::vector<sl::Vertex3D> vertices;
        vertices.reserve((limb_rings + 1) * (limb_segments + 1));
        for (int i = 0; i <= limb_rings; ++i)
        {
            const double t = static_cast<double>(i) / limb_rings;
            const double h = thickness * t * t;
            // Start a little below the surface so the glow fills the gaps along the planet's tessellated edge.
            const double p = std::min(radius * 0.996 + h, d * 0.999);
            const double r = p * d / std::sqrt(std::max(d * d - p * p, 1.0));
            const double density = std::exp(-h / height_scale) * (1.0 - fx::smoothstep(0.55, 1.0, t));
            for (int j = 0; j <= limb_segments; ++j)
            {
                const double phi = 2.0 * pi * j / limb_segments;
                const glm::vec3 u = static_cast<float>(std::cos(phi)) * e1 + static_cast<float>(std::sin(phi)) * e2;
                const double sun = glm::dot(u, to_sun);
                const double lit = fx::smoothstep(-0.25, 0.55, sun);
                const double dusk = std::exp(-(sun - 0.05) * (sun - 0.05) / 0.03);
                const glm::vec3 warm(1.0f, 0.55f, 0.25f);
                const glm::vec3 c = glm::mix(colour, warm, static_cast<float>(0.6 * dusk * (1.0 - t)));
                sl::Vertex3D v;
                const glm::vec3 pos = rel + static_cast<float>(r) * u;
                for (int k = 0; k < 3; ++k)
                {
                    v.position[k] = pos[k];
                    v.normal[k] = view[k];
                }
                for (int k = 0; k < 3; ++k)
                    v.colour[k] = c[k];
                v.colour[3] = static_cast<float>(0.5 * density * lit);
                vertices.push_back(v);
            }
        }
        sl::update_mesh_vertices(mesh, vertices);
    }

    // A flat annulus in the XZ plane with outer radius 1: banded opacity, a gap and soft edges.
    sl::Mesh *make_ring_mesh(double inner_ratio, const float colour[3])
    {
        const int bands = 96, segments = 128;
        auto hash = [](double x) { return 0.5 + 0.5 * std::sin(x * 91.7 + std::sin(x * 37.1) * 3.0); };
        std::vector<sl::Vertex3D> vertices;
        std::vector<std::uint32_t> indices;
        for (int i = 0; i <= bands; ++i)
        {
            const double t = static_cast<double>(i) / bands, radius = inner_ratio + (1.0 - inner_ratio) * t;
            double alpha = 0.35 + 0.55 * hash(t * 7.0) * hash(t * 23.0 + 1.0);
            alpha *= fx::smoothstep(0.0, 0.06, t) * (1.0 - fx::smoothstep(0.94, 1.0, t));
            alpha *= 1.0 - 0.92 * (fx::smoothstep(0.60, 0.62, t) - fx::smoothstep(0.66, 0.68, t));
            const float shade = static_cast<float>(0.75 + 0.25 * hash(t * 41.0));
            for (int j = 0; j <= segments; ++j)
            {
                const double a = 2.0 * pi * j / segments;
                sl::Vertex3D v;
                v.position[0] = static_cast<float>(radius * std::cos(a));
                v.position[1] = 0.0f;
                v.position[2] = static_cast<float>(radius * std::sin(a));
                for (int c = 0; c < 3; ++c)
                    v.colour[c] = colour[c] * shade;
                v.colour[3] = static_cast<float>(alpha);
                v.uv[0] = static_cast<float>(t);
                v.uv[1] = static_cast<float>(j) / segments;
                vertices.push_back(v);
            }
        }
        for (int i = 0; i < bands; ++i)
            for (int j = 0; j < segments; ++j)
            {
                const std::uint32_t a = i * (segments + 1) + j, b = a + segments + 1;
                for (const std::uint32_t k : {a, b, a + 1, a + 1, b, b + 1})
                    indices.push_back(k);
            }
        return sl::create_mesh(vertices, indices);
    }

    // A tapering cone along +Z, opaque at the base and fading to nothing at the tip, for a comet's tail.
    sl::Mesh *make_tail_mesh()
    {
        const int segments = 16;
        std::vector<sl::Vertex3D> vertices;
        std::vector<std::uint32_t> indices;
        for (int ring = 0; ring <= 8; ++ring)
        {
            const double t = ring / 8.0;
            for (int j = 0; j < segments; ++j)
            {
                const double a = 2.0 * pi * j / segments, r = 0.01 + 0.09 * t;
                sl::Vertex3D v;
                v.position[0] = static_cast<float>(r * std::cos(a));
                v.position[1] = static_cast<float>(r * std::sin(a));
                v.position[2] = static_cast<float>(t);
                v.colour[0] = static_cast<float>(0.7 + 0.3 * t);
                v.colour[1] = 0.85f;
                v.colour[2] = static_cast<float>(1.0 - 0.3 * t);
                v.colour[3] = static_cast<float>((1.0 - t) * (1.0 - t));
                vertices.push_back(v);
            }
        }
        for (int ring = 0; ring < 8; ++ring)
            for (int j = 0; j < segments; ++j)
            {
                const std::uint32_t a = ring * segments + j, b = ring * segments + (j + 1) % segments;
                for (const std::uint32_t k : {a, a + segments, b, b, a + segments, b + segments})
                    indices.push_back(k);
            }
        return sl::create_mesh(vertices, indices);
    }

    std::vector<sl::TerrainLayer> planet_layers()
    {
        std::vector<sl::TerrainLayer> layers(4);
        layers[0].colour = {18, 62, 135, 255};
        layers[0].max_height = 1.0;
        layers[0].height_blend = 2.0;
        layers[1].colour = {58, 112, 52, 255};
        layers[1].min_height = 1.0;
        layers[1].max_height = 2600.0;
        layers[1].height_blend = 600.0;
        layers[2].colour = {125, 112, 98, 255};
        layers[2].min_height = 2200.0;
        layers[2].max_height = 4600.0;
        layers[2].height_blend = 500.0;
        layers[3].colour = {245, 246, 250, 255};
        layers[3].min_height = 4300.0;
        layers[3].height_blend = 400.0;
        return layers;
    }

    std::string find_tile_source(const std::string &directory);
    std::string find_surface_image(const std::string &root, const kosmos::Body &body);

    // A body's streaming terrain. Elevation comes from Orbiter when available; colour uses the finer available imagery.
    struct BodyTerrain
    {
        int body = -1;
        std::unique_ptr<sl::Terrain> terrain;
        bool active = false;
        sl::dvec3 eye_rel{0.0};
    };

    std::unique_ptr<BodyTerrain> make_body_terrain(Sim &sim, int id, bool home)
    {
        const kosmos::Body &b = sim.universe.bodies[static_cast<std::size_t>(id)];
        const kosmos::SurfaceFrame frame{sim.universe.rotation_angle(id)};
        const std::string orbiter = sim.root() + "/data/orbiter";
        sl::TerrainConfig config;
        config.shape = sl::TerrainShape::sphere;
        config.radius = b.radius;
        config.finest_cell_size = 4.0;
        config.max_screen_error = 1.0;
        config.error_scale = 0.1;
        if (home)
            config.layers = planet_layers();
        else
        {
            config.layers.resize(1);
            config.layers[0].colour = {static_cast<std::uint8_t>(b.colour[0] * 255.0f), static_cast<std::uint8_t>(b.colour[1] * 255.0f), static_cast<std::uint8_t>(b.colour[2] * 255.0f), 255};
        }

        std::shared_ptr<sl::TerrainSource> source;
        const std::string elevation = kosmos::OrbiterTree::find_layer(orbiter, b.name, "Elev");
        if (!elevation.empty())
        {
            // Earth's elevation file includes the sea floor; the sea itself stays flat at zero.
            auto data = std::make_shared<kosmos::OrbiterElevation>(elevation, frame, b.radius, home ? 0.0 : -12000.0, home ? 9000.0 : 12000.0,
                                                                   home ? 0.0 : std::numeric_limits<double>::quiet_NaN());
            if (data->ok())
                source = data;
            else
                std::fprintf(stderr, "[kosmos] %s elevation: %s\n", b.name.c_str(), data->error().c_str());
        }
        std::shared_ptr<kosmos::OrbiterImagery> orbiter_imagery;
        const std::string surface = kosmos::OrbiterTree::find_layer(orbiter, b.name, "Surf");
        if (source && !surface.empty())
        {
            orbiter_imagery = std::make_shared<kosmos::OrbiterImagery>(surface, frame, b.radius);
            if (!orbiter_imagery->ok())
            {
                std::fprintf(stderr, "[kosmos] %s imagery: %s\n", b.name.c_str(), orbiter_imagery->error().c_str());
                orbiter_imagery.reset();
            }
        }
        std::string lower_name = b.name;
        for (char &c : lower_name)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        std::vector<std::shared_ptr<kosmos::CelestiaImagery>> surface_imagery;
        const std::string celestia_tiles = find_tile_source(sim.root() + "/data/celestia/hires/" + lower_name);
        const std::string surface_image = find_surface_image(sim.root(), b);
        std::vector<std::string> surface_paths;
        if (!celestia_tiles.empty())
            surface_paths.push_back(celestia_tiles);
        else if (!surface_image.empty())
            surface_paths.push_back(surface_image);
        for (const std::string &path : surface_paths)
        {
            auto imagery = std::make_shared<kosmos::CelestiaImagery>(path, frame, b.radius);
            if (!imagery->ok())
            {
                if (!imagery->error().empty())
                    std::fprintf(stderr, "[kosmos] %s surface imagery: %s\n", b.name.c_str(), imagery->error().c_str());
                continue;
            }
            std::fprintf(stderr, "[kosmos] %s terrain imagery candidate: %s\n", b.name.c_str(), path.c_str());
            surface_imagery.push_back(std::move(imagery));
        }
        if (orbiter_imagery || !surface_imagery.empty())
        {
            config.colour_function = [orbiter_imagery, surface_imagery](const sl::TerrainSample &sample, float rgb[3])
            {
                float orbiter_rgb[3], celestia_rgb[3];
                double orbiter_resolution = std::numeric_limits<double>::infinity();
                const bool has_orbiter = orbiter_imagery &&
                                         orbiter_imagery->colour(sample.position, sample.cell_size, orbiter_rgb, orbiter_resolution);
                bool has_celestia = false;
                double celestia_resolution = std::numeric_limits<double>::infinity();
                for (const auto &imagery : surface_imagery)
                {
                    float candidate_rgb[3];
                    double candidate_resolution = std::numeric_limits<double>::infinity();
                    if (imagery->colour(sample.position, sample.cell_size, candidate_rgb, candidate_resolution) &&
                        candidate_resolution < celestia_resolution)
                    {
                        std::copy(std::begin(candidate_rgb), std::end(candidate_rgb), celestia_rgb);
                        celestia_resolution = candidate_resolution;
                        has_celestia = true;
                    }
                }
                if (has_celestia && (!has_orbiter || celestia_resolution < orbiter_resolution))
                    std::copy(std::begin(celestia_rgb), std::end(celestia_rgb), rgb);
                else if (has_orbiter)
                    std::copy(std::begin(orbiter_rgb), std::end(orbiter_rgb), rgb);
                else
                    return false;
                return true;
            };
        }
        if (source)
        {
            std::fprintf(stderr, "[kosmos] %s terrain from Orbiter data\n", b.name.c_str());
        }
        else if (home)
        {
            sl::NoiseParameters noise;
            noise.seed = 11;
            noise.frequency = 1.0 / 3.0e6;
            noise.amplitude = 6000.0;
            noise.base_height = -1200.0;
            noise.floor_height = 0.0;
            source = std::make_shared<sl::NoiseSource>(noise);
        }
        else
            return nullptr;
        {
            // Level the ground under each base at the height of its centre.
            std::vector<kosmos::FlattenSource::Site> sites;
            for (kosmos::Base &base : sim.bases)
            {
                if (sim.universe.find(base.body) != id)
                    continue;
                base.height = source->height_on_sphere(base.direction, b.radius);
                for (const kosmos::FlattenSite &site : kosmos::flatten_sites(base, b.radius))
                    sites.push_back(site);
            }
            if (!sites.empty())
                source = std::make_shared<kosmos::FlattenSource>(source, b.radius, std::move(sites));
        }
        auto result = std::make_unique<BodyTerrain>();
        result->body = id;
        result->terrain = std::make_unique<sl::Terrain>(config, source);
        return result;
    }

    void settle(sl::Terrain &terrain, const sl::TerrainView &view)
    {
        for (int attempt = 0; attempt < 400; ++attempt)
        {
            terrain.update(view);
            if (terrain.stats().pending_chunks == 0)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    bool project(const glm::mat4 &view_proj, const glm::vec3 &p, float &x, float &y)
    {
        const glm::vec4 clip = view_proj * glm::vec4(p, 1.0f);
        if (clip.w <= 0.0f)
            return false;
        x = (clip.x / clip.w * 0.5f + 0.5f) * static_cast<float>(screen_w);
        y = (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) * static_cast<float>(screen_h);
        return true;
    }

    // ---- Settings (F3) and the property browser (F4) ----

    std::string settings_path()
    {
        const char *xdg = std::getenv("XDG_CONFIG_HOME");
        const char *home = std::getenv("HOME");
        const char *appdata = std::getenv("APPDATA");
        std::filesystem::path dir = xdg && *xdg ? std::filesystem::path(xdg) : home && *home ? std::filesystem::path(home) / ".config" : appdata ? std::filesystem::path(appdata) : std::filesystem::path();
        return dir.empty() ? std::string() : (dir / "kosmos" / "settings.json").string();
    }

    // Settings live in the property tree under /sim/settings so scripts and the F4 browser see them too.
    void settings_defaults(sl::PropertyTree &props)
    {
        props.set_bool("/sim/settings/labels", false);
        props.set_bool("/sim/settings/ship-labels", false);
        props.set_bool("/sim/settings/help-text", true);
        props.set_bool("/sim/settings/orbit-line", true);
        props.set_bool("/sim/settings/sound", true);
        props.set_int("/sim/settings/volume", 20);
        props.set_string("/sim/settings/blender", "blender");
    }

    void settings_load(sl::PropertyTree &props)
    {
        const std::string path = settings_path();
        std::ifstream in(path);
        if (path.empty() || !in)
            return;
        nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
        if (!j.is_object())
            return;
        for (const char *key : {"labels", "ship-labels", "help-text", "orbit-line", "sound"})
            if (j.contains(key) && j[key].is_boolean())
                props.set_bool(std::string("/sim/settings/") + key, j[key].get<bool>());
        if (j.contains("volume") && j["volume"].is_number())
            props.set_int("/sim/settings/volume", std::clamp(j["volume"].get<int>(), 0, 100));
        if (j.contains("blender") && j["blender"].is_string() && !j["blender"].get<std::string>().empty())
            props.set_string("/sim/settings/blender", j["blender"].get<std::string>());
    }

    void settings_save(sl::PropertyTree &props)
    {
        const std::string path = settings_path();
        if (path.empty())
            return;
        nlohmann::json j;
        for (const char *key : {"labels", "ship-labels", "help-text", "orbit-line", "sound"})
            j[key] = props.get_bool(std::string("/sim/settings/") + key);
        j["volume"] = props.get_int("/sim/settings/volume");
        j["blender"] = props.get_string("/sim/settings/blender", "blender");
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
        std::ofstream out(path);
        if (out)
            out << j.dump(1) << "\n";
    }

    struct PropertyBrowser
    {
        char filter[96] = "";
        char path[160] = "/sim/settings/labels";
        char value[160] = "";
        std::string status;
        std::map<const sl::PropertyNode *, std::string> edit; // text being typed for a string node
    };

    // One editable row for a node that has a value.
    void property_value_widget(sl::PropertyNode &node, PropertyBrowser &state)
    {
        ImGui::PushID(&node);
        ImGui::SetNextItemWidth(160.0f);
        switch (node.type())
        {
        case sl::PropertyNode::Type::boolean:
        {
            bool v = node.get_bool();
            if (ImGui::Checkbox("##v", &v))
                node.set_bool(v);
            break;
        }
        case sl::PropertyNode::Type::integer:
        {
            int v = node.get_int();
            if (ImGui::InputInt("##v", &v, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue))
                node.set_int(v);
            break;
        }
        case sl::PropertyNode::Type::real:
        {
            double v = node.get_double();
            if (ImGui::InputDouble("##v", &v, 0.0, 0.0, "%.6g", ImGuiInputTextFlags_EnterReturnsTrue))
                node.set_double(v);
            break;
        }
        default:
        {
            std::string &text = state.edit[&node];
            if (!ImGui::IsItemActive() && !ImGui::IsAnyItemActive())
                text = node.get_string();
            char buffer[256];
            std::snprintf(buffer, sizeof(buffer), "%s", text.c_str());
            if (ImGui::InputText("##v", buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue))
                node.set_string(buffer);
            else
                text = buffer;
            break;
        }
        }
        ImGui::PopID();
    }

    void property_tree_rows(sl::PropertyNode &node, PropertyBrowser &state)
    {
        for (sl::PropertyNode *child : node.children())
        {
            std::string label = child->name();
            if (child->index() > 0 || node.child_count(child->name()) > 1)
                label += "[" + std::to_string(child->index()) + "]";
            ImGui::PushID(child);
            if (child->has_children())
            {
                const bool open = ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);
                if (child->has_value())
                {
                    ImGui::SameLine();
                    property_value_widget(*child, state);
                }
                if (open)
                {
                    property_tree_rows(*child, state);
                    ImGui::TreePop();
                }
            }
            else
            {
                ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
                if (ImGui::IsItemClicked())
                {
                    std::snprintf(state.path, sizeof(state.path), "%s", child->path().c_str());
                    std::snprintf(state.value, sizeof(state.value), "%s", child->get_string().c_str());
                }
                ImGui::SameLine(190.0f);
                if (child->has_value())
                    property_value_widget(*child, state);
                else
                    ImGui::TextDisabled("(empty)");
            }
            ImGui::PopID();
        }
    }

    // Flat list of the leaves whose path contains the filter text.
    void property_search(sl::PropertyNode &node, const std::string &needle, PropertyBrowser &state, int &shown)
    {
        for (sl::PropertyNode *child : node.children())
        {
            if (shown >= 200)
                return;
            if (child->has_value())
            {
                const std::string path = child->path();
                std::string lower = path;
                std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (lower.find(needle) != std::string::npos)
                {
                    ++shown;
                    ImGui::Text("%s", path.c_str());
                    ImGui::SameLine(300.0f);
                    property_value_widget(*child, state);
                }
            }
            property_search(*child, needle, state, shown);
        }
    }

    void draw_property_browser(sl::PropertyTree &props, bool &open, PropertyBrowser &state)
    {
        ImGui::SetNextWindowPos(ImVec2(560.0f, 330.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(520.0f, 460.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.9f);
        if (ImGui::Begin("Property tree [F4]", &open))
        {
            ImGui::SetNextItemWidth(-60.0f);
            ImGui::InputTextWithHint("##filter", "filter paths, e.g. controls or altitude", state.filter, sizeof(state.filter));
            ImGui::SameLine();
            if (ImGui::Button("Clear"))
                state.filter[0] = '\0';
            ImGui::BeginChild("tree", ImVec2(0.0f, -64.0f), true);
            if (state.filter[0])
            {
                std::string needle = state.filter;
                std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                int shown = 0;
                property_search(props.root(), needle, state, shown);
                if (shown >= 200)
                    ImGui::TextDisabled("(first 200 matches)");
            }
            else
                property_tree_rows(props.root(), state);
            ImGui::EndChild();
            ImGui::SetNextItemWidth(220.0f);
            ImGui::InputText("path", state.path, sizeof(state.path));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            const bool enter = ImGui::InputText("value", state.value, sizeof(state.value), ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            if (ImGui::Button("Get"))
            {
                if (const sl::PropertyNode *n = props.find(state.path))
                {
                    std::snprintf(state.value, sizeof(state.value), "%s", n->get_string().c_str());
                    state.status.clear();
                }
                else
                    state.status = "no such property";
            }
            ImGui::SameLine();
            if (ImGui::Button("Set") || enter)
            {
                if (state.path[0] == '/')
                {
                    props.node(state.path)->set_from_string(state.value);
                    state.status = "set " + std::string(state.path);
                }
                else
                    state.status = "path must start with /";
            }
            ImGui::TextDisabled("%s", state.status.c_str());
        }
        ImGui::End();
    }

    std::string format_time(double seconds);
    std::string format_distance(double metres);

    /** Orthographic orrery of the system: the ecliptic is the XZ plane, +Y is north. */
    struct MapView
    {
        int focus = -1; // body id, or -1 for the ship
        double metres_per_pixel = 1.0e9;
        float yaw = 0.0f, pitch = 1.05f;

        void project_point(const sl::dvec3 &rel, float &x, float &y) const
        {
            const double cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
            const double a = cy * rel.x - sy * rel.z;
            const double depth = sy * rel.x + cy * rel.z;
            const double up = rel.y * cp - depth * sp;
            x = static_cast<float>(screen_w * 0.5 + a / metres_per_pixel);
            y = static_cast<float>(screen_h * 0.5 - up / metres_per_pixel);
        }

        void draw_path(const std::vector<sl::dvec3> &path, const sl::dvec3 &origin, const sl::Colour &colour) const
        {
            float px = 0.0f, py = 0.0f;
            bool have = false;
            for (const sl::dvec3 &p : path)
            {
                float x, y;
                project_point(p + origin, x, y);
                const bool ok = std::fabs(x) < 4.0e3f && std::fabs(y) < 4.0e3f;
                if (ok && have)
                    sl::line(sl::screen, px, py, x, y, colour);
                have = ok;
                px = x;
                py = y;
            }
        }

        void draw(kosmos::Universe &universe, Ship &ship, const kosmos::Telemetry &tm) const
        {
            sl::clear_to_colour(sl::screen, {2, 3, 8});
            const int ship_id = ship.body_id();
            const sl::dvec3 focus_pos = focus < 0 ? universe.position(ship_id) : universe.position(focus);
            const sl::dvec3 ship_origin = universe.position(ship_id) - focus_pos;

            for (std::size_t i = 1; i < universe.bodies.size(); ++i)
            {
                const kosmos::Body &b = universe.bodies[i];
                if (b.parent < 0 || b.minor)
                    continue;
                const kosmos::Body &parent = universe.bodies[static_cast<std::size_t>(b.parent)];
                sl::OrbitElements el;
                if (!universe.world.orbit_of(static_cast<int>(i), b.parent, el))
                    continue;
                const double size = el.semi_major_axis / metres_per_pixel;
                if (std::fabs(size) < 3.0 || std::fabs(size) > 1.0e5)
                    continue;
                const sl::dvec3 origin = universe.position(b.parent) - focus_pos;
                const sl::Colour colour = b.kind == kosmos::BodyKind::moon ? sl::Colour{70, 80, 100, 255} : sl::Colour{70, 110, 160, 255};
                draw_path(sl::orbit_path(parent.gm, el, 240, 1.0e13), origin, colour);
            }

            sl::OrbitElements ship_el;
            if (tm.soi >= 0 && universe.world.orbit_of(ship_id, tm.soi, ship_el))
            {
                const kosmos::Body &soi_body = universe.bodies[static_cast<std::size_t>(tm.soi)];
                draw_path(sl::orbit_path(soi_body.gm, ship_el, 240, std::min(soi_body.soi, 1.0e13)), universe.position(tm.soi) - focus_pos,
                          {80, 255, 120, 255});
            }

            for (std::size_t i = 0; i < universe.bodies.size(); ++i)
            {
                const kosmos::Body &b = universe.bodies[i];
                if (b.minor && metres_per_pixel > 5.0e8)
                    continue;
                float x, y;
                project_point(universe.position(static_cast<int>(i)) - focus_pos, x, y);
                if (x < -50 || x > screen_w + 50 || y < -50 || y > screen_h + 50)
                    continue;
                const float radius = std::clamp(static_cast<float>(b.radius / metres_per_pixel), 2.5f, 14.0f);
                const sl::Colour colour{static_cast<std::uint8_t>(b.colour[0] * 255.0f), static_cast<std::uint8_t>(b.colour[1] * 255.0f),
                                        static_cast<std::uint8_t>(b.colour[2] * 255.0f), 255};
                sl::circlefill(sl::screen, x, y, b.minor ? 1.5f : radius, colour);
                if (b.minor)
                    continue;
                // Moons are labelled only once their orbit is wide enough not to crowd the parent.
                if (b.kind == kosmos::BodyKind::moon && b.orbit.semi_major_axis / metres_per_pixel < 24.0)
                    continue;
                sl::gprintf(static_cast<int>(x) + static_cast<int>(radius) + 4, static_cast<int>(y) - 6, {200, 220, 255}, "%s", b.name.c_str());
            }

            float x, y;
            project_point(ship_origin, x, y);
            sl::circle(sl::screen, x, y, 6.0f, {80, 255, 120}, 1.0f);
            sl::gprintf(static_cast<int>(x) + 10, static_cast<int>(y) - 6, {80, 255, 120}, "%s", ship.name.c_str());
            if (ship_el.eccentricity < 1.0 && tm.soi >= 0)
            {
                const sl::dvec3 soi_origin = universe.position(tm.soi) - focus_pos;
                const std::vector<sl::dvec3> path = sl::orbit_path(universe.bodies[static_cast<std::size_t>(tm.soi)].gm, ship_el, 720, 1.0e13);
                auto mark = [&](const char *label, bool nearest) {
                    const sl::dvec3 *best = nullptr;
                    for (const sl::dvec3 &p : path)
                        if (!best || (nearest ? glm::length(p) < glm::length(*best) : glm::length(p) > glm::length(*best)))
                            best = &p;
                    if (!best)
                        return;
                    float ax, ay;
                    project_point(*best + soi_origin, ax, ay);
                    sl::circle(sl::screen, ax, ay, 3.0f, {120, 255, 150}, 1.0f);
                    sl::gprintf(static_cast<int>(ax) + 6, static_cast<int>(ay) - 6, {120, 255, 150}, "%s", label);
                };
                mark("Pe", true);
                mark("Ap", false);
            }

            sl::gprintf(16, 130, {240, 240, 240}, "MAP   focus: %s   scale: %s/px   T+%s", focus < 0 ? ship.name.c_str() : universe.bodies[static_cast<std::size_t>(focus)].name.c_str(),
                        format_distance(metres_per_pixel).c_str(), format_time(universe.time()).c_str());
            sl::gprintf_center(screen_h - 22, {200, 210, 220}, "[Tab] Flight view  [F] Focus  [ [ ] ] Zoom  [R-drag] Rotate  [,/.] Warp");
        }
    };

    /** Top-down chart of the galaxy centred on the current star; the destination is picked from the nearest stars. */
    struct GalaxyView
    {
        double ly_per_pixel = 0.16;
        int selected = -1;
        std::vector<int> candidates;

        void refresh(const kosmos::Galaxy &galaxy, int current)
        {
            candidates = galaxy.nearest(current, 16);
            if (selected < 0 && !candidates.empty())
                selected = candidates.front();
        }

        void step_selection(int direction)
        {
            if (candidates.empty())
                return;
            const auto at = std::find(candidates.begin(), candidates.end(), selected);
            const int i = at == candidates.end() ? 0 : static_cast<int>(at - candidates.begin());
            selected = candidates[static_cast<std::size_t>((i + direction + static_cast<int>(candidates.size())) % static_cast<int>(candidates.size()))];
        }

        void draw(const kosmos::Galaxy &galaxy, int current) const
        {
            const kosmos::Star &here = galaxy.star(current);
            sl::clear_to_colour(sl::screen, {1, 2, 6});
            const float cx = screen_w * 0.5f + 110.0f, cy = screen_h * 0.5f;
            auto to_screen = [&](const kosmos::Star &s, float &x, float &y)
            {
                x = cx + static_cast<float>((s.x - here.x) / ly_per_pixel);
                y = cy + static_cast<float>((s.z - here.z) / ly_per_pixel);
            };
            for (const double ring : {10.0, 20.0, 50.0, 100.0, 1000.0, 10000.0})
            {
                const float r = static_cast<float>(ring / ly_per_pixel);
                if (r > 12.0f && r < 1200.0f)
                {
                    sl::circle(sl::screen, cx, cy, r, {25, 40, 60}, 1.0f);
                    sl::gprintf(static_cast<int>(cx + r * 0.7071f) + 3, static_cast<int>(cy - r * 0.7071f), {60, 90, 120}, "%g ly", ring);
                }
            }
            const bool detailed = ly_per_pixel < 0.8;
            for (int i = 0; i < galaxy.size(); ++i)
            {
                const kosmos::Star &s = galaxy.star(i);
                float x, y;
                to_screen(s, x, y);
                if (x < 0 || y < 0 || x >= screen_w || y >= screen_h)
                    continue;
                const auto c = [&](float k) { return static_cast<std::uint8_t>(std::clamp(k * 255.0f, 0.0f, 255.0f)); };
                const float bright = detailed ? 1.0f : 0.55f;
                const sl::Colour colour{c(s.colour[0] * bright), c(s.colour[1] * bright), c(s.colour[2] * bright), 255};
                if (detailed)
                    sl::circlefill(sl::screen, x, y, s.spectral_class == 'M' ? 2.0f : s.spectral_class == 'K' ? 2.5f : 3.0f + (s.mass > 1.5 ? 1.0f : 0.0f), colour);
                else
                    sl::putpixel(sl::screen, static_cast<int>(x), static_cast<int>(y), colour);
            }
            if (detailed)
                for (int id : candidates)
                {
                    float x, y;
                    to_screen(galaxy.star(id), x, y);
                    sl::gprintf(static_cast<int>(x) + 7, static_cast<int>(y) - 4, id == selected ? sl::Colour{255, 240, 120} : sl::Colour{120, 150, 190}, "%s", galaxy.star(id).name.c_str());
                }
            sl::circle(sl::screen, cx, cy, 7.0f, {80, 255, 120}, 1.0f);
            sl::gprintf(static_cast<int>(cx) + 10, static_cast<int>(cy) + 6, {120, 255, 150}, "%s (here)", here.name.c_str());
            if (selected >= 0)
            {
                float x, y;
                to_screen(galaxy.star(selected), x, y);
                sl::line(sl::screen, cx, cy, x, y, {120, 110, 40});
                sl::circle(sl::screen, x, y, 8.0f, {255, 240, 120}, 1.0f);
                const kosmos::Star &s = galaxy.star(selected);
                sl::rectfill(sl::screen, 8, 8, 330, 168, {8, 16, 28});
                sl::gprintf(16, 16, {255, 240, 120}, "%s", s.name.c_str());
                sl::gprintf(16, 38, {200, 210, 220}, "Class %c   %.0f K   %.2f Msun", s.spectral_class, s.temperature, s.mass);
                sl::gprintf(16, 56, {200, 210, 220}, "Luminosity %.3g Lsun   Radius %.2f Rsun", s.luminosity, s.radius);
                sl::gprintf(16, 74, {200, 210, 220}, "Distance %.1f ly", galaxy.distance_ly(current, selected));
                sl::gprintf(16, 92, {200, 210, 220}, "%d planets%s", galaxy.planet_count(selected), s.real ? "   (known star)" : "");
                sl::gprintf(16, 124, {120, 255, 150}, "[Enter] Jump here and arrive in orbit");
                sl::gprintf(16, 142, {160, 170, 180}, "[Left/Right] Choose   [ [ ] ] Zoom");
            }
            sl::gprintf(16, screen_h - 40, {200, 210, 220}, "GALAXY   %d stars   %s per pixel", galaxy.size(), format_distance(ly_per_pixel * 9.4607e15).c_str());
            sl::gprintf_center(screen_h - 22, {200, 210, 220}, "[G] Back to flight  [Left/Right] Choose star  [ [ ] ] Zoom  [Enter] Jump");
        }
    };

    // Ten seconds of wind: band-limited noise whose strength drifts slowly and irregularly, so it does not pulse like an engine.
    sl::Sample *make_breeze_sound()
    {
        const int rate = 22050, fade = rate, frames = rate * 10;
        std::uint32_t state = 0xBEEF1u;
        auto noise = [&]()
        {
            state = state * 1664525u + 1013904223u;
            return static_cast<float>(state >> 8) / 8388608.0f - 1.0f;
        };
        std::vector<float> raw(static_cast<std::size_t>(frames + fade));
        float a = 0.0f, b = 0.0f, c = 0.0f;
        for (float &v : raw)
        {
            const float n = noise();
            a += 0.06f * (n - a);
            b += 0.012f * (n - b);
            c += 0.25f * (n - c);
            v = (a - b) * 2.2f + (c - a) * 0.5f;
        }
        std::vector<std::int16_t> pcm(static_cast<std::size_t>(frames));
        for (int i = 0; i < frames; ++i)
        {
            float v = raw[static_cast<std::size_t>(i)];
            if (i < fade)
            {
                const float k = 1.5707963f * static_cast<float>(i) / fade;
                v = v * std::sin(k) + raw[static_cast<std::size_t>(frames + i)] * std::cos(k);
            }
            const double t = static_cast<double>(i) / frames;
            const float gust = 0.7f + 0.15f * static_cast<float>(std::sin(6.2831853 * t) + 0.8 * std::sin(6.2831853 * 3.0 * t + 1.0) + 0.5 * std::sin(6.2831853 * 7.0 * t + 2.0));
            pcm[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(std::clamp(v * gust * 0.6f, -1.0f, 1.0f) * 30000.0f);
        }
        return sl::create_sample_from_pcm(pcm.data(), pcm.size(), rate, 1);
    }

    struct EngineSound
    {
        sl::Sample *rocket = nullptr, *hiss = nullptr, *wind = nullptr;
        std::uint64_t rocket_voice = 0, hiss_voice = 0, wind_voice = 0;
        bool started = false;

        void start()
        {
            started = true;
            rocket = fx::make_rocket_sound();
            hiss = fx::make_hiss_sound();
            wind = make_breeze_sound();
            rocket_voice = rocket ? sl::play_sample(rocket, 0, 128, 1000, -1) : 0;
            hiss_voice = hiss ? sl::play_sample(hiss, 0, 128, 1000, -1) : 0;
            wind_voice = wind ? sl::play_sample(wind, 0, 128, 1000, -1) : 0;
        }
        void set(double rocket_level, double rcs_level, double wind_level)
        {
            // The voices are only created once the first frame is on screen, so nothing sounds during start-up.
            if (!started)
                start();
            if (wind_voice)
                sl::set_voice_volume(wind_voice, static_cast<int>(std::clamp(wind_level, 0.0, 1.0) * 40.0));
            if (rocket_voice)
                sl::set_voice_volume(rocket_voice, static_cast<int>(std::clamp(rocket_level, 0.0, 1.0) * 51.0));
            if (hiss_voice)
                sl::set_voice_volume(hiss_voice, static_cast<int>(std::clamp(rcs_level, 0.0, 1.0) * 28.0));
        }
        void stop()
        {
            sl::stop_all_samples();
            for (sl::Sample **s : {&rocket, &hiss, &wind})
            {
                if (*s)
                    sl::destroy_sample(*s);
                *s = nullptr;
            }
            sl::audio_fx_shutdown();
        }
    };

    // A props-library part moved into the ship frame: the prop's +Y becomes the nose direction (-Z) and its +Z umbilical faces +Y.
    bool make_prop_data(const kosmos::PartSpec &part, sl::MeshData &data)
    {
        auto colour = [](const float c[3]) { return sl::Colour{static_cast<std::uint8_t>(c[0] * 255.0f), static_cast<std::uint8_t>(c[1] * 255.0f), static_cast<std::uint8_t>(c[2] * 255.0f), 255}; };
        const sl::Colour body = colour(part.colour), accent = colour(part.accent);
        if (part.prop == "stage")
            data = sl::make_rocket_stage_data(part.radius, part.length, body, accent);
        else if (part.prop == "adapter")
            data = sl::make_rocket_adapter_data(part.radius, part.top_radius, part.length, body, accent);
        else if (part.prop == "engines")
            data = sl::make_rocket_engines_data(part.radius, part.count, part.length);
        else if (part.prop == "fins")
            data = sl::make_rocket_fins_data(part.radius, part.span, part.length, part.count, body);
        else if (part.prop == "fairing")
            data = sl::make_payload_fairing_data(part.radius, part.length, body, accent);
        else if (part.prop == "csm")
            data = sl::make_apollo_csm_data(part.radius, body);
        else if (part.prop == "aeroplane")
        {
            // The prop's nose is +Z with wheels at y = 0; turn it about the vertical to face the ship's -Z.
            data = sl::make_aeroplane_data(part.span, body, accent);
            glm::mat4 turn = glm::rotate(glm::mat4(1.0f), 3.14159265f, glm::vec3(0.0f, 1.0f, 0.0f));
            turn[3] = glm::vec4(0.0f, 0.0f, part.z, 1.0f);
            sl::transform_mesh_data(data, turn);
            return true;
        }
        else
        {
            std::fprintf(stderr, "[kosmos] unknown prop part '%s'\n", part.prop.c_str());
            return false;
        }
        glm::mat4 to_ship(1.0f);
        to_ship[0] = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        to_ship[1] = glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);
        to_ship[2] = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
        to_ship[3] = glm::vec4(0.0f, 0.0f, part.z, 1.0f);
        sl::transform_mesh_data(data, to_ship);
        return true;
    }

    // A props-library part moved into the ship frame: the prop's +Y becomes the nose direction (-Z) and its +Z umbilical faces +Y.
    sl::Mesh *make_prop_mesh(const kosmos::PartSpec &part)
    {
        sl::MeshData data;
        return make_prop_data(part, data) ? sl::create_mesh(data.vertices, data.indices) : nullptr;
    }

    // A profile of revolution about Z as mesh data, in the part's colour; the same shape the lathe draws.
    sl::MeshData make_lathe_data(const kosmos::PartSpec &part, int segments = 28)
    {
        sl::MeshData data;
        const auto &profile = part.profile;
        const int rows = static_cast<int>(profile.size());
        for (int i = 0; i < rows; ++i)
        {
            const int a = std::max(i - 1, 0), b = std::min(i + 1, rows - 1);
            const float dz = profile[static_cast<std::size_t>(b)].first - profile[static_cast<std::size_t>(a)].first;
            const float dr = profile[static_cast<std::size_t>(b)].second - profile[static_cast<std::size_t>(a)].second;
            const float len = std::max(std::sqrt(dz * dz + dr * dr), 1e-6f);
            for (int s = 0; s <= segments; ++s)
            {
                const float angle = 6.2831853f * static_cast<float>(s) / static_cast<float>(segments);
                const float cs = std::cos(angle), sn = std::sin(angle);
                sl::Vertex3D v;
                v.position[0] = cs * profile[static_cast<std::size_t>(i)].second;
                v.position[1] = sn * profile[static_cast<std::size_t>(i)].second;
                v.position[2] = profile[static_cast<std::size_t>(i)].first;
                v.normal[0] = cs * dz / len;
                v.normal[1] = sn * dz / len;
                v.normal[2] = -dr / len;
                for (int c = 0; c < 3; ++c)
                    v.colour[c] = part.colour[c];
                v.uv[0] = static_cast<float>(s) / static_cast<float>(segments);
                v.uv[1] = static_cast<float>(i) / static_cast<float>(rows - 1);
                data.vertices.push_back(v);
            }
        }
        for (int i = 0; i + 1 < rows; ++i)
            for (int s = 0; s < segments; ++s)
            {
                const std::uint32_t p = static_cast<std::uint32_t>(i * (segments + 1) + s), q = p + segments + 1;
                data.indices.insert(data.indices.end(), {p, p + 1, q, p + 1, q + 1, q});
            }
        return data;
    }

    // A box-filtered halving, used when an image is larger than the GPU accepts.
    void halve_bitmap(sl::Bitmap &bitmap)
    {
        const int w = std::max(1, bitmap.width / 2), h = std::max(1, bitmap.height / 2);
        std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * h * 4);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                for (int c = 0; c < 4; ++c)
                {
                    int sum = 0;
                    for (int dy = 0; dy < 2; ++dy)
                        for (int dx = 0; dx < 2; ++dx)
                            sum += bitmap.pixels[(static_cast<std::size_t>(std::min(y * 2 + dy, bitmap.height - 1)) * bitmap.width + std::min(x * 2 + dx, bitmap.width - 1)) * 4 + c];
                    out[(static_cast<std::size_t>(y) * w + x) * 4 + c] = static_cast<std::uint8_t>(sum / 4);
                }
        bitmap.pixels = std::move(out);
        bitmap.width = w;
        bitmap.height = h;
    }

    // Loads a hi-res surface image onto the GPU, halving it until the backend accepts it; RAM is released afterwards.
    sl::Bitmap *finish_cpu_texture(sl::Bitmap *bitmap, const std::string &path, int max_size)
    {
        const int original_w = bitmap->width, original_h = bitmap->height;
        while (std::max(bitmap->width, bitmap->height) > max_size && bitmap->width > 1 && bitmap->height > 1)
            halve_bitmap(*bitmap);
        bitmap->smooth = true;
        bitmap->mipmaps = true;
        bitmap->anisotropy = 8.0f;
        while (!sl::upload_bitmap(bitmap))
        {
            if (bitmap->width <= 512)
            {
                std::fprintf(stderr, "[kosmos] could not upload %s\n", path.c_str());
                sl::destroy_bitmap(bitmap);
                return nullptr;
            }
            halve_bitmap(*bitmap);
        }
        std::fprintf(stderr, "[kosmos] surface texture %s: %dx%d (file %dx%d)\n", path.c_str(), bitmap->width, bitmap->height, original_w, original_h);
        bitmap->pixels.clear();
        bitmap->pixels.shrink_to_fit();
        bitmap->ram_dirty = false;
        return bitmap;
    }

    sl::Bitmap *load_surface_texture(const std::string &path, int max_size)
    {
        const bool is_dds = path.size() > 4 && !std::strcmp(path.c_str() + path.size() - 4, ".dds");
        if (is_dds)
        {
            sl::DdsOptions options;
            options.max_size = max_size;
            options.anisotropy = 8.0f;
            sl::Bitmap *bitmap = sl::load_dds(path, options);
            if (bitmap && bitmap->compressed)
            {
                std::fprintf(stderr, "[kosmos] surface texture %s: %dx%d compressed on the GPU\n", path.c_str(), bitmap->width, bitmap->height);
                return bitmap;
            }
            if (!bitmap)
            {
                std::fprintf(stderr, "[kosmos] could not load %s\n", path.c_str());
                return nullptr;
            }
            return finish_cpu_texture(bitmap, path, max_size);
        }
        sl::Bitmap *bitmap = sl::load_bitmap(path);
        if (!bitmap)
        {
            std::fprintf(stderr, "[kosmos] could not load %s\n", path.c_str());
            return nullptr;
        }
        return finish_cpu_texture(bitmap, path, max_size);
    }

    sl::Bitmap *load_optional_texture(const std::string &path, int max_size)
    {
        return std::filesystem::exists(path) ? load_surface_texture(path, max_size) : nullptr;
    }

    std::string find_texture_in_directory(const std::string &root, const std::string &directory, const std::string &name,
                                          std::initializer_list<const char *> extensions)
    {
        namespace fs = std::filesystem;
        for (const char *extension : extensions)
        {
            const std::string path = root + directory + name + extension;
            if (fs::exists(path))
                return path;
        }
        return {};
    }

    // A texture by name: data/textures-hires/ (optional, not in the repository) wins over the default set in data/textures/.
    std::string find_texture_file(const std::string &root, const std::string &name, std::initializer_list<const char *> extensions)
    {
        for (const char *directory : {"/data/textures-hires/", "/data/textures/"})
        {
            const std::string path = find_texture_in_directory(root, directory, name, extensions);
            if (!path.empty())
                return path;
        }
        return {};
    }

    // The first .ctx in a Celestia virtual-texture folder, or the folder itself when it has none. Empty if the folder is missing.
    std::string find_tile_source(const std::string &directory)
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::is_directory(directory, ec))
            return {};
        for (const fs::directory_entry &entry : fs::directory_iterator(directory, ec))
            if (entry.path().extension() == ".ctx")
                return entry.path().string();
        return directory;
    }

    std::string find_image_in_folder(const std::string &directory)
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::is_directory(directory, ec))
            return {};
        std::vector<fs::path> images;
        for (const fs::directory_entry &entry : fs::directory_iterator(directory, ec))
        {
            if (!entry.is_regular_file(ec))
                continue;
            const std::string extension = entry.path().extension().string();
            std::string filename = entry.path().filename().string();
            for (char &c : filename)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if ((extension == ".dds" || extension == ".png" || extension == ".jpg" || extension == ".jpeg") &&
                filename.find("bump") == std::string::npos)
                images.push_back(entry.path());
        }
        std::sort(images.begin(), images.end());
        return images.empty() ? std::string{} : images.front().string();
    }

    std::string find_surface_image(const std::string &root, const kosmos::Body &b)
    {
        if (!b.texture.empty())
            return root + "/data/" + b.texture;
        std::string lower = b.name;
        for (char &c : lower)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const std::string override = find_texture_in_directory(root, "/data/textures-hires/", lower, {".dds", ".png", ".jpg", ".jpeg"});
        if (!override.empty())
            return override;
        const std::string celestia = find_image_in_folder(root + "/data/celestia/hires/" + lower);
        if (!celestia.empty())
            return celestia;
        return find_texture_in_directory(root, "/data/textures/", lower, {".dds", ".png", ".jpg", ".jpeg"});
    }

    // A 3D cockpit's two MFD screens sit on a console in the ship frame (-Z is forward): a position, and a tilt back from the pilot.
    struct CockpitScreen
    {
        glm::vec3 centre;
        float width, height;
    };
    constexpr float screen_tilt = 0.436f;
    constexpr int mfd_target_size = 512;
    constexpr float mfd_target_frame = 400.0f;

    kosmos::MfdLayout mfd_target_layout()
    {
        kosmos::MfdLayout layout;
        layout.size = mfd_target_frame;
        layout.x = (mfd_target_size - mfd_target_frame) * 0.5f;
        layout.y = 22.0f;
        return layout;
    }

    CockpitScreen cockpit_screen(const kosmos::Cockpit &cp, int side)
    {
        return {glm::vec3(cp.eye) + glm::vec3(side == 0 ? -0.17f : 0.17f, -0.14f, -0.7f), 0.24f, 0.24f};
    }

    glm::mat3 cockpit_screen_basis()
    {
        const glm::vec3 up(0.0f, std::cos(screen_tilt), -std::sin(screen_tilt));
        const glm::vec3 normal(0.0f, std::sin(screen_tilt), std::cos(screen_tilt));
        return glm::mat3(glm::vec3(1.0f, 0.0f, 0.0f), up, normal);
    }

    sl::Mesh *make_screen_quad()
    {
        std::vector<sl::Vertex3D> v(4);
        const float xs[4] = {-0.5f, 0.5f, 0.5f, -0.5f}, ys[4] = {0.5f, 0.5f, -0.5f, -0.5f};
        for (int i = 0; i < 4; ++i)
        {
            v[i].position[0] = xs[i];
            v[i].position[1] = ys[i];
            v[i].normal[2] = 1.0f;
            v[i].uv[0] = xs[i] + 0.5f;
            v[i].uv[1] = ys[i] + 0.5f;
        }
        return sl::create_mesh(v, {0, 2, 1, 0, 3, 2});
    }

    // Exhaust as glowing particles that live in the ship's frame, each a hot core in a fainter halo (as in the exreel orbit scene).
    struct ExhaustFx
    {
        enum Kind : std::uint8_t { kerosene, hydrogen, methane, hypergolic, solid, nuclear, scramjet, afterburner, rcs, kinds, none = 255 };

        // How each plume looks: length and width relative to the engine's `plume`, the particle lifetime, and the colours
        // of the young core, the cooled tail and the halo.
        struct Style
        {
            float length, width, life, spread, halo;
            sl::Colour core, tail, glow;
        };

        static const Style &style(int kind)
        {
            static const Style table[kinds] = {
                {1.0f, 1.0f, 1.0f, 1.0f, 0.10f, {255, 235, 190, 255}, {255, 120, 40, 255}, {255, 110, 40, 255}},    // kerosene
                {1.7f, 0.55f, 0.9f, 0.5f, 0.07f, {235, 242, 255, 255}, {255, 165, 90, 255}, {120, 150, 255, 255}},  // hydrogen
                {1.15f, 0.8f, 1.0f, 0.8f, 0.09f, {255, 240, 225, 255}, {255, 150, 70, 255}, {110, 145, 255, 255}},  // methane
                {0.8f, 0.85f, 0.9f, 0.8f, 0.06f, {255, 225, 190, 255}, {255, 150, 90, 255}, {255, 150, 80, 255}},   // hypergolic
                {0.95f, 1.5f, 1.35f, 1.6f, 0.16f, {255, 245, 205, 255}, {255, 85, 20, 255}, {255, 90, 30, 255}},    // solid
                {1.4f, 0.5f, 1.0f, 0.4f, 0.16f, {205, 150, 255, 255}, {150, 60, 255, 255}, {130, 50, 255, 255}},    // nuclear
                {1.3f, 0.5f, 0.9f, 0.35f, 0.14f, {205, 228, 255, 255}, {70, 120, 255, 255}, {60, 110, 255, 255}},   // scramjet
                {1.1f, 0.5f, 0.8f, 0.4f, 0.12f, {255, 235, 190, 255}, {255, 110, 40, 255}, {255, 120, 50, 255}},    // afterburner
                {1.0f, 1.0f, 1.0f, 1.0f, 0.10f, {255, 235, 190, 255}, {255, 120, 40, 255}, {255, 110, 40, 255}},    // rcs
            };
            return table[kind];
        }

        // Which plume an engine makes, or none: jets are invisible unless the afterburner is lit, and a hyperjump drive has no plume.
        static int plume_kind(const kosmos::EngineSpec &e, double level)
        {
            using kosmos::EngineType;
            switch (e.type)
            {
            case EngineType::jet: return e.afterburner && level > 0.8 ? afterburner : none;
            case EngineType::scramjet: return scramjet;
            case EngineType::solid: return solid;
            case EngineType::nuclear: return nuclear;
            case EngineType::hyperjump: return none;
            case EngineType::rocket: break;
            }
            if (e.fuel == "hydrogen") return hydrogen;
            if (e.fuel == "methane") return methane;
            if (e.fuel == "hypergolic") return hypergolic;
            if (e.fuel == "solid") return solid;
            return kerosene;
        }

        // A shock diamond: a bright knot standing in the plume of a scramjet or an afterburner.
        struct Diamond
        {
            glm::vec3 position, axis;
            float size, strength;
            std::uint8_t kind;
        };

        struct Particle
        {
            glm::vec3 position, velocity;
            float age, life, size;
            std::uint8_t kind = kerosene;
        };
        std::vector<Particle> particles;
        std::vector<Diamond> diamonds;
        const kosmos::Ship *owner = nullptr;
        std::uint32_t seed = 1;
        float carry_main = 0.0f, carry_rcs[7] = {};

        float random01()
        {
            seed = seed * 1664525u + 1013904223u;
            return static_cast<float>(seed >> 8) / 16777216.0f;
        }

        void update(const kosmos::Ship &ship, double vacuum, float dt, bool spawn)
        {
            if (owner != &ship)
            {
                particles.clear();
                owner = &ship;
            }
            for (Particle &p : particles)
            {
                p.age += dt;
                p.position += p.velocity * dt;
            }
            particles.erase(std::remove_if(particles.begin(), particles.end(), [](const Particle &p) { return p.age >= p.life; }), particles.end());
            diamonds.clear();
            if (!spawn || particles.size() > 1400)
                return;
            const sl::FlightBody &fb = ship.body();

            // Thrusters are numbered in the order of the stages' engines.
            std::vector<const kosmos::EngineSpec *> engines;
            for (const kosmos::StageSpec &st : ship.spec().stages)
                for (const kosmos::EngineSpec &e : st.engines)
                    engines.push_back(&e);
            auto plume_of = [&](std::size_t i, const sl::Thruster &t) -> int
            {
                if (t.level <= 0.01 || t.max_thrust <= 0.0 || t.tank < 0 || fb.tanks[static_cast<std::size_t>(t.tank)].mass <= 0.0)
                    return none;
                return i < engines.size() ? plume_kind(*engines[i], t.level) : static_cast<int>(kerosene);
            };

            int firing = 0;
            for (std::size_t i = 0; i < fb.thrusters.size(); ++i)
                if (plume_of(i, fb.thrusters[i]) != none)
                    ++firing;
            for (std::size_t i = 0; i < fb.thrusters.size() && firing > 0; ++i)
            {
                const sl::Thruster &t = fb.thrusters[i];
                const int kind = plume_of(i, t);
                if (kind == none)
                    continue;
                const Style &st = style(kind);
                const double plume = i < engines.size() ? engines[i]->plume : 10.0;
                // An afterburner only lights above 80% throttle and ramps up from there.
                const float level = kind == afterburner ? static_cast<float>((t.level - 0.8) / 0.2) : static_cast<float>(t.level);
                const float length = static_cast<float>(plume) * st.length * level * (1.0f + 0.6f * static_cast<float>(vacuum));
                const float width = static_cast<float>((1.1 + 3.2 * vacuum) * (0.5 + 0.5 * t.level)) * st.width;
                const glm::vec3 dir = glm::vec3(-t.direction);
                glm::vec3 side1 = std::fabs(dir.x) < 0.9f ? glm::cross(dir, glm::vec3(1, 0, 0)) : glm::cross(dir, glm::vec3(0, 1, 0));
                side1 = glm::normalize(side1);
                const glm::vec3 side2 = glm::cross(dir, side1);
                const float rate = std::min(420.0f, 1000.0f / static_cast<float>(firing)) * level;
                float &carry = carry_main;
                carry += rate * dt;
                for (; carry >= 1.0f; carry -= 1.0f)
                {
                    const float angle = random01() * 6.2831853f, spread = (0.1f + 0.2f * static_cast<float>(vacuum)) * st.spread * random01();
                    const float life = (0.45f + random01() * 0.35f) * st.life;
                    const float speed = length / (0.62f * st.life) * (0.8f + 0.4f * random01());
                    const glm::vec3 radial = side1 * std::cos(angle) + side2 * std::sin(angle);
                    Particle p;
                    p.position = glm::vec3(t.position) + radial * (0.25f * width * random01());
                    p.velocity = dir * speed + radial * (spread * speed);
                    p.age = random01() * dt;
                    p.life = life;
                    p.size = width * 0.35f * (0.75f + 0.5f * random01());
                    p.kind = static_cast<std::uint8_t>(kind);
                    particles.push_back(p);
                }
                if (kind == scramjet || kind == afterburner)
                {
                    // Shock diamonds sit at fixed spacing down the plume, shrinking and fading with distance.
                    const float reach = length * 0.9f, gap = reach / 5.5f;
                    for (int k = 0; k < 5; ++k)
                    {
                        const float fall = 1.0f - 0.16f * static_cast<float>(k);
                        diamonds.push_back({glm::vec3(t.position) + dir * (gap * (0.7f + static_cast<float>(k))), dir, width * 0.42f * fall * (0.95f + 0.1f * random01()), level * fall, static_cast<std::uint8_t>(kind)});
                    }
                }
            }

            // Reaction-control jets: cold-gas puffs, one jet per commanded translation and per rotation couple.
            double top = 0.0, bottom = 0.0, radius = 1.0;
            bool first_stage = true;
            const auto &all_stages = ship.spec().stages;
            for (std::size_t s = static_cast<std::size_t>(std::clamp(ship.active_stage(), 0, std::max(0, static_cast<int>(all_stages.size()) - 1))); s < all_stages.size(); ++s)
            {
                const kosmos::StageSpec &st = all_stages[s];
                top = first_stage ? st.z + st.length * 0.5 : std::max(top, st.z + st.length * 0.5);
                bottom = first_stage ? st.z - st.length * 0.5 : std::min(bottom, st.z - st.length * 0.5);
                radius = first_stage ? st.radius : std::max(radius, st.radius);
                first_stage = false;
            }
            const float arm = static_cast<float>(std::max(1.5, 0.5 * (top - bottom))), centre = static_cast<float>(0.5 * (top + bottom));
            int slot = 0;
            auto jet = [&](const glm::vec3 &position, const glm::vec3 &push, float amount)
            {
                float &carry = carry_rcs[slot++ % 7];
                carry += 110.0f * amount * dt;
                for (; carry >= 1.0f; carry -= 1.0f)
                {
                    Particle p;
                    const glm::vec3 out = -push;
                    p.position = position;
                    p.velocity = out * (5.0f + random01() * 3.0f) + glm::vec3(random01() - 0.5f, random01() - 0.5f, random01() - 0.5f) * 1.2f;
                    p.age = random01() * dt;
                    p.life = 0.18f + random01() * 0.12f;
                    p.size = 0.09f + random01() * 0.05f;
                    p.kind = rcs;
                    particles.push_back(p);
                }
            };
            const glm::vec3 shove = glm::vec3(ship.rcs_push());
            if (glm::length(shove) > 0.01f)
                jet(glm::vec3(0.0f, 0.0f, centre) - shove * static_cast<float>(radius), shove, 1.0f);
            const glm::vec3 torque = glm::vec3(ship.rcs_torque());
            const glm::vec3 arms[3][2] = {{{0, 0, centre + arm}, {0, 0, centre - arm}}, {{0, 0, centre + arm}, {0, 0, centre - arm}}, {{static_cast<float>(radius), 0, centre}, {-static_cast<float>(radius), 0, centre}}};
            for (int axis = 0; axis < 3; ++axis)
            {
                const float amount = std::fabs(torque[axis]);
                if (amount < 0.05f)
                    continue;
                glm::vec3 a(0.0f);
                a[axis] = torque[axis] > 0.0f ? 1.0f : -1.0f;
                for (const glm::vec3 &p : arms[axis])
                {
                    const glm::vec3 force = glm::normalize(glm::cross(a, p - glm::vec3(0.0f, 0.0f, centre)));
                    jet(p, force, std::min(1.0f, amount));
                }
            }
        }

        void draw(sl::Mesh *glow_mesh, const glm::mat4 &hull) const
        {
            sl::Material glow;
            glow.lit = false;
            glow.fog = false;
            glow.blend = sl::MaterialBlend::additive;
            glow.cull = sl::CullMode::none;
            for (const Particle &p : particles)
            {
                const float age = p.age / p.life;
                const float fade = (1.0f - age) * (1.0f - age);
                const Style &st = style(p.kind);
                const float size = p.size * (0.6f + 1.2f * age);
                const glm::mat4 base = hull * glm::translate(glm::mat4(1.0f), p.position);
                glow.tint = st.glow;
                glow.tint.alpha = static_cast<std::uint8_t>(std::clamp(st.halo * fade * 255.0f, 0.0f, 255.0f));
                sl::draw_mesh(glow_mesh, base * glm::scale(glm::mat4(1.0f), glm::vec3(size * 2.6f)), glow);
                glow.tint = age < 0.35f ? st.core : st.tail;
                const float body = p.kind == scramjet || p.kind == afterburner ? 0.22f : 0.55f;
                glow.tint.alpha = static_cast<std::uint8_t>(std::clamp(body * fade * 255.0f, 0.0f, 255.0f));
                sl::draw_mesh(glow_mesh, base * glm::scale(glm::mat4(1.0f), glm::vec3(size)), glow);
            }
            for (const Diamond &d : diamonds)
            {
                const Style &st = style(d.kind);
                glm::vec3 side1 = std::fabs(d.axis.x) < 0.9f ? glm::cross(d.axis, glm::vec3(1, 0, 0)) : glm::cross(d.axis, glm::vec3(0, 1, 0));
                side1 = glm::normalize(side1);
                const glm::mat4 frame(glm::vec4(side1, 0.0f), glm::vec4(glm::cross(d.axis, side1), 0.0f), glm::vec4(d.axis, 0.0f), glm::vec4(d.position, 1.0f));
                const glm::mat4 base = hull * frame;
                // Stretched along the flow so each knot reads as a diamond.
                glow.tint = st.glow;
                glow.tint.alpha = static_cast<std::uint8_t>(std::clamp(0.30f * d.strength * 255.0f, 0.0f, 255.0f));
                sl::draw_mesh(glow_mesh, base * glm::scale(glm::mat4(1.0f), glm::vec3(d.size * 1.5f, d.size * 1.5f, d.size * 3.2f)), glow);
                glow.tint = st.core;
                glow.tint.alpha = static_cast<std::uint8_t>(std::clamp(1.0f * d.strength * 255.0f, 0.0f, 255.0f));
                sl::draw_mesh(glow_mesh, base * glm::scale(glm::mat4(1.0f), glm::vec3(d.size * 1.0f, d.size * 1.0f, d.size * 2.6f)), glow);
            }
        }
    };

    struct Visuals
    {
        std::vector<sl::Mesh *> limb_pool;
        sl::Mesh *sphere = nullptr, *glow = nullptr, *quad = nullptr, *cloud_shell = nullptr, *starfield = nullptr,  *tail = nullptr, *screen_quad = nullptr, *cockpit_box = nullptr;
        std::vector<sl::Mesh *> rings;
        std::vector<sl::Mesh *> body_cloud_meshes;
        std::vector<sl::Bitmap *> body_cloud_textures;
        sl::Bitmap *glow_texture = nullptr, *puff_texture = nullptr, *cloud_texture = nullptr, *lights_texture = nullptr;
        sl::Bitmap *star_surface = nullptr, *star_filaments = nullptr, *star_corona = nullptr, *star_corona_far = nullptr, *star_rays = nullptr, *star_limb = nullptr, *sky = nullptr;
        std::vector<sl::Bitmap *> body_textures;
        // Optional per-body shape replacing the sphere; entries may be null.
        std::vector<sl::Model *> body_meshes;
        // Optional per-body normal maps (fake relief for airless moons); entries may be null.
        std::vector<sl::Bitmap *> body_normals;
        std::unique_ptr<sl::PlanetTiles> earth_tiles;
        std::vector<int> surface_textured;
        std::map<const kosmos::PartSpec *, sl::Mesh *> parts;

        sl::Mesh *part_mesh(const kosmos::PartSpec &part)
        {
            auto found = parts.find(&part);
            if (found != parts.end())
                return found->second;
            sl::Mesh *mesh = part.prop.empty() ? fx::make_lathe_mesh(part.profile, 28) : make_prop_mesh(part);
            parts[&part] = mesh;
            return mesh;
        }
        struct LoadedModel
        {
            sl::Model *model = nullptr;
            std::filesystem::path path;
            std::filesystem::file_time_type stamp{};
        };
        std::map<const kosmos::PartSpec *, LoadedModel> models;
        sl::Model *part_model(const kosmos::PartSpec &part, const std::string &directory)
        {
            auto found = models.find(&part);
            if (found != models.end())
                return found->second.model;
            LoadedModel loaded;
            loaded.path = std::filesystem::path(directory) / part.model;
            std::error_code ec;
            loaded.stamp = std::filesystem::last_write_time(loaded.path, ec);
            loaded.model = sl::load_model(loaded.path.string());
            if (!loaded.model)
                std::fprintf(stderr, "[kosmos] cannot load part model %s: %s\n", part.model.c_str(), sl::last_error().c_str());
            if (loaded.model)
                for (sl::ModelPart &mp : loaded.model->parts)
                    mp.material.cull = sl::CullMode::none; // Orbiter draws meshes double-sided and many are not closed
            models[&part] = loaded;
            return loaded.model;
        }
        // Drops models whose file changed on disk so the next draw loads them again; this is how an edit made in
        // Blender appears. A file is left alone until it has stopped changing, as exporters write it in pieces.
        int reload_models()
        {
            int dropped = 0;
            const auto now = std::filesystem::file_time_type::clock::now();
            for (auto it = models.begin(); it != models.end();)
            {
                std::error_code ec;
                const auto stamp = std::filesystem::last_write_time(it->second.path, ec);
                if (!ec && stamp != it->second.stamp && now - stamp > std::chrono::milliseconds(600))
                {
                    if (it->second.model)
                        sl::destroy_model(it->second.model);
                    it = models.erase(it);
                    ++dropped;
                }
                else
                    ++it;
            }
            return dropped;
        }
        void clear_parts()
        {
            for (auto &entry : models)
                if (entry.second.model)
                    sl::destroy_model(entry.second.model);
            models.clear();
            for (auto &entry : parts)
                if (entry.second)
                    sl::destroy_mesh(entry.second);
            parts.clear();
        }
    };

    void draw_parts(Visuals &v, const kosmos::ShipSpec &spec, int first_stage, int last_stage, const glm::mat4 &model, bool lit = true)
    {
        for (int s = first_stage; s <= last_stage; ++s)
            for (const kosmos::PartSpec &part : spec.stages[static_cast<std::size_t>(s)].parts)
            {
                if (!part.model.empty())
                {
                    if (sl::Model *part_model = v.part_model(part, spec.directory))
                    {
                        glm::mat4 place = glm::translate(glm::mat4(1.0f), glm::vec3(part.offset));
                        if (part.rotate != sl::dvec3(0.0))
                        {
                            place = glm::rotate(place, glm::radians(static_cast<float>(part.rotate.z)), glm::vec3(0.0f, 0.0f, 1.0f));
                            place = glm::rotate(place, glm::radians(static_cast<float>(part.rotate.y)), glm::vec3(0.0f, 1.0f, 0.0f));
                            place = glm::rotate(place, glm::radians(static_cast<float>(part.rotate.x)), glm::vec3(1.0f, 0.0f, 0.0f));
                        }
                        if (part.scale != sl::dvec3(1.0))
                            place = glm::scale(place, glm::vec3(part.scale));
                        sl::draw_model(part_model, model * place);
                    }
                    continue;
                }
                sl::Mesh *mesh = v.part_mesh(part);
                if (!mesh)
                    continue;
                sl::Material m;
                m.tint = part.prop.empty() ? sl::Colour{static_cast<std::uint8_t>(part.colour[0] * 255.0f), static_cast<std::uint8_t>(part.colour[1] * 255.0f),
                                                        static_cast<std::uint8_t>(part.colour[2] * 255.0f), 255}
                                           : sl::Colour{255, 255, 255, 255};
                m.lit = lit;
                m.fog = false;
                m.pbr = true;
                m.metallic = 0.0f;
                m.roughness = 0.7f;
                sl::draw_mesh(mesh, model * glm::translate(glm::mat4(1.0f), glm::vec3(part.offset)), m);
            }
    }

    std::string format_time(double seconds)
    {
        const long total = static_cast<long>(std::max(seconds, 0.0));
        char buffer[64];
        if (total >= 86400)
            std::snprintf(buffer, sizeof(buffer), "%ldd %02ld:%02ld:%02ld", total / 86400, (total / 3600) % 24, (total / 60) % 60, total % 60);
        else
            std::snprintf(buffer, sizeof(buffer), "%02ld:%02ld:%02ld", total / 3600, (total / 60) % 60, total % 60);
        return buffer;
    }

    std::string format_distance(double metres)
    {
        char buffer[48];
        const double a = std::fabs(metres);
        if (a >= 1.0e9)
            std::snprintf(buffer, sizeof(buffer), "%.2f Gm", metres / 1.0e9);
        else if (a >= 1.0e5)
            std::snprintf(buffer, sizeof(buffer), "%.0f km", metres / 1.0e3);
        else if (a >= 1.0e3)
            std::snprintf(buffer, sizeof(buffer), "%.2f km", metres / 1.0e3);
        else
            std::snprintf(buffer, sizeof(buffer), "%.0f m", metres);
        return buffer;
    }
} // namespace

// A packaged build keeps data/ beside the executable; a development build falls back to the source tree.
static std::string default_root(const char *argv0)
{
    namespace fs = std::filesystem;
    std::vector<fs::path> candidates;
    std::error_code ec;
#ifndef _WIN32
    candidates.push_back(fs::read_symlink("/proc/self/exe", ec).parent_path());
#endif
    if (argv0 && *argv0)
        candidates.push_back(fs::absolute(fs::path(argv0), ec).parent_path());
    candidates.push_back(fs::current_path(ec));
    for (const fs::path &c : candidates)
        if (!c.empty() && fs::exists(c / "data" / "sol.json", ec))
            return c.string();
    return KOSMOS_ROOT;
}

// ---- Multiplayer window (F12) ----

// Move the player's ship to the close vicinity of another player. Beside a landed player it lands; otherwise it
// matches their velocity a short way off, so the two fly together.
bool snap_to_ghost(Sim &sim, Ship &ship, const kosmos::Ghost &ghost, const TerrainSet &terrains)
{
    const int soi = ghost.state.soi;
    if (!ghost.have_state || soi < 0 || soi >= static_cast<int>(sim.universe.bodies.size()))
        return false;
    const kosmos::Body &planet = sim.universe.bodies[static_cast<std::size_t>(soi)];
    const sl::dvec3 p = ghost.shown_position();
    const double r = glm::length(p);
    if (r < 1.0)
        return false;
    const sl::dvec3 up = p / r;
    const sl::dvec3 right = ghost.shown * sl::dvec3(1.0, 0.0, 0.0);
    const sl::dvec3 nose = ghost.shown * sl::dvec3(0.0, 0.0, -1.0);
    const sl::Terrain *terrain = terrains.find(soi);
    const double ground = terrain ? terrain->height_at_direction(up) : 0.0;
    const sl::dvec3 surface_velocity = ghost.state.velocity - glm::cross(sl::dvec3(0.0, planet.spin, 0.0), p);
    if (r - planet.radius - ground < 30.0 && glm::length(surface_velocity) < 5.0)
    {
        const sl::dvec3 beside = glm::normalize(p + right * 60.0);
        const sl::dvec3 east = glm::normalize(glm::cross(sl::dvec3(0.0, 1.0, 0.0), beside));
        const sl::dvec3 north = glm::cross(beside, east);
        const double heading = std::atan2(glm::dot(nose, east), glm::dot(nose, north)) * 180.0 / 3.14159265358979;
        ship.place_on_surface(soi, beside, heading, terrain ? terrain->height_at_direction(beside) : 0.0);
        return true;
    }
    const sl::dvec3 offset = ghost.shown * sl::dvec3(60.0, 25.0, 140.0);
    ship.place_relative(soi, p + offset, ghost.state.velocity, ghost.shown);
    return true;
}

struct NetUi
{
    char irc_host[128] = "", irc_port[8] = "6667", channel[64] = "#kosmos", nick[32] = "", advertise[128] = "", game_port[8] = "7777", join[160] = "", title[48] = "Kosmos", chat[300] = "";
    bool scroll_chat = false;
};

void copy_text(char *dest, std::size_t size, const std::string &text)
{
    std::snprintf(dest, size, "%s", text.c_str());
}

void draw_multiplayer_window(kosmos::NetPlay &net, NetUi &ui, bool &open, int system, const std::function<void(const kosmos::Ghost &)> &snap)
{
    ImGui::SetNextWindowPos(ImVec2(340.0f, 40.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(520.0f, 560.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.92f);
    if (!ImGui::Begin("Multiplayer [F12]", &open))
    {
        ImGui::End();
        return;
    }
    const bool playing = net.mode() != kosmos::NetPlay::Mode::none;
    ImGui::TextUnformatted(net.status().c_str());
    if (!net.error().empty())
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.45f, 1.0f), "%s", net.error().c_str());
    ImGui::TextDisabled("Everyone flies their own ship and sees the others as ghosts. Time warp is held at 1x while two or more play.");

    ImGui::BeginDisabled(playing);
    ImGui::InputText("Your name", ui.nick, sizeof ui.nick);
    ImGui::EndDisabled();

    ImGui::SeparatorText("Game");
    if (!playing)
    {
        ImGui::InputText("Port", ui.game_port, sizeof ui.game_port, ImGuiInputTextFlags_CharsDecimal);
        ImGui::InputText("Game title", ui.title, sizeof ui.title);
        ImGui::InputText("Public address (shown to others)", ui.advertise, sizeof ui.advertise);
        if (ImGui::Button("Host a game"))
        {
            const int port = std::atoi(ui.game_port);
            if (port >= 1 && port <= 65535)
                net.host(static_cast<std::uint16_t>(port), ui.nick, system);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("UDP port must be reachable (port forward or VPN).");
        ImGui::InputText("host:port", ui.join, sizeof ui.join);
        ImGui::SameLine();
        if (ImGui::Button("Join"))
        {
            std::string address;
            std::uint16_t port = kosmos::NetPlay::default_port;
            std::string text = ui.join;
            if (text.find(':') == std::string::npos)
                text += ":" + std::to_string(kosmos::NetPlay::default_port);
            if (lobby::split_endpoint(text, address, port))
                net.join(address, port, ui.nick, system);
        }
    }
    else
    {
        if (ImGui::Button("Leave game"))
            net.leave();
        for (const std::string &name : net.player_names())
            ImGui::BulletText("%s", name.c_str());
        for (const kosmos::Ghost &g : net.ghosts())
        {
            if (!g.have_state || g.age >= 3.0)
                continue;
            ImGui::PushID(static_cast<int>(g.id));
            if (ImGui::Button("Go to"))
                snap(g);
            ImGui::SameLine();
            ImGui::TextDisabled("%s: %s", g.nick.c_str(), g.state.ship.c_str());
            ImGui::PopID();
        }
    }

    ImGui::SeparatorText("Lobby (IRC)");
    const bool irc_active = net.irc.state() != lobby::IrcClient::State::disconnected;
    ImGui::BeginDisabled(irc_active);
    ImGui::InputText("IRC server", ui.irc_host, sizeof ui.irc_host);
    ImGui::InputText("IRC port", ui.irc_port, sizeof ui.irc_port, ImGuiInputTextFlags_CharsDecimal);
    ImGui::InputText("Channel", ui.channel, sizeof ui.channel);
    ImGui::EndDisabled();
    if (!irc_active)
    {
        if (ImGui::Button("Connect to lobby"))
        {
            const int port = std::atoi(ui.irc_port);
            if (port >= 1 && port <= 65535)
                net.lobby_connect(ui.irc_host, static_cast<std::uint16_t>(port), ui.nick, ui.channel);
        }
    }
    else if (ImGui::Button("Disconnect lobby"))
        net.lobby_disconnect();
    ImGui::SameLine();
    switch (net.irc.state())
    {
    case lobby::IrcClient::State::connecting: ImGui::TextUnformatted("connecting"); break;
    case lobby::IrcClient::State::registering: ImGui::TextUnformatted("registering"); break;
    case lobby::IrcClient::State::online: ImGui::Text("%s as %s, %d here", net.irc.channel().c_str(), net.irc.nick().c_str(), static_cast<int>(net.irc.users().size())); break;
    default: ImGui::TextDisabled("not connected"); break;
    }

    ImGui::Text("Games");
    ImGui::BeginChild("games", ImVec2(0.0f, 90.0f), true);
    const std::vector<lobby::GameListing> games = net.board.games();
    if (games.empty())
        ImGui::TextDisabled(irc_active ? "No games announced yet." : "Connect to the lobby to see games.");
    for (std::size_t i = 0; i < games.size(); ++i)
    {
        const lobby::GameListing &game = games[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::BeginDisabled(playing);
        if (ImGui::Button("Join"))
            net.join(game.address, game.port, ui.nick, system);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Text("%s  by %s  (%s:%d)", game.title.c_str(), game.host_nick.c_str(), game.address.c_str(), static_cast<int>(game.port));
        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::BeginChild("chat", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()), true);
    for (const std::string &line : net.chat)
        ImGui::TextWrapped("%s", line.c_str());
    if (ui.scroll_chat)
    {
        ImGui::SetScrollHereY(1.0f);
        ui.scroll_chat = false;
    }
    ImGui::EndChild();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("##chat", ui.chat, sizeof ui.chat, ImGuiInputTextFlags_EnterReturnsTrue) && ui.chat[0])
    {
        if (net.irc.say(ui.chat))
            net.chat.push_back(std::string("<") + net.irc.nick() + "> " + ui.chat);
        ui.chat[0] = 0;
        ui.scroll_chat = true;
        ImGui::SetKeyboardFocusHere(-1);
    }
    ImGui::TextDisabled("IRC is plain text: do not type anything private.");
    ImGui::End();
}

int main(int argc, char **argv)
{
    Options options;
    options.root = default_root(argc > 0 ? argv[0] : nullptr);
    std::vector<char *> backend_args{argv[0]};
    for (int i = 1; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--version"))
        {
            std::printf("Kosmos %s\n", KOSMOS_VERSION);
            return 0;
        }
        else if (!std::strcmp(argv[i], "--root") && i + 1 < argc)
            options.root = argv[++i];
        else if (!std::strcmp(argv[i], "--selftest") && i + 1 < argc)
            options.selftest = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--system") && i + 1 < argc)
            options.system_name = argv[++i];
        else if (!std::strcmp(argv[i], "--stars"))
            options.list_stars = true;
        else if (!std::strcmp(argv[i], "--galaxy-map"))
            options.galaxy_view = true;
        else if (!std::strcmp(argv[i], "--quantum"))
            options.quantum_window = true;
        else if (!std::strcmp(argv[i], "--test-jump") && i + 1 < argc)
            options.test_jump = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--max-texture") && i + 1 < argc)
            options.max_texture = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--host"))
        {
            options.net_host = true;
            if (i + 1 < argc && std::isdigit(static_cast<unsigned char>(argv[i + 1][0])))
                options.net_port = std::atoi(argv[++i]);
        }
        else if (!std::strcmp(argv[i], "--join") && i + 1 < argc)
            options.net_join = argv[++i];
        else if (!std::strcmp(argv[i], "--name") && i + 1 < argc)
            options.net_name = argv[++i];
        else if (!std::strcmp(argv[i], "--advertise") && i + 1 < argc)
            options.net_advertise = argv[++i];
        else if (!std::strcmp(argv[i], "--irc") && i + 1 < argc)
            options.net_irc = argv[++i];
        else if (!std::strcmp(argv[i], "--channel") && i + 1 < argc)
            options.net_channel = argv[++i];
        else if (!std::strcmp(argv[i], "--lobby"))
            options.net_window = true;
        else if (!std::strcmp(argv[i], "--no-tiles"))
            options.no_tiles = true;
        else if (!std::strcmp(argv[i], "--no-relief"))
            options.no_relief = true;
        else if (!std::strcmp(argv[i], "--blender") && i + 1 < argc)
            options.blender = argv[++i];
        else if (!std::strcmp(argv[i], "--part-editor"))
            options.part_window = true;
        else if (!std::strcmp(argv[i], "--hide-info"))
            options.hide_info = true;
        else if (!std::strcmp(argv[i], "--camera-window"))
            options.camera_window = true;
        else if (!std::strcmp(argv[i], "--settings-window"))
            options.settings_window = true;
        else if (!std::strcmp(argv[i], "--lua-window"))
            options.lua_window = true;
        else if (!std::strcmp(argv[i], "--export-base") && i + 2 < argc)
        {
            options.export_base = argv[++i];
            options.export_file = argv[++i];
        }
        else if (!std::strcmp(argv[i], "--check-data"))
            options.check_data = true;
        else if (!std::strcmp(argv[i], "--menu"))
            options.menu_visible = true;
        else if (!std::strcmp(argv[i], "--scenario-editor"))
            options.scenario_editor = true;
        else if (!std::strcmp(argv[i], "--base-editor"))
            options.base_editor = true;
        else if (!std::strcmp(argv[i], "--property-window"))
            options.property_window = true;
        else if (!std::strcmp(argv[i], "--base-cam") && i + 1 < argc)
            options.view_base = argv[++i];
        else if (!std::strcmp(argv[i], "--selftest-throttle") && i + 1 < argc)
            options.selftest_throttle = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--selftest-warp") && i + 1 < argc)
            options.selftest_warp = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--shot-throttle") && i + 1 < argc)
            options.shot_throttle = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--shot") && i + 1 < argc)
            options.shot = argv[++i];
        else if (!std::strcmp(argv[i], "--shot-at") && i + 1 < argc)
            options.shot_at = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--autopilot"))
            options.autopilot = true;
        else if (!std::strcmp(argv[i], "--cockpit"))
            options.cockpit = true;
        else if (!std::strcmp(argv[i], "--cockpit3d"))
            options.cockpit = options.cockpit3d = true;
        else if (!std::strcmp(argv[i], "--scenario") && i + 1 < argc)
            options.scenario = argv[++i];
        else if (!std::strcmp(argv[i], "--target") && i + 1 < argc)
            options.target = argv[++i];
        else if (!std::strcmp(argv[i], "--list-scenarios"))
            options.list_scenarios = true;
        else if (!std::strcmp(argv[i], "--mfd") && i + 1 < argc)
            options.mfd_display = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--mfd-left") && i + 1 < argc)
            options.mfd_left = argv[++i];
        else if (!std::strcmp(argv[i], "--mfd-right") && i + 1 < argc)
            options.mfd_right = argv[++i];
        else if (!std::strcmp(argv[i], "--ship") && i + 1 < argc)
        {
            // A number picks among the ships spawned in orbit; a name starts the scenario with that ship.
            const char *value = argv[++i];
            if (std::isdigit(static_cast<unsigned char>(value[0])))
                options.ship_index = std::atoi(value);
            else
                options.ship_name = value;
        }
        else if (!std::strcmp(argv[i], "--map-scale") && i + 1 < argc)
            options.map_scale = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--map-focus") && i + 1 < argc)
            options.map_focus = argv[++i];
        else if (!std::strcmp(argv[i], "--map"))
            options.map = true;
        else if (!std::strcmp(argv[i], "--orbit"))
            options.orbit_start = true;
        else if (!std::strcmp(argv[i], "--orbit-body") && i + 1 < argc)
            options.orbit_body = argv[++i];
        else if (!std::strcmp(argv[i], "--cam-pitch") && i + 1 < argc)
            options.cam_pitch = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--cam-yaw") && i + 1 < argc)
            options.cam_yaw = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--cam-fov") && i + 1 < argc)
            options.cam_fov = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--cam-antisun"))
            options.cam_antisun = true;
        else if (!std::strcmp(argv[i], "--cam-sunward"))
            options.cam_sunward = true;
        else if (!std::strcmp(argv[i], "--cam-distance") && i + 1 < argc)
            options.cam_distance = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--ambient") && i + 1 < argc)
            options.ambient = static_cast<float>(std::atof(argv[++i]));
        else if (!std::strcmp(argv[i], "--orbit-phase") && i + 1 < argc)
            options.orbit_phase = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--sunlit"))
            options.sunlit = true;
        else if (!std::strcmp(argv[i], "--orbit-half"))
            options.orbit_half = true;
        else if (!std::strcmp(argv[i], "--orbit-alt") && i + 1 < argc)
            options.orbit_altitude = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--earth-turn") && i + 1 < argc)
            options.earth_turn = std::atof(argv[++i]) * 3.14159265358979 / 180.0;
        else if (!std::strcmp(argv[i], "--warp-to") && i + 1 < argc)
            options.shot_time = std::atof(argv[++i]);
        else
            backend_args.push_back(argv[i]);
    }

    Sim sim;
    std::string error;
    if (!options.system_name.empty())
    {
        char *end = nullptr;
        const long index = std::strtol(options.system_name.c_str(), &end, 10);
        options.system = *end == '\0' ? static_cast<int>(index) : sim.galaxy.find(options.system_name);
        if (options.system < 0 || options.system >= sim.galaxy.size())
        {
            std::fprintf(stderr, "kosmos: unknown star system '%s' (try --stars)\n", options.system_name.c_str());
            return 1;
        }
    }
    if (!sim.init(options.root, error, options.system))
    {
        std::fprintf(stderr, "kosmos: %s\n", error.c_str());
        return 1;
    }
    if (options.list_stars)
    {
        std::printf("%d stars; system %d %s (class %c)\n", sim.galaxy.size(), sim.system_index(), sim.galaxy.star(sim.system_index()).name.c_str(),
                    sim.galaxy.star(sim.system_index()).spectral_class);
        for (const int id : sim.galaxy.nearest(sim.system_index(), 20))
            std::printf("%6d  %-22s class %c  %6.1f ly  %d planets\n", id, sim.galaxy.star(id).name.c_str(), sim.galaxy.star(id).spectral_class,
                        sim.galaxy.distance_ly(sim.system_index(), id), sim.galaxy.planet_count(id));
        return 0;
    }
    if (options.list_scenarios)
    {
        for (const kosmos::Scenario &sc : sim.scenarios())
            std::printf("%-22s %s\n", sc.id.c_str(), sc.name.c_str());
        return 0;
    }
    if (!options.export_base.empty())
    {
        // Every built-in prop of a base, one scene object each, as a GLB (also a check of the Blender bake).
        for (const kosmos::Base &base : sim.bases)
            if (base.id == options.export_base)
            {
                std::vector<kosmos::GlbNode> nodes;
                for (std::size_t i = 0; i < base.objects.size(); ++i)
                {
                    kosmos::GlbNode node;
                    node.name = std::to_string(i) + "_" + base.objects[i].type;
                    if (kosmos::bake_base_object(base.objects[i], node.mesh))
                        nodes.push_back(std::move(node));
                }
                std::string error;
                const bool ok = kosmos::write_glb(nodes, options.export_file, error);
                std::printf("%zu objects -> %s %s\n", nodes.size(), options.export_file.c_str(), ok ? "" : error.c_str());
                return ok ? 0 : 1;
            }
        std::printf("no base %s\n", options.export_base.c_str());
        return 1;
    }
    if (options.check_data)
    {
        // Every scenario and base must survive the editors' save and load unchanged.
        int bad = 0;
        for (const kosmos::Scenario &sc : sim.scenarios())
        {
            kosmos::Scenario again;
            std::string error;
            const std::string text = kosmos::scenario_to_json(sc);
            if (!kosmos::parse_scenario(text, sc.path, again, error) || kosmos::scenario_to_json(again) != text)
            {
                std::printf("scenario %s does not round-trip %s\n", sc.id.c_str(), error.c_str());
                ++bad;
            }
        }
        {
            std::vector<kosmos::Base> again;
            std::string error;
            const std::string text = kosmos::bases_to_json(sim.bases);
            if (!kosmos::parse_bases(text, "check", again, error) || kosmos::bases_to_json(again) != text || again.size() != sim.bases.size())
            {
                std::printf("bases do not round-trip %s\n", error.c_str());
                ++bad;
            }
        }
        std::printf("%zu scenarios, %zu bases, %d problems\n", sim.scenarios().size(), sim.bases.size(), bad);
        return bad == 0 ? 0 : 1;
    }
    // With no scenario or start flags, load the "default" scenario (data/scenarios/default.json, overridable from ~/.config/kosmos/scenarios).
    if (options.scenario.empty() && options.selftest <= 0.0 && !options.orbit_start && options.orbit_body.empty() && options.ship_name == "Kestrel" && sim.find_scenario("default"))
        options.scenario = "default";
    if (options.selftest > 0.0)
    {
        // The selftest has no terrain, so bases stand at sea level.
        for (kosmos::Base &base : sim.bases)
        {
            const int body = sim.universe.find(base.body);
            if (body >= 0)
            {
                base.direction = kosmos::base_direction(sim.universe, body, base);
                base.height = 0.0;
                kosmos::register_base_beacons(sim.nav, sim.universe, body, base);
            }
        }
        return run_selftest(sim, options);
    }

    sl::set_text_panel_style({true});
    if (!sl::configure_graphics_backend_from_args(static_cast<int>(backend_args.size()), backend_args.data()))
        return -1;
    if (!sl::set_gfx_mode(sl::GFX_AUTODETECT_WINDOWED, screen_w, screen_h))
    {
        std::fprintf(stderr, "Failed to initialize display: %s\n", sl::last_error().c_str());
        return -1;
    }
    sl::set_window_title("Kosmos");
    sl::Surface3D *surface = sl::create_surface_3d(screen_w, screen_h, 1);
    if (!surface)
    {
        std::fprintf(stderr, "3D surface unavailable on this backend: %s\n", sl::last_error().c_str());
        sl::shutdown();
        return -1;
    }

    Visuals vis;
    vis.sphere = sl::create_sphere_mesh(1.0f, 192, 96, {255, 255, 255, 255});
    vis.glow = sl::create_sphere_mesh(1.0f, 14, 8, {255, 255, 255, 255});
    vis.quad = sl::create_plane_mesh(2.0f, 2.0f, {255, 255, 255, 255});
    vis.glow_texture = make_glow_texture();
    vis.star_surface = make_star_surface(0, 11);
    vis.star_filaments = make_star_surface(1, 29);
    vis.star_corona = make_star_corona(5, 5.0);
    vis.star_corona_far = make_star_corona(41, 14.0);
    vis.star_rays = make_star_rays();
    vis.star_limb = make_star_limb();
    vis.sky = make_sky_texture();
    vis.puff_texture = make_puff_texture();
    vis.cloud_texture = fx::make_cloud_texture();
    vis.starfield = make_starfield();
    vis.tail = make_tail_mesh();
    if (!vis.sphere || !vis.glow || !vis.quad || !vis.glow_texture || !vis.puff_texture || !vis.cloud_texture || !vis.starfield)
    {
        std::fprintf(stderr, "Failed to create resources: %s\n", sl::last_error().c_str());
        sl::shutdown();
        return -1;
    }
    // Orbiter's Earth cloud and night-light layers, when imported with tools/orbiter_import.py.
    if (sl::Bitmap *clouds = load_optional_texture(find_texture_file(sim.root(), "earth_clouds", {".dds"}), options.max_texture))
    {
        sl::destroy_bitmap(vis.cloud_texture);
        vis.cloud_texture = clouds;
    }
    vis.lights_texture = load_optional_texture(find_texture_file(sim.root(), "earth_lights", {".dds", ".jpg", ".png"}), options.max_texture);
    int earth_id = -1;
    const kosmos::Body *earth_body = nullptr;
    std::vector<std::unique_ptr<BodyTerrain>> body_terrains;
    sl::Terrain *terrain = nullptr;
    TerrainSet terrain_set;
    sl::Mesh *cloud_shell = nullptr;
    kosmos::BaseRenderer base_renderer;
    const std::size_t base_beacon_mark = sim.nav.beacon_count();

    // Everything drawn or collided with that belongs to one star system; a quantum jump drops it and builds the next.
    auto leave_system = [&]()
    {
        body_terrains.clear();
        terrain_set.items.clear();
        terrain = nullptr;
        if (cloud_shell)
            sl::destroy_mesh(cloud_shell);
        cloud_shell = nullptr;
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
        vis.rings.clear();
        vis.surface_textured.clear();
    };
    auto enter_system = [&]() -> bool
    {
        sl::Bitmap *shared_rock = nullptr;
        for (std::size_t i = 0; i < sim.universe.bodies.size(); ++i)
        {
            const kosmos::Body &b = sim.universe.bodies[i];
            const sl::Colour average{static_cast<std::uint8_t>(b.colour[0] * 255.0f), static_cast<std::uint8_t>(b.colour[1] * 255.0f),
                                     static_cast<std::uint8_t>(b.colour[2] * 255.0f), 255};
            // The anonymous belt rocks share one texture instead of generating ninety.
            if (b.minor && shared_rock)
                vis.body_textures.push_back(nullptr);
            else
            {
                vis.body_textures.push_back(b.name == "Moon" ? fx::make_moon_texture() : fx::make_detail_texture(average, 31 + static_cast<int>(i) * 7, 0.45f, 0.15f, 256));
                if (b.minor)
                    shared_rock = vis.body_textures.back();
            }
            sl::Model *shape = nullptr;
            if (!b.mesh.empty())
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
                const std::string image = find_surface_image(sim.root(), b);
                if (!image.empty())
                    if (sl::Bitmap *surface = load_surface_texture(image, options.max_texture))
                    {
                        sl::destroy_bitmap(vis.body_textures.back());
                        vis.body_textures.back() = surface;
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
                    celestia_normal = find_image_in_folder(sim.root() + "/data/celestia/hires/" + lower + "_normal");
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

        // Tiled Earth imagery (optional, not in the repository: see the Developers Manual) replaces the single image on the sphere.
        if (!options.no_tiles)
        {
            const int home = sim.universe.home_id();
            if (home >= 0 && sim.universe.bodies[static_cast<std::size_t>(home)].name == "Earth")
            {
                const std::string hires = sim.root() + "/data/celestia/hires/";
                sl::PlanetTilesOptions tiles;
                // Skirts show as bright lines along tile edges from orbit.
                tiles.skirt = 0.0f;
                tiles.colour = find_tile_source(hires + "earth");
                tiles.specular = find_tile_source(hires + "earth_specular");
                if (!options.no_relief)
                    tiles.normal = find_tile_source(hires + "earth_normal/textures/medres/earth-normals-hs-32k");
                if (!tiles.colour.empty())
                {
                    vis.earth_tiles = sl::PlanetTiles::create(tiles);
                    if (!vis.earth_tiles)
                        std::fprintf(stderr, "kosmos: tiled Earth unavailable: %s\n", sl::last_error().c_str());
                }
                if (vis.earth_tiles)
                {
                    const std::size_t index = static_cast<std::size_t>(home);
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

        // The home planet is a streaming procedural terrain whose heights the launch gear also collides with.
        earth_id = sim.universe.home_id();
        if (earth_id < 0)
        {
            std::fprintf(stderr, "kosmos: system has no home planet with terrain\n");
            return false;
        }
        earth_body = &sim.universe.bodies[static_cast<std::size_t>(earth_id)];
        sim.universe.bodies[static_cast<std::size_t>(earth_id)].rotation_offset += options.earth_turn;
        // A tidally locked body with terrain does not spin, so its far side is fixed: face Earth at time zero.
        for (std::size_t i = 0; i < sim.universe.bodies.size(); ++i)
        {
            kosmos::Body &b = sim.universe.bodies[i];
            if (!b.terrain.empty() && static_cast<int>(i) != earth_id && b.parent >= 0)
            {
                const sl::dvec3 to_parent = sim.universe.position(b.parent) - sim.universe.position(static_cast<int>(i));
                b.rotation_offset = std::atan2(-to_parent.z, to_parent.x);
            }
        }
        sim.nav.truncate_beacons(base_beacon_mark);
        for (kosmos::Base &base : sim.bases)
        {
            const int body = sim.universe.find(base.body);
            if (body >= 0)
            {
                base.direction = kosmos::base_direction(sim.universe, body, base);
                base.height = base.elevation;
                kosmos::register_base_beacons(sim.nav, sim.universe, body, base);
            }
        }
        body_terrains.clear();
        terrain_set.items.clear();
        try
        {
            body_terrains.push_back(make_body_terrain(sim, earth_id, true));
            for (std::size_t i = 0; i < sim.universe.bodies.size(); ++i)
                if (static_cast<int>(i) != earth_id && !sim.universe.bodies[i].terrain.empty())
                    if (std::unique_ptr<BodyTerrain> extra = make_body_terrain(sim, static_cast<int>(i), false))
                        body_terrains.push_back(std::move(extra));
        }
        catch (const std::exception &e)
        {
            std::fprintf(stderr, "Terrain unavailable: %s\n", e.what());
            return false;
        }
        terrain = body_terrains[0]->terrain.get();
        for (const std::unique_ptr<BodyTerrain> &bt : body_terrains)
        {
            sim.universe.world.body(bt->body)->terrain = bt->terrain.get();
            terrain_set.items.emplace_back(bt->body, bt->terrain.get());
        }
        // Weather for the other worlds that have it: a generated cloud layer on its own shell.
        vis.body_cloud_meshes.assign(sim.universe.bodies.size(), nullptr);
        vis.body_cloud_textures.assign(sim.universe.bodies.size(), nullptr);
        for (std::size_t i = 0; i < sim.universe.bodies.size(); ++i)
        {
            const kosmos::Body &b = sim.universe.bodies[i];
            if (b.cloud_cover <= 0.0 || static_cast<int>(i) == earth_id)
                continue;
            vis.body_cloud_textures[i] = fx::make_cloud_texture(1024, 512, b.cloud_cover, 101 + static_cast<std::uint32_t>(i));
            vis.body_cloud_meshes[i] = sl::create_sphere_mesh(static_cast<float>(b.radius + b.cloud_height), 128, 64, {255, 255, 255, 255});
        }
        cloud_shell = sl::create_sphere_mesh(static_cast<float>(earth_body->radius + cloud_height), 256, 128, {255, 255, 255, 255});
        return true;
    };
    if (!enter_system())
    {
        sl::shutdown();
        return 1;
    }

    Ship *ship = start_scenario(sim, options, terrain_set);
    if (!ship)
    {
        std::fprintf(stderr, "No Kestrel addon found under %s\n", options.root.c_str());
        sl::shutdown();
        return 1;
    }

    EngineSound sound;
    bool sound_on = true, first_frame = true, show_orbit = true, show_info = !options.hide_info, running = true, shot_saved = false, map_mode = options.map;
    MapView map;
    GalaxyView gview;
    bool galaxy_mode = options.galaxy_view;
    int jump_to = -1;
    bool jump_to_star_orbit = false;
    bool quantum_window = options.quantum_window;
    int quantum_selected = -1;
    gview.refresh(sim.galaxy, sim.system_index());
    bool cockpit_view = options.cockpit;
    if (!options.target.empty())
        sim.props.set_string("/sim/target", options.target);
    int mfd_display = options.mfd_display;
    bool mfd_mouse_prev = false;
    bool cockpit3d = options.cockpit3d;
    // Camera selection (F2): a ship's interior camera or a base camera, besides the chase and cockpit views.
    int interior_cam = -1, base_view = -1, base_view_cam = 0;
    bool camera_window = options.camera_window, settings_window = options.settings_window, property_window = options.property_window, lua_window = options.lua_window;
    bool net_window = options.net_window;
    // Part editor: meshes are edited in Blender while the simulation waits (the pause property is restored afterwards).
    bool part_window = options.part_window, edit_paused = false, edit_was_paused = false;
    kosmos::BlenderEdit blender_edit;
    struct PartEdit
    {
        kosmos::PartSpec *part = nullptr;
        kosmos::PartSpec original;
        bool converted = false;
        std::filesystem::path file;
        int stage = 0, index = 0;
        std::string model;
    } part_edit;
    std::string part_status;
    char blender_buffer[260] = "";
    double model_poll = 0.0;
    kosmos::NetPlay netplay;
    NetUi net_ui;
    copy_text(net_ui.nick, sizeof net_ui.nick, options.net_name.empty() ? "Pilot" : options.net_name);
    copy_text(net_ui.irc_host, sizeof net_ui.irc_host, options.net_irc);
    copy_text(net_ui.channel, sizeof net_ui.channel, options.net_channel);
    copy_text(net_ui.advertise, sizeof net_ui.advertise, options.net_advertise);
    copy_text(net_ui.game_port, sizeof net_ui.game_port, std::to_string(options.net_port));
    if (options.net_host)
        netplay.host(static_cast<std::uint16_t>(options.net_port), net_ui.nick, sim.system_index());
    else if (!options.net_join.empty())
    {
        std::string join_text = options.net_join, join_address;
        std::uint16_t join_port = kosmos::NetPlay::default_port;
        if (join_text.find(':') == std::string::npos)
            join_text += ":" + std::to_string(kosmos::NetPlay::default_port);
        if (lobby::split_endpoint(join_text, join_address, join_port))
            netplay.join(join_address, join_port, net_ui.nick, sim.system_index());
    }
    kosmos::LuaConsole lua_console;
    PropertyBrowser property_browser;
    {
        const std::string settings_file = settings_path();
        lua_console.init(sim, sim.root() + "/data/scripts", settings_file.empty() ? std::string() : (std::filesystem::path(settings_file).parent_path() / "scripts").string());
    }
    settings_defaults(sim.props);
    settings_load(sim.props);
    kosmos::set_stock_model_folder(sim.root() + "/data");
    if (!kosmos::Sim::user_dir().empty())
        kosmos::set_base_model_folder((std::filesystem::path(kosmos::Sim::user_dir()) / "bases").string());
    if (!options.blender.empty())
        sim.props.set_string("/sim/settings/blender", options.blender);
    int applied_volume = -1;
    if (!options.view_base.empty())
    {
        const std::size_t colon = options.view_base.find(':');
        const std::string id = options.view_base.substr(0, colon);
        for (std::size_t i = 0; i < sim.bases.size(); ++i)
            if (sim.bases[i].id == id)
                base_view = static_cast<int>(i), base_view_cam = colon == std::string::npos ? 0 : std::atoi(options.view_base.c_str() + colon + 1);
    }
    sl::gui_init();
    sl::Bitmap *mfd_target[2] = {sl::create_render_target(mfd_target_size, mfd_target_size), sl::create_render_target(mfd_target_size, mfd_target_size)};
    vis.screen_quad = make_screen_quad();
    vis.cockpit_box = sl::create_cube_mesh(1.0f, {255, 255, 255, 255});
    sim.mfd.set_mode(0, std::max(0, sim.mfd.mode_by_id(options.mfd_left)));
    sim.mfd.set_mode(1, std::max(0, sim.mfd.mode_by_id(options.mfd_right)));
    float look_yaw = 0.0f, look_pitch = 0.0f;
    if (options.map_scale > 0.0)
        map.metres_per_pixel = options.map_scale;
    if (!options.map_focus.empty())
        map.focus = sim.universe.find(options.map_focus);
    int sas_index = 0;
    float cam_yaw = static_cast<float>(options.cam_yaw), cam_pitch = static_cast<float>(options.cam_pitch);
    double cam_distance = options.cam_distance > 0.0 ? options.cam_distance : ship->spec().camera_distance;
    int last_mouse_x = 0, last_mouse_y = 0;
    kosmos::MenuBar menu_bar;
    bool screenshot_requested = false;
    menu_bar.keep_visible = options.menu_visible;
    menu_bar.scenario_editor_open = options.scenario_editor;
    menu_bar.base_editor_open = options.base_editor;
    std::vector<kosmos::MenuWindow> menu_windows = {{"Part editor", "", &part_window}, {"Cameras", "F2", &camera_window}, {"Settings", "F3", &settings_window}, {"Properties", "F4", &property_window}, {"Lua console", "F11", &lua_window}, {"Multiplayer", "F12", &net_window}, {"Quantum navigation", "", &quantum_window}};
    kosmos::MenuActions menu_actions;
    menu_actions.start_scenario = [&](const kosmos::Scenario &sc)
    {
        vis.clear_parts();
        if (Ship *flown = apply_scenario(sim, sc, terrain_set))
        {
            ship = flown;
            cam_distance = ship->spec().camera_distance;
            cockpit_view = cockpit3d = false;
            interior_cam = base_view = -1;
            look_yaw = look_pitch = 0.0f;
        }
    };
    menu_actions.restart = [&]()
    {
        vis.clear_parts();
        ship = start_scenario(sim, options, terrain_set);
        cam_distance = ship->spec().camera_distance;
    };
    menu_actions.quit = [&]() { running = false; };
    menu_actions.screenshot = [&]() { screenshot_requested = true; };
    ExhaustFx exhaust_fx;
    sl::Event event_discard;
    double clock = 0.0;
    std::uint32_t last_time = sl::time_ms();
    // The engine's own limiter only sleeps when vsync is off, so turn it off and let simlib pace the frames.
    sl::set_vsync(false);
    sl::set_fps(60);

    while (running)
    {
        if (jump_to >= 0)
        {
            // The world stops while the next system loads; show a short hyperspace effect so the window never closes.
            const int target_system = jump_to;
            jump_to = -1;
            const std::string target_name = sim.galaxy.star(target_system).name;
            auto warp_frame = [&](float t)
            {
                while (sl::poll_event(&event_discard))
                {
                }
                sl::clear_to_colour(sl::screen, {0, 0, 0});
                const float cx = screen_w * 0.5f, cy = screen_h * 0.5f;
                std::uint32_t seed = 12345u;
                for (int i = 0; i < 260; ++i)
                {
                    seed = seed * 1664525u + 1013904223u;
                    const float a = static_cast<float>(seed >> 8) * (6.2831853f / 16777216.0f);
                    seed = seed * 1664525u + 1013904223u;
                    const float r0 = 0.05f + 0.95f * static_cast<float>(seed >> 8) / 16777216.0f;
                    const float r1 = r0 + t * (0.15f + r0) * 1.6f, r2 = r0 + t * 0.3f * r0;
                    const int shade = static_cast<int>(std::min(1.0f, 0.3f + t) * 255.0f * (0.4f + 0.6f * r0));
                    sl::line(sl::screen, static_cast<int>(cx + std::cos(a) * r2 * screen_w * 0.5f), static_cast<int>(cy + std::sin(a) * r2 * screen_h * 0.5f),
                                  static_cast<int>(cx + std::cos(a) * r1 * screen_w * 0.5f), static_cast<int>(cy + std::sin(a) * r1 * screen_h * 0.5f), {static_cast<std::uint8_t>(shade * 0.7f), static_cast<std::uint8_t>(shade * 0.85f), static_cast<std::uint8_t>(shade)});
                }
                const int fade = static_cast<int>(255.0f * std::clamp((t - 0.7f) / 0.3f, 0.0f, 1.0f));
                if (fade > 0)
                    sl::rectfill(sl::screen, 0, 0, screen_w, screen_h, {static_cast<std::uint8_t>(fade), static_cast<std::uint8_t>(fade), static_cast<std::uint8_t>(fade)});
                sl::gprintf_center(screen_h / 2 - 8, {200, 220, 255}, "Quantum jump to %s", target_name.c_str());
                sl::show_video_bitmap();
                sl::end_frame();
            };
            const std::uint32_t begin = sl::time_ms();
            warp_frame(0.0f);
            if (netplay.joined())
                netplay.leave();
            vis.clear_parts();
            leave_system();
            std::string jump_error;
            bool loaded = sim.change_system(target_system, jump_error) && enter_system();
            if (!loaded)
            {
                std::fprintf(stderr, "kosmos: quantum jump failed: %s\n", jump_error.c_str());
                break;
            }
            options.scenario.clear();
            options.orbit_altitude = 0.0;
            options.orbit_body = jump_to_star_orbit ? "auto" : "";
            options.orbit_start = !jump_to_star_orbit;
            if (jump_to_star_orbit && ship)
                options.ship_name = ship->spec().name;
            ship = start_scenario(sim, options, terrain_set);
            if (!ship)
                break;
            cam_distance = ship->spec().camera_distance;
            cockpit_view = cockpit3d = false;
            interior_cam = base_view = -1;
            look_yaw = look_pitch = 0.0f;
            map.focus = -1;
            quantum_selected = -1;
            galaxy_mode = false;
            exhaust_fx = ExhaustFx();
            gview.refresh(sim.galaxy, sim.system_index());
            // Hold the effect for at least a couple of seconds, fading to black before the new sky appears.
            for (std::uint32_t t = sl::time_ms() - begin; t < 2500; t = sl::time_ms() - begin)
                warp_frame(static_cast<float>(t) / 2500.0f);
            last_time = sl::time_ms();
        }
        if (options.test_jump >= 0 && clock > 1.0)
        {
            jump_to = options.test_jump;
            jump_to_star_orbit = true;
            options.test_jump = -1;
        }
        const std::uint32_t now = sl::time_ms();
        const double frame = std::clamp(static_cast<double>(now - last_time) * 0.001, 0.001, 0.1);
        last_time = now;
        clock += frame;
        kosmos::Universe &universe = sim.universe;
        sl::PropertyTree &props = sim.props;

        sl::Event event;
        while (sl::poll_event(&event))
        {
            sl::gui_handle_event(event);
            if (event.type() == sl::Event::Type::quit)
                running = false;
            else if (event.type() == sl::Event::Type::key_down && sl::gui_available() && ImGui::GetIO().WantTextInput)
                continue;
            else if (event.type() == sl::Event::Type::key_down)
            {
                const auto key = event.key();
                if (key == sl::Event::Key::f2)
                    camera_window = !camera_window;
                else if (key == sl::Event::Key::f3)
                    settings_window = !settings_window;
                else if (key == sl::Event::Key::f4)
                    property_window = !property_window;
                else if (key == sl::Event::Key::f11)
                    lua_window = !lua_window;
                else if (key == sl::Event::Key::f12)
                    net_window = !net_window;
                else if (key == sl::Event::Key::escape)
                {
                    if (!menu_bar.dismiss(menu_windows))
                        running = false;
                }
                else if (key == sl::Event::Key::letter_r)
                {
                    vis.clear_parts();
                    ship = start_scenario(sim, options, terrain_set);
                    cam_distance = ship->spec().camera_distance;
                }
                else if (key == sl::Event::Key::letter_z)
                {
                    props.set_bool("/autopilot/enabled", false);
                    props.set_double("/controls/throttle", 1.0);
                }
                else if (key == sl::Event::Key::letter_x)
                {
                    props.set_bool("/autopilot/enabled", false);
                    props.set_double("/controls/throttle", 0.0);
                }
                else if (key == sl::Event::Key::space)
                    props.set_bool("/controls/stage", true);
                else if (key == sl::Event::Key::letter_p)
                {
                    const bool enable = !props.get_bool("/autopilot/enabled");
                    props.set_bool("/autopilot/enabled", enable);
                    if (!enable)
                        props.set_string("/autopilot/status", "");
                }
                else if (key == sl::Event::Key::letter_t)
                {
                    props.set_bool("/autopilot/enabled", false);
                    sas_index = (sas_index + 1) % sas_mode_count;
                    props.set_string("/controls/sas", sas_modes[sas_index]);
                }
                else if (key == sl::Event::Key::f10 && !sim.scenarios().empty())
                {
                    // Load the next scenario.
                    const std::vector<kosmos::Scenario> &all = sim.scenarios();
                    std::size_t next = 0;
                    for (std::size_t i = 0; i < all.size(); ++i)
                        if (!options.scenario.empty() && sim.find_scenario(options.scenario) == &all[i])
                            next = (i + 1) % all.size();
                    options.scenario = all[next].id;
                    vis.clear_parts();
                    ship = start_scenario(sim, options, terrain_set);
                    cam_distance = ship->spec().camera_distance;
                }
                else if (key == sl::Event::Key::f5)
                    mfd_display = (mfd_display + 1) % 3;
                else if (key == sl::Event::Key::f6 || key == sl::Event::Key::f7)
                {
                    const int side = key == sl::Event::Key::f6 ? 0 : 1;
                    const int count = static_cast<int>(sim.mfd.modes().size());
                    if (count > 0)
                        sim.mfd.set_mode(side, (sim.mfd.unit(side).mode + 1) % count);
                }
                else if (key == sl::Event::Key::f8 || key == sl::Event::Key::f9)
                    sim.mfd.press(key == sl::Event::Key::f8 ? 0 : 1, 12);
                else if (key == sl::Event::Key::letter_u)
                    props.set_bool("/dock/undock", true);
                else if (key == sl::Event::Key::letter_o)
                    props.set_bool("/sim/settings/orbit-line", !props.get_bool("/sim/settings/orbit-line"));
                else if (key == sl::Event::Key::tab)
                    map_mode = !map_mode;
                else if (key == sl::Event::Key::letter_g)
                    galaxy_mode = !galaxy_mode;
                else if (galaxy_mode && key == sl::Event::Key::arrow_right)
                    gview.step_selection(1);
                else if (galaxy_mode && key == sl::Event::Key::arrow_left)
                    gview.step_selection(-1);
                else if (galaxy_mode && (key == sl::Event::Key::return_key || key == sl::Event::Key::keypad_enter) && gview.selected >= 0 && gview.selected != sim.system_index())
                {
                    jump_to = gview.selected;
                    jump_to_star_orbit = false;
                }
                else if (key == sl::Event::Key::letter_c)
                {
                    // C cycles chase view, 2D cockpit and (for ships with MFDs) the 3D cockpit.
                    interior_cam = base_view = -1;
                    if (!cockpit_view)
                        cockpit_view = true, cockpit3d = false;
                    else if (!cockpit3d && ship->spec().cockpit.mfds)
                        cockpit3d = true;
                    else
                        cockpit_view = cockpit3d = false;
                    look_yaw = look_pitch = 0.0f;
                }
                else if (key == sl::Event::Key::letter_v && sim.ship_count() > 1)
                {
                    props.set_bool("/autopilot/enabled", false);
                    props.set_string("/autopilot/status", "");
                    sim.activate((props.get_int("/sim/active-ship") + 1) % sim.ship_count());
                    ship = sim.active_ship();
                    props.set_double("/controls/throttle", 0.0);
                    props.set_string("/controls/sas", "stability");
                    sas_index = 0;
                    cam_distance = ship->spec().camera_distance;
                    look_yaw = look_pitch = 0.0f;
                    interior_cam = -1;
                }
                else if (key == sl::Event::Key::letter_f && map_mode)
                {
                    do
                        map.focus = map.focus + 1 >= static_cast<int>(universe.bodies.size()) ? -1 : map.focus + 1;
                    while (map.focus >= 0 && universe.bodies[static_cast<std::size_t>(map.focus)].minor);
                    map.metres_per_pixel = map.focus < 0 ? std::min(map.metres_per_pixel, 1.0e5) : std::max(universe.bodies[static_cast<std::size_t>(map.focus)].radius * 0.02, 1.0);
                }
                else if (key == sl::Event::Key::letter_f && !map_mode)
                    props.set_double("/controls/flaps", std::fmod(props.get_double("/controls/flaps") + 0.34, 1.02));
                else if (key == sl::Event::Key::letter_i)
                    show_info = !show_info;
                else if (key == sl::Event::Key::letter_j)
                {
                    // Jump to the closest other player.
                    const kosmos::Ghost *best = nullptr;
                    double best_d = 0.0;
                    for (const kosmos::Ghost &g : netplay.ghosts())
                    {
                        if (!g.have_state || g.age > 3.0 || g.state.soi < 0 || g.state.soi >= static_cast<int>(universe.bodies.size()))
                            continue;
                        const double d = glm::length(universe.position(g.state.soi) + g.shown_position() - ship->body().position);
                        if (!best || d < best_d)
                        {
                            best = &g;
                            best_d = d;
                        }
                    }
                    if (best && snap_to_ghost(sim, *ship, *best, terrain_set))
                        sim.log("Moved next to " + best->nick);
                }
                else if (key == sl::Event::Key::letter_m)
                    props.set_bool("/sim/settings/sound", !props.get_bool("/sim/settings/sound"));
                else if (key == sl::Event::Key::period)
                    props.set_int("/sim/warp-index", std::min(props.get_int("/sim/warp-index") + 1, Sim::warp_levels - 1));
                else if (key == sl::Event::Key::comma)
                    props.set_int("/sim/warp-index", std::max(props.get_int("/sim/warp-index") - 1, 0));
            }
            sl::display_handle_event(event);
        }

        // M and O keep their own flags; the settings (and the F4 browser) share them through the tree.
        sound_on = sim.props.get_bool("/sim/settings/sound");
        show_orbit = sim.props.get_bool("/sim/settings/orbit-line");
        {
            const int volume = std::clamp(sim.props.get_int("/sim/settings/volume"), 0, 100);
            if (volume != applied_volume)
            {
                applied_volume = volume;
                sl::audio_fx_set_volume(volume * 255 / 100);
            }
        }
        const bool typing = sl::gui_available() && ImGui::GetIO().WantTextInput;

        // Keyboard controls write the property tree; scripts and the cockpit use the same paths.
        const double throttle_rate = 0.5 * frame;
        const double throttle = props.get_double("/controls/throttle");
        if (!typing && sl::key_down(sl::Key::left_shift))
            props.set_double("/controls/throttle", std::min(1.0, throttle + throttle_rate));
        if (!typing && sl::key_down(sl::Key::left_control))
        {
            props.set_bool("/autopilot/enabled", false);
            props.set_double("/controls/throttle", std::max(0.0, throttle - throttle_rate));
        }
        if (options.shot_throttle >= 0.0)
            props.set_double("/controls/throttle", options.shot_throttle);
        auto axis = [typing](sl::Key plus, sl::Key minus) { return typing ? 0.0 : (sl::key_down(plus) ? 1.0 : 0.0) - (sl::key_down(minus) ? 1.0 : 0.0); };
        props.set_double("/controls/pitch", axis(sl::Key::letter_s, sl::Key::letter_w));
        props.set_double("/controls/yaw", axis(sl::Key::letter_a, sl::Key::letter_d));
        props.set_double("/controls/roll", axis(sl::Key::letter_q, sl::Key::letter_e));
        props.set_double("/controls/brake", !typing && sl::key_down(sl::Key::letter_b) ? 1.0 : 0.0);
        props.set_double("/controls/rcs-x", axis(sl::Key::arrow_right, sl::Key::arrow_left));
        props.set_double("/controls/rcs-y", axis(sl::Key::arrow_up, sl::Key::arrow_down));
        if (props.get_bool("/autopilot/enabled") && props.get_double("/controls/pitch") + props.get_double("/controls/yaw") != 0.0)
            props.set_bool("/autopilot/enabled", false);

        // The shot option can skip ahead in time on rails so a screenshot of an orbit needs no waiting.
        if (options.shot_time > 0.0 && universe.time() < options.shot_time && ship->can_rails(Controls{}))
            props.set_int("/sim/warp-index", Sim::warp_levels - 1);

        // Time warp is held at 1x while others share the sky.
        netplay.update(frame);
        netplay.lobby_update(net_ui.advertise, net_ui.title);
        if (netplay.shared())
            props.set_int("/sim/warp-index", 0);

        if (edit_paused && !blender_edit.active())
        {
            edit_paused = false;
            props.set_bool("/sim/paused", edit_was_paused);
            props.set_bool("/sim/blender-edit", false);
            part_status = "Blender closed; simulation resumed";
        }
        model_poll += frame;
        if (model_poll > 0.5)
        {
            model_poll = 0.0;
            if (vis.reload_models() > 0 && edit_paused)
                part_status = "Reloaded the edited mesh";
        }
        sim.step(frame);
        lua_console.tick(frame);
        ship = sim.active_ship();
        const kosmos::Telemetry &tm = ship->telemetry();
        if (netplay.joined())
        {
            kosmos::GhostState mine;
            const sl::FlightBody &nb = ship->body();
            mine.ship = ship->spec().name;
            mine.soi = tm.soi;
            mine.stage = static_cast<std::uint8_t>(std::clamp(ship->active_stage(), 0, 255));
            mine.throttle = static_cast<float>(props.get_double("/controls/throttle"));
            mine.engine = tm.thrust > 0.0;
            mine.position = nb.position - universe.position(tm.soi);
            mine.velocity = nb.velocity - universe.world.body(tm.soi)->velocity;
            mine.orientation = nb.orientation;
            netplay.publish(mine);
        }
        sl::FlightBody &fb = ship->body();

        if (interior_cam >= static_cast<int>(ship->spec().cameras.size()))
            interior_cam = -1;
        if (base_view >= static_cast<int>(sim.bases.size()) || (base_view >= 0 && base_view_cam >= static_cast<int>(sim.bases[static_cast<std::size_t>(base_view)].cameras.size())))
            base_view = -1;
        const bool in_base_cam = base_view >= 0 && !map_mode && !galaxy_mode;
        const bool in_interior = interior_cam >= 0 && !map_mode && !in_base_cam;
        const bool in_cockpit = cockpit_view && ship->spec().cockpit.present && !map_mode && !in_interior && !in_base_cam;
        const bool in_cockpit3d = in_cockpit && cockpit3d && ship->spec().cockpit.mfds;
        const int mx = sl::mouse_x(), my = sl::mouse_y();
        const int dx = mx - last_mouse_x, dy = my - last_mouse_y;
        const bool dragging = sl::mouse_button_down(sl::MouseButton::right) && !(sl::gui_available() && ImGui::GetIO().WantCaptureMouse);
        last_mouse_x = mx;
        last_mouse_y = my;
        const double zoom_in = sl::key_down(sl::Key::left_bracket) ? 1.0 / (1.0 + 2.0 * frame) : 1.0;
        const double zoom_out = sl::key_down(sl::Key::right_bracket) ? 1.0 + 2.0 * frame : 1.0;
        if (galaxy_mode)
            gview.ly_per_pixel = std::clamp(gview.ly_per_pixel * zoom_in * zoom_out, 0.02, 400.0);
        else if (map_mode)
        {
            if (dragging)
            {
                map.yaw -= static_cast<float>(dx) * 0.008f;
                map.pitch = std::clamp(map.pitch + static_cast<float>(dy) * 0.008f, 0.0f, 1.5708f);
            }
            map.metres_per_pixel = std::clamp(map.metres_per_pixel * zoom_in * zoom_out, 1.0, 1.0e12);
        }
        else if (in_base_cam)
        {
        }
        else if (in_cockpit || in_interior)
        {
            if (dragging)
            {
                look_yaw -= static_cast<float>(dx) * 0.008f;
                look_pitch = std::clamp(look_pitch - static_cast<float>(dy) * 0.008f, -1.4f, 1.4f);
            }
        }
        else
        {
            if (dragging)
            {
                cam_yaw -= static_cast<float>(dx) * 0.008f;
                cam_pitch = std::clamp(cam_pitch + static_cast<float>(dy) * 0.008f, -1.5f, 1.5f);
            }
            cam_distance = std::clamp(cam_distance * zoom_in * zoom_out, 8.0, 3.0e9);
        }

        // The camera orbits the ship in its local horizon frame, so the horizon stays level as the rocket pitches over.
        sl::dvec3 up, east, north;
        ship->local_frame(up, east, north);
        const double yaw_d = cam_yaw, pitch_d = cam_pitch;
        sl::dvec3 offset_dir = (east * std::sin(yaw_d) + north * std::cos(yaw_d)) * std::cos(pitch_d) + up * std::sin(pitch_d);
        if (options.cam_sunward)
            offset_dir = glm::normalize(universe.position(0) - fb.position);
        if (options.cam_antisun)
            offset_dir = glm::normalize(glm::normalize(fb.position - universe.position(0)) + east * std::sin(yaw_d) * 0.9 + up * std::sin(pitch_d) * 0.9);
        sl::dvec3 eye_world = fb.position + offset_dir * cam_distance;
        glm::vec3 ship_rel = glm::vec3(fb.position - eye_world);
        if (options.cam_antisun)
            ship_rel = glm::vec3(glm::normalize(universe.position(0) - eye_world) * 1000.0);
        double view_fov = options.cam_fov;
        if (in_cockpit || in_interior)
        {
            // Seen from the pilot's seat: the eye sits in the ship frame and the view looks along -Z, free to turn with the mouse.
            sl::dvec3 seat = ship->spec().cockpit.eye;
            double yaw = look_yaw, pitch = look_pitch;
            if (in_interior)
            {
                const kosmos::CameraSpec &cs = ship->spec().cameras[static_cast<std::size_t>(interior_cam)];
                seat = cs.position;
                yaw += cs.yaw * pi / 180.0;
                pitch += cs.pitch * pi / 180.0;
                view_fov = cs.fov;
            }
            eye_world = fb.position + fb.direction_to_world(seat);
            const double cl = std::cos(pitch);
            const sl::dvec3 look(-std::sin(yaw) * cl, std::sin(pitch), -std::cos(yaw) * cl);
            ship_rel = glm::vec3(fb.direction_to_world(look) * 1000.0);
            up = fb.direction_to_world(sl::dvec3(0.0, 1.0, 0.0));
        }
        else if (in_base_cam)
        {
            const kosmos::Base &bs = sim.bases[static_cast<std::size_t>(base_view)];
            const kosmos::BaseCamera &bc = bs.cameras[static_cast<std::size_t>(base_view_cam)];
            sl::dvec3 look;
            kosmos::base_camera_pose(universe, bs, bc, &fb.position, eye_world, look, up);
            ship_rel = glm::vec3(look * 1000.0);
            view_fov = bc.fov;
        }

        const int soi = tm.soi;
        const kosmos::Body &soi_body = universe.bodies[static_cast<std::size_t>(soi)];
        const sl::dvec3 soi_pos = universe.position(soi);
        const bool home = soi == earth_id;
        const sl::dvec3 earth_pos = universe.position(earth_id);
        const sl::dvec3 eye_from_earth = eye_world - earth_pos;
        const double eye_earth_distance = glm::length(eye_from_earth);

        sl::TerrainView view;
        view.position = eye_from_earth;
        view.forward = glm::normalize(sl::dvec3(ship_rel));
        view.up = up;
        view.fov_y_degrees = view_fov;
        view.aspect = static_cast<double>(screen_w) / screen_h;
        view.viewport_height = screen_h;
        view.far_distance = eye_earth_distance + earth_body->radius * 1.3;
        const bool near_earth = eye_earth_distance < earth_body->radius + 2.0e7;
        // With a hi-res surface image, the terrain gives way to a plain textured sphere above 300 km, scaled by the body's size.
        const bool earth_has_image = std::find(vis.surface_textured.begin(), vis.surface_textured.end(), earth_id) != vis.surface_textured.end();
        const bool earth_as_sphere = earth_has_image && eye_earth_distance - earth_body->radius > 300.0e3 * earth_body->radius / 6.371e6;
        if (near_earth)
        {
            if (first_frame)
                settle(*terrain, view);
            else
                terrain->update(view);
        }
        for (std::size_t k = 1; k < body_terrains.size(); ++k)
        {
            BodyTerrain &bt = *body_terrains[k];
            const kosmos::Body &b = universe.bodies[static_cast<std::size_t>(bt.body)];
            bt.eye_rel = eye_world - universe.position(bt.body);
            const double threshold = 300.0e3 * b.radius / 6.371e6;
            const bool has_image = std::find(vis.surface_textured.begin(), vis.surface_textured.end(), bt.body) != vis.surface_textured.end();
            bt.active = glm::length(bt.eye_rel) - b.radius < (has_image ? threshold : 10.0 * threshold);
            if (bt.active)
            {
                sl::TerrainView body_view = view;
                body_view.position = bt.eye_rel;
                body_view.far_distance = glm::length(bt.eye_rel) + b.radius * 1.3;
                if (first_frame)
                    settle(*bt.terrain, body_view);
                else
                    bt.terrain->update(body_view);
            }
        }
        first_frame = false;

        const sl::dvec3 sun_dir_world = glm::normalize(-eye_world);
        const glm::vec3 to_sun = glm::vec3(sun_dir_world);

        sl::Camera camera;
        camera.position = glm::vec3(0.0f);
        camera.target = ship_rel;
        camera.up = glm::vec3(up);
        camera.field_of_view = static_cast<float>(view_fov);
        double extent = 4.0;
        for (const kosmos::StageSpec &st : ship->spec().stages)
            extent = std::max(extent, std::max(std::fabs(st.z - st.length * 0.5), std::fabs(st.z + st.length * 0.5)) + 6.0);
        const double eye_soi_altitude = glm::length(eye_world - soi_pos) - soi_body.radius;
        const double nearest_ground = std::max(1.0, 0.9 * (eye_soi_altitude - 10.0e3));
        const double ship_near = in_cockpit3d ? 0.25 : (in_cockpit || in_interior) ? 1.0 : in_base_cam ? 1.0 : std::max(1.0, std::min(cam_distance - extent, nearest_ground));
        // As Orbiter does: out in space the planets are drawn first with a near plane set by the nearest body, then the depth buffer
        // is cleared and the ships go over them with their own near plane, so neither range has to share one depth buffer.
        static bool depth_layers_work = true;
        const bool layered = depth_layers_work && !in_cockpit3d && !in_cockpit && !in_interior && !in_base_cam && eye_soi_altitude > 20.0e3;
        double near_plane = ship_near;
        if (layered)
        {
            double nearest_surface = 1.0e30;
            for (std::size_t bi = 0; bi < universe.bodies.size(); ++bi)
                nearest_surface = std::min(nearest_surface, glm::length(universe.position(static_cast<int>(bi)) - eye_world) - universe.bodies[bi].radius);
            near_plane = std::max(ship_near, 0.8 * nearest_surface);
        }
        camera.near_plane = static_cast<float>(near_plane);
        camera.far_plane = static_cast<float>(layered || cam_distance * 0.3 > 1.0e3 ? 1.0e10 : near_earth ? std::max(view.far_distance, soi_body.radius * 3.0) : std::max(soi_body.radius * 8.0, 6.0e8));
        sl::Camera ship_camera = camera;
        ship_camera.near_plane = static_cast<float>(ship_near);
        ship_camera.far_plane = 1.0e8f;

        sl::Environment3D environment;
        environment.sun_direction = -to_sun;
        environment.up = glm::vec3(glm::normalize(eye_from_earth));
        environment.camera_altitude = static_cast<float>(eye_earth_distance - earth_body->radius);
        environment.camera_position = eye_from_earth;
        environment.time = clock;
        environment.planet = near_earth;
        environment.planet_radius = static_cast<float>(earth_body->radius);
        environment.sun_intensity = 20.0f;
        environment.haze_scale = 0.35f;
        environment.ground_albedo = {0.1f, 0.25f, 0.12f};
        environment.fog_enabled = near_earth;
        environment.fog_density = near_earth ? 1.4e-4f : 0.0f;

        if (in_cockpit3d)
        {
            sim.mfd.update(frame);
            for (int side = 0; side < 2; ++side)
                sim.mfd.draw_to_target(side, mfd_target[side], mfd_target_layout());
        }
        if (sl::begin_surface_3d(surface, camera))
        {
            sl::clear_surface_3d({0, 0, 0, 255});
            sl::set_environment_3d(environment);
            if (near_earth)
                sl::draw_sky_3d();
            sl::Light3D light;
            light.direction = environment.sun_direction;
            // Above the atmosphere the Sun is not reddened by it, even when it sits on the horizon below.
            light.colour = near_earth && eye_earth_distance < earth_body->radius + 1.0e6 ? sl::atmosphere_sun_colour(environment) : glm::vec3(1.0f);
            light.ambient = options.ambient;
            // Near a surface the sky's fill light drops away after dusk, which is when lamps matter.
            {
                const int body_i = std::clamp(tm.soi, 0, static_cast<int>(universe.bodies.size()) - 1);
                const kosmos::Body &here = universe.bodies[static_cast<std::size_t>(body_i)];
                const sl::dvec3 from_centre = eye_world - universe.position(body_i);
                const double alt_here = glm::length(from_centre) - here.radius;
                if (body_i != 0 && alt_here < 200.0e3)
                {
                    const double sun_height = glm::dot(glm::normalize(from_centre), glm::normalize(universe.position(0) - eye_world));
                    const double dusk = std::clamp((0.10 - sun_height) / 0.16, 0.0, 1.0);
                    const double low = 1.0 - std::clamp((alt_here - 50.0e3) / 150.0e3, 0.0, 1.0);
                    light.ambient = static_cast<float>(glm::mix(static_cast<double>(options.ambient), std::min<double>(options.ambient, 0.07), dusk * dusk * (3.0 - 2.0 * dusk) * low));
                }
            }
            if (sim.system_index() != 0)
            {
                // Other suns light the scene in their own colour, and dimmer or brighter than Sol's 1 AU by their luminosity and our distance.
                const float *c = universe.bodies[0].colour;
                const float peak = std::max({c[0], c[1], c[2], 0.01f});
                const glm::vec3 hue = glm::mix(glm::vec3(c[0], c[1], c[2]) / peak, glm::vec3(1.0f), 0.45f);
                const double au_now = std::max(1.0e-3, glm::length(eye_world) / 1.495978707e11);
                const double flux = sim.galaxy.star(sim.system_index()).luminosity / (au_now * au_now);
                light.colour *= hue * static_cast<float>(std::clamp(std::pow(flux, 0.3), 0.45, 1.0));
            }
            sl::set_light_3d(light);

            // Lamps: the bases' floodlights and street lamps after dusk, and every ship's running lights.
            std::vector<kosmos::PlacedLight> lamps;
            {
                std::vector<sl::dvec3> ship_positions;
                for (int s = 0; s < sim.ship_count(); ++s)
                    ship_positions.push_back(sim.ship(s)->body().position);
                kosmos::collect_base_lights(universe, sim.bases, eye_world, 8000.0, clock, ship_positions, lamps);
                for (int s = 0; s < sim.ship_count(); ++s)
                {
                    const Ship &lit_ship = *sim.ship(s);
                    const sl::FlightBody &lb = lit_ship.body();
                    const glm::dmat3 turn = glm::mat3_cast(lb.orientation);
                    for (const kosmos::LightSpec &spec : lit_ship.spec().lights)
                    {
                        if (spec.stage >= 0 && lit_ship.active_stage() > spec.stage)
                            continue;
                        for (int i = 0; i < spec.count; ++i)
                        {
                            const sl::dvec3 at = lb.position + turn * sl::dvec3(spec.x + spec.step_x * i, spec.y + spec.step_y * i, spec.z + spec.step_z * i);
                            const double distance = glm::length(at - eye_world);
                            const float on = kosmos::light_pattern(spec, clock + i * 0.31);
                            if (distance > 40000.0 || on <= 0.001f)
                                continue;
                            kosmos::PlacedLight p;
                            p.light.position = glm::vec3(at - eye_world);
                            p.light.colour = glm::vec3(spec.colour.red, spec.colour.green, spec.colour.blue) / 255.0f;
                            p.light.intensity = spec.intensity * on;
                            p.light.range = spec.range;
                            p.light.inner_angle = spec.inner;
                            p.light.outer_angle = spec.outer;
                            p.light.direction = glm::vec3(glm::normalize(turn * sl::dvec3(spec.dx, spec.dy, spec.dz)));
                            p.glow_colour = p.light.colour;
                            p.glow_size = spec.glow;
                            p.glow_strength = on;
                            if (spec.navigation)
                            {
                                p.light.intensity *= 0.35f;
                                p.glow_size *= 0.5f;
                                p.glow_strength *= 0.7f;
                                p.far_size = 0.003f;
                            }
                            p.distance = static_cast<float>(distance);
                            lamps.push_back(p);
                        }
                    }
                }
                std::vector<const kosmos::PlacedLight *> order;
                for (const kosmos::PlacedLight &p : lamps)
                    order.push_back(&p);
                // The lamps that reach nearest the camera win the sixteen slots.
                std::sort(order.begin(), order.end(), [](const kosmos::PlacedLight *a, const kosmos::PlacedLight *b)
                          { return a->distance - 0.5f * a->light.range < b->distance - 0.5f * b->light.range; });
                std::vector<sl::LocalLight3D> chosen;
                for (const kosmos::PlacedLight *p : order)
                    if (static_cast<int>(chosen.size()) < sl::max_local_lights_3d && p->light.intensity > 0.0f && p->distance < 4000.0f)
                    {
                        chosen.push_back(p->light);
                        chosen.back().intensity *= 4.0f;
                        chosen.back().range *= 1.4f;
                    }
                sl::set_local_lights_3d(chosen);
            }

            // Every star is a real sphere at its true angular size, placed inside the far plane without changing how big it looks.
            struct StarPlace
            {
                bool valid = false;
                glm::vec3 dir{0.0f}, tint{1.0f};
                double distance = 0.0, radius = 0.0, angular = 0.0;
            };
            auto place_star = [&](std::size_t i) -> StarPlace
            {
                StarPlace sp;
                const kosmos::Body &b = universe.bodies[i];
                const sl::dvec3 rel = universe.position(static_cast<int>(i)) - eye_world;
                const double dist = glm::length(rel);
                if (b.kind != kosmos::BodyKind::star || dist <= b.radius * 1.02)
                    return sp;
                const double limit = std::min(camera.far_plane * 0.05, camera.near_plane * 1.0e5);
                sp.valid = true;
                sp.dir = glm::vec3(rel / dist);
                sp.angular = std::asin(std::min(1.0, b.radius / dist));
                sp.distance = std::min(dist, limit);
                sp.radius = dist < limit ? b.radius : b.radius * limit / dist;
                const float peak = std::max({b.colour[0], b.colour[1], b.colour[2], 0.01f});
                // Cool and red stars keep their hue but are lifted toward white so no star's surface reads dark.
                sp.tint = glm::mix(glm::vec3(b.colour[0], b.colour[1], b.colour[2]) / peak, glm::vec3(1.0f), 0.3f);
                return sp;
            };
            auto star_basis_base = [&](const StarPlace &sp, double distance, double half, float spin)
            {
                const glm::vec3 to_eye = -sp.dir;
                glm::vec3 right = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), to_eye));
                glm::vec3 up_v = glm::cross(to_eye, right);
                const float c = std::cos(spin), sn = std::sin(spin);
                const glm::vec3 r2 = right * c + up_v * sn, u2 = -right * sn + up_v * c;
                return glm::mat4(glm::vec4(r2 * static_cast<float>(half), 0.0f), glm::vec4(to_eye, 0.0f), glm::vec4(u2 * static_cast<float>(half), 0.0f),
                                 glm::vec4(sp.dir * static_cast<float>(distance), 1.0f));
            };
            auto tint_of = [](const glm::vec3 &c, float alpha)
            {
                return sl::Colour{static_cast<std::uint8_t>(255.0f * std::clamp(c.r, 0.0f, 1.0f)), static_cast<std::uint8_t>(255.0f * std::clamp(c.g, 0.0f, 1.0f)),
                                  static_cast<std::uint8_t>(255.0f * std::clamp(c.b, 0.0f, 1.0f)), static_cast<std::uint8_t>(255.0f * std::clamp(alpha, 0.0f, 1.0f))};
            };

            // The photosphere: an alpha-blended shell and an additive shell of filaments turning at different rates. Blended draws never write
            // depth, so the layers cannot z-fight at star distances where the depth buffer has no resolution; the late star loop calls this.
            auto draw_photosphere = [&](const StarPlace &sp)
            {
                if (!sp.valid || sp.angular < 2.0e-4)
                    return;
                const glm::mat4 at = glm::translate(glm::mat4(1.0f), sp.dir * static_cast<float>(sp.distance));
                sl::Material surface;
                surface.texture = vis.star_surface;
                surface.lit = false;
                surface.fog = false;
                surface.blend = sl::MaterialBlend::alpha;
                surface.tint = tint_of(sp.tint * 1.0f, 1.0f);
                sl::draw_mesh(vis.sphere, at * glm::rotate(glm::mat4(1.0f), static_cast<float>(clock * 0.015), glm::vec3(0, 1, 0)) *
                                              glm::scale(glm::mat4(1.0f), glm::vec3(static_cast<float>(sp.radius))), surface);
                sl::Material bloom;
                bloom.texture = vis.star_surface;
                bloom.lit = false;
                bloom.fog = false;
                bloom.blend = sl::MaterialBlend::additive;
                bloom.tint = tint_of(sp.tint, 0.08f);
                sl::draw_mesh(vis.sphere, at * glm::rotate(glm::mat4(1.0f), static_cast<float>(clock * 0.015), glm::vec3(0, 1, 0)) *
                                              glm::scale(glm::mat4(1.0f), glm::vec3(static_cast<float>(sp.radius * 1.0005))), bloom);
                sl::Material filaments;
                filaments.texture = vis.star_filaments;
                filaments.lit = false;
                filaments.fog = false;
                filaments.blend = sl::MaterialBlend::additive;
                filaments.tint = tint_of(glm::mix(sp.tint, glm::vec3(1.0f), 0.5f), 0.45f);
                sl::draw_mesh(vis.sphere, at * glm::rotate(glm::mat4(1.0f), static_cast<float>(-clock * 0.022 + 1.3), glm::vec3(0.3f, 1.0f, 0.1f)) *
                                              glm::scale(glm::mat4(1.0f), glm::vec3(static_cast<float>(sp.radius * 1.002))), filaments);
            };

            // The sky, stars and every star's glare go first, close in where the depth buffer still has resolution, and write no depth;
            // the opaque scene drawn afterwards then covers them wherever it is in front.
            const double backdrop = std::min(camera.far_plane * 0.05, camera.near_plane * 1.0e5);
            // Stars fade in as the sky darkens.
            {
                const double altitude = home ? tm.altitude : 1.0e6;
                const float star_alpha = static_cast<float>(fx::smoothstep(30.0e3, 110.0e3, altitude));
                if (star_alpha > 0.0f)
                {
                    sl::Material stars;
                    stars.texture = vis.puff_texture;
                    stars.lit = false;
                    stars.fog = false;
                    stars.cull = sl::CullMode::none;
                    stars.blend = sl::MaterialBlend::additive;
                    stars.tint.alpha = static_cast<std::uint8_t>(255.0f * star_alpha);
                    sl::Material sky;
                    sky.texture = vis.sky;
                    sky.lit = false;
                    sky.fog = false;
                    sky.cull = sl::CullMode::none;
                    sky.blend = sl::MaterialBlend::additive;
                    sky.tint.alpha = stars.tint.alpha;
                    // Close enough that depth keeps its resolution against the far plane, and turned so the pinched pole triangles face an axis the camera rarely looks along.
                    sl::draw_mesh(vis.sphere, glm::rotate(glm::mat4(1.0f), 1.5708f, glm::vec3(1, 0, 0)) * glm::scale(glm::mat4(1.0f), glm::vec3(static_cast<float>(backdrop))), sky);
                    sl::draw_mesh(vis.starfield, glm::scale(glm::mat4(1.0f), glm::vec3(static_cast<float>(backdrop))), stars);
                }
            }

            // Limb darkening, corona and glare for every star: constant-looking at any distance, bigger for bigger or nearer stars.
                        for (std::size_t i = 0; i < universe.bodies.size(); ++i)
            {
                const StarPlace sp = place_star(i);
                if (!sp.valid)
                    continue;
                const double D = sp.distance;
                const double disc = D * std::tan(sp.angular);
                const glm::vec3 white = glm::vec3(1.0f);
                const double min_half = D * std::tan(glm::radians(1.8));
                auto star_basis = [&](const StarPlace &s, double distance, double half, float spin) { return star_basis_base(s, distance, half, spin); };
                draw_photosphere(sp);
                // Up close the glare thins out so the surface shows through; from far away it stays full.
                const float gk = 1.0f - 0.92f * static_cast<float>(fx::smoothstep(0.05, 0.5, sp.angular / std::tan(glm::radians(view_fov * 0.5))));
                if (sp.angular >= 2.0e-4)
                {
                    sl::Material limb;
                    limb.texture = vis.star_limb;
                    limb.lit = false;
                    limb.fog = false;
                    limb.cull = sl::CullMode::none;
                    limb.blend = sl::MaterialBlend::alpha;
                    sl::draw_mesh(vis.quad, star_basis(sp, D, disc, 0.0f), limb);
                }
                sl::Material glow;
                glow.lit = false;
                glow.fog = false;
                glow.cull = sl::CullMode::none;
                glow.blend = sl::MaterialBlend::additive;
                // The corona: two layers of streamers turning against each other so they shimmer.
                const double corona_half = std::max(disc * 5.0, min_half * 1.5);
                glow.texture = vis.star_corona;
                glow.tint = tint_of(glm::mix(sp.tint, white, 0.35f), 0.9f * gk);
                sl::draw_mesh(vis.quad, star_basis(sp, D, corona_half, static_cast<float>(clock * 0.012)), glow);
                glow.tint = tint_of(glm::mix(sp.tint, white, 0.2f), 0.7f * gk);
                sl::draw_mesh(vis.quad, star_basis(sp, D, corona_half * 0.85, static_cast<float>(-clock * 0.017 + 2.0)), glow);
                // The far halo, the old soft glow and the diffraction spikes.
                glow.texture = vis.star_corona_far;
                glow.tint = tint_of(sp.tint, 0.55f * gk);
                sl::draw_mesh(vis.quad, star_basis(sp, D, std::max(disc * 14.0, min_half * 4.0), static_cast<float>(clock * 0.004)), glow);
                glow.texture = vis.glow_texture;
                glow.tint = tint_of(sp.tint, 1.0f * gk);
                sl::draw_mesh(vis.quad, star_basis(sp, D, std::max(disc * 3.0, min_half * 2.2), 0.0f), glow);
                glow.texture = vis.star_rays;
                glow.tint = tint_of(glm::mix(sp.tint, white, 0.5f), 0.8f * gk);
                const double rays_half = std::max(disc * 18.0, min_half * 6.0) * (1.0 + 0.04 * std::sin(clock * 1.7 + static_cast<double>(i)));
                sl::draw_mesh(vis.quad, star_basis(sp, D, rays_half, 0.0f), glow);
                sl::draw_mesh(vis.quad, star_basis(sp, D, rays_half * 0.55, 0.7853982f), glow);
            }

            if (near_earth)
            {
                if (!earth_as_sphere)
                {
                    sl::Material ground;
                    terrain->draw(eye_from_earth, ground);
                }
            }

            for (std::size_t k = 1; k < body_terrains.size(); ++k)
                if (body_terrains[k]->active)
                {
                    sl::Material ground;
                    body_terrains[k]->terrain->draw(body_terrains[k]->eye_rel, ground);
                }
            {
                sl::Material props;
                base_renderer.draw(universe, sim.bases, eye_world, 15000.0, props);
            }

            // A sunlit limb: a camera-facing disc whose opacity follows the atmosphere's thickness along each grazing ray.
            std::size_t limb_used = 0;
            auto draw_atmosphere_glow = [&](const sl::dvec3 &rel, const kosmos::Body &b)
            {
                if (b.atmosphere_density <= 0.0)
                    return;
                // Every atmosphere gets a broad haze, just under Venus's; denser ones, Venus most of all, are stronger still.
                const double dense = std::clamp(std::log10(1.0 + b.atmosphere_density) / std::log10(66.0), 0.0, 1.0);
                const double haze = 3.0 + 1.4 * dense;
                const double thickness = std::max(b.atmosphere_scale_height * 10.0, b.radius * 0.04) * haze * 0.7;
                if (glm::length(rel) - b.radius < thickness)
                    return;
                if (limb_used == vis.limb_pool.size())
                    vis.limb_pool.push_back(make_limb_mesh());
                sl::Mesh *mesh = vis.limb_pool[limb_used++];
                update_limb_mesh(mesh, glm::vec3(rel), b.radius, thickness, std::max(b.atmosphere_scale_height * haze * haze, b.radius * 0.04 * (0.8 + 0.2 * dense)), to_sun,
                                 glm::vec3(b.atmosphere_colour[0], b.atmosphere_colour[1], b.atmosphere_colour[2]));
                sl::Material shell;
                shell.lit = false;
                shell.fog = false;
                shell.cull = sl::CullMode::none;
                shell.blend = sl::MaterialBlend::additive;
                sl::draw_mesh(mesh, glm::mat4(1.0f), shell);
            };

            // Other bodies are spheres while inside the far plane; farther ones become markers on the overlay.
            for (std::size_t i = 0; i < universe.bodies.size(); ++i)
            {
                const kosmos::Body &b = universe.bodies[i];
                if (b.kind == kosmos::BodyKind::star || (static_cast<int>(i) == earth_id && near_earth && !earth_as_sphere))
                    continue;
                bool terrain_drawn = false;
                for (std::size_t k = 1; k < body_terrains.size(); ++k)
                    terrain_drawn = terrain_drawn || (body_terrains[k]->body == static_cast<int>(i) && body_terrains[k]->active);
                if (terrain_drawn)
                    continue;
                const sl::dvec3 rel = universe.position(static_cast<int>(i)) - eye_world;
                const double distance = glm::length(rel);
                if (distance - b.radius > camera.far_plane * 0.95)
                    continue;
                sl::Material rock;
                rock.texture = vis.body_textures[i];
                rock.normal_map = vis.body_normals[i];
                rock.fog = false;
                const glm::mat4 frame = glm::translate(glm::mat4(1.0f), glm::vec3(rel)) * glm::rotate(glm::mat4(1.0f), static_cast<float>(b.tilt), glm::vec3(0, 0, 1));
                const glm::mat4 model = frame * glm::rotate(glm::mat4(1.0f), static_cast<float>(universe.rotation_angle(static_cast<int>(i))), glm::vec3(0, 1, 0)) *
                                        glm::scale(glm::mat4(1.0f), glm::vec3(static_cast<float>(b.radius)));
                if (static_cast<int>(i) == earth_id && vis.earth_tiles)
                {
                    // The tiles run east to the right seen from outside; the body's sphere mesh runs the other way, so mirror the frame
                    // (which also reverses the triangle winding, hence the culling swap) to keep the surface where the rest of Kosmos expects it.
                    const glm::mat4 tile_model = model * glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 1.0f, -1.0f));
                    const glm::mat4 view_projection = camera.projection_matrix(static_cast<float>(screen_w) / screen_h) * camera.view_matrix() * tile_model;
                    const glm::dvec3 eye_local = glm::dvec3(glm::inverse(tile_model) * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
                    vis.earth_tiles->update(eye_local, glm::radians(view_fov), screen_h, &view_projection);
                    sl::Material tiled = rock;
                    tiled.cull = sl::CullMode::front;
                    vis.earth_tiles->draw(tile_model, tiled);
                }
                else
                    sl::draw_mesh(i < vis.body_meshes.size() && vis.body_meshes[i] ? vis.body_meshes[i]->parts[0].mesh : vis.sphere, model, rock);
                // Depth precision thins with distance, so each layer over the surface is scaled about the eye towards the camera: it looks the
                // same but sits clear of the planet in depth. The layers are lifted by different amounts so they keep their order too.
                const double depth_gap = std::min(16.0 * distance * distance / (near_plane * 1.68e7), 0.3 * distance);
                auto lifted = [&](double fraction)
                {
                    const float lift = static_cast<float>(1.0 - depth_gap * fraction / distance);
                    return glm::translate(glm::mat4(1.0f), glm::vec3(rel) * lift) * glm::rotate(glm::mat4(1.0f), static_cast<float>(b.tilt), glm::vec3(0, 0, 1)) *
                           glm::rotate(glm::mat4(1.0f), static_cast<float>(universe.rotation_angle(static_cast<int>(i))), glm::vec3(0, 1, 0)) *
                           glm::scale(glm::mat4(1.0f), glm::vec3(static_cast<float>(b.radius) * lift));
                };
                if (static_cast<int>(i) == earth_id && vis.lights_texture)
                {
                    // City lights: lit by a light shining from the night side, so they glow only where the Sun is not.
                    sl::Light3D night;
                    night.direction = to_sun;
                    night.ambient = 0.0f;
                    sl::set_light_3d(night);
                    sl::Material lights;
                    lights.texture = vis.lights_texture;
                    lights.fog = false;
                    lights.blend = sl::MaterialBlend::additive;
                    sl::draw_mesh(vis.sphere, lifted(0.25) * glm::scale(glm::mat4(1.0f), glm::vec3(1.0005f)), lights);
                    sl::set_light_3d(light);
                }
                if (b.atmosphere_density > 0.5 && b.atmosphere_density < 5.0)
                {
                    // Rayleigh scattering over the sunlit side: a faint, lit blue veil that lifts the oceans and hazes the land.
                    sl::Material veil;
                    veil.lit = true;
                    veil.fog = false;
                    veil.blend = sl::MaterialBlend::additive;
                    veil.tint = {static_cast<std::uint8_t>(b.atmosphere_colour[0] * 255.0f * 0.5f), static_cast<std::uint8_t>(b.atmosphere_colour[1] * 255.0f * 0.5f),
                                 static_cast<std::uint8_t>(b.atmosphere_colour[2] * 255.0f * 0.5f), 255};
                    sl::draw_mesh(vis.sphere, lifted(0.5) * glm::scale(glm::mat4(1.0f), glm::vec3(1.003f)), veil);
                }
                if (i < vis.body_cloud_meshes.size() && vis.body_cloud_meshes[i])
                {
                    sl::Material weather;
                    weather.texture = vis.body_cloud_textures[i];
                    weather.blend = sl::MaterialBlend::alpha;
                    weather.fog = false;
                    weather.tint = {static_cast<std::uint8_t>(b.cloud_colour[0] * 255.0f), static_cast<std::uint8_t>(b.cloud_colour[1] * 255.0f),
                                    static_cast<std::uint8_t>(b.cloud_colour[2] * 255.0f), 150};
                    const float k = static_cast<float>(1.0 - depth_gap / distance);
                    const glm::mat4 shell_frame = glm::translate(glm::mat4(1.0f), glm::vec3(rel) * k) * glm::rotate(glm::mat4(1.0f), static_cast<float>(b.tilt), glm::vec3(0, 0, 1));
                    sl::draw_mesh(vis.body_cloud_meshes[i], shell_frame * glm::rotate(glm::mat4(1.0f), static_cast<float>(universe.rotation_angle(static_cast<int>(i))), glm::vec3(0, 1, 0)) * glm::scale(glm::mat4(1.0f), glm::vec3(k)), weather);
                }
                draw_atmosphere_glow(rel, b);
                if (vis.rings[i])
                {
                    // Rings are lit from one side or the other; edge-on they thin to a line.
                    const glm::vec3 axis = glm::normalize(glm::vec3(frame * glm::vec4(0, 1, 0, 0)));
                    sl::Material ring;
                    ring.lit = false;
                    ring.fog = false;
                    ring.cull = sl::CullMode::none;
                    ring.blend = sl::MaterialBlend::alpha;
                    ring.tint.alpha = static_cast<std::uint8_t>(255.0f * (0.45f + 0.55f * std::fabs(glm::dot(axis, to_sun))));
                    sl::draw_mesh(vis.rings[i], frame * glm::scale(glm::mat4(1.0f), glm::vec3(static_cast<float>(b.ring_outer))), ring);
                }

                if (b.kind == kosmos::BodyKind::comet)
                {
                    // The tail streams away from the Sun and grows as the comet nears it.
                    const sl::dvec3 from_sun = universe.position(static_cast<int>(i)) - universe.position(0);
                    const double sun_au = glm::length(from_sun) / 1.495978707e11;
                    const double activity = fx::smoothstep(6.0, 0.6, sun_au);
                    if (activity > 0.01)
                    {
                        const glm::vec3 dir = glm::vec3(glm::normalize(from_sun));
                        const glm::vec3 x_axis = glm::normalize(glm::cross(std::fabs(dir.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0), dir));
                        const glm::mat4 basis(glm::vec4(x_axis, 0.0f), glm::vec4(glm::cross(dir, x_axis), 0.0f), glm::vec4(dir, 0.0f), glm::vec4(0, 0, 0, 1));
                        sl::Material tail;
                        tail.lit = false;
                        tail.fog = false;
                        tail.cull = sl::CullMode::none;
                        tail.blend = sl::MaterialBlend::additive;
                        tail.tint.alpha = static_cast<std::uint8_t>(80.0 * activity);
                        const float length = static_cast<float>(1.0e6 + activity * 9.0e8);
                        for (const float width : {1.0f, 0.62f, 0.36f, 0.16f})
                            sl::draw_mesh(vis.tail, glm::translate(glm::mat4(1.0f), glm::vec3(rel)) * basis * glm::scale(glm::mat4(1.0f), glm::vec3(length * width, length * width, length)), tail);
                    }
                }
            }

            if (near_earth)
            {
                // From space the terrain hides the sky's clouds, so the weather is a translucent shell lifted as depth coarsens.
                sl::Material cloud;
                cloud.texture = vis.cloud_texture;
                cloud.blend = sl::MaterialBlend::alpha;
                cloud.fog = false;
                const double altitude_now = eye_earth_distance - earth_body->radius;
                const double resolution = altitude_now * altitude_now / (near_plane * 1.68e7);
                const double shell_height = earth_as_sphere ? cloud_height : std::clamp(2.0 * resolution, cloud_height, 600.0e3);
                const float shell_scale = static_cast<float>((earth_body->radius + shell_height) / (earth_body->radius + cloud_height));
                sl::draw_mesh(cloud_shell, glm::translate(glm::mat4(1.0f), glm::vec3(earth_pos - eye_world)) * glm::scale(glm::mat4(1.0f), glm::vec3(shell_scale)), cloud);
            }

            if (earth_id >= 0 && near_earth)
                draw_atmosphere_glow(universe.position(earth_id) - eye_world, universe.bodies[static_cast<std::size_t>(earth_id)]);

            if (layered)
            {
                if (sl::clear_depth_3d())
                    sl::set_camera_3d(ship_camera);
                else
                    depth_layers_work = false;
            }

            // Ships and their jettisoned stages.
            for (int s = 0; s < sim.ship_count(); ++s)
            {
                Ship &other = *sim.ship(s);
                const sl::FlightBody &ob = other.body();
                const glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(ob.position - eye_world)) *
                                        glm::mat4(glm::mat3_cast(glm::quat(ob.orientation)));
                if (!((in_cockpit || in_interior) && &other == ship))
                    draw_parts(vis, other.spec(), other.active_stage(), other.stage_count() - 1, model);
                else if (in_cockpit3d)
                {
                    const kosmos::Cockpit &cp = other.spec().cockpit;
                    const glm::mat3 basis = cockpit_screen_basis();
                    sl::Material console;
                    console.tint = {38, 40, 46, 255};
                    console.cull = sl::CullMode::none;
                    sl::draw_mesh(vis.cockpit_box, model * glm::translate(glm::mat4(1.0f), glm::vec3(cp.eye) + glm::vec3(0.0f, -0.36f, -0.8f)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.8f, 0.3f, 0.5f)), console);
                    for (int side = 0; side < 2; ++side)
                    {
                        const CockpitScreen cs = cockpit_screen(cp, side);
                        sl::Material screen;
                        screen.texture = mfd_target[side];
                        screen.lit = false;
                        screen.fog = false;
                        screen.cull = sl::CullMode::none;
                        sl::draw_mesh(vis.screen_quad, model * glm::translate(glm::mat4(1.0f), cs.centre) * glm::mat4(basis) * glm::scale(glm::mat4(1.0f), glm::vec3(cs.width * 1.18f, cs.height * 1.13f, 1.0f)), screen);
                    }
                }
                for (const kosmos::Debris &d : other.debris())
                {
                    const sl::FlightBody *db = universe.world.body(d.body);
                    const glm::mat4 dm = glm::translate(glm::mat4(1.0f), glm::vec3(db->position - eye_world)) *
                                         glm::mat4(glm::mat3_cast(glm::quat(db->orientation))) *
                                         glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, static_cast<float>(-d.centre_z)));
                    draw_parts(vis, other.spec(), d.stage, d.stage, dm);
                }
            }

            // Other players' ships.
            for (const kosmos::Ghost &g : netplay.ghosts())
            {
                const kosmos::ShipSpec *gspec = g.have_state ? sim.find_ship_spec(g.state.ship) : nullptr;
                if (!gspec || g.state.soi < 0 || g.state.soi >= static_cast<int>(universe.bodies.size()) || g.age > 5.0)
                    continue;
                const sl::dvec3 gpos = universe.position(g.state.soi) + g.shown_position();
                const glm::mat4 gmodel = glm::translate(glm::mat4(1.0f), glm::vec3(gpos - eye_world)) * glm::mat4(glm::mat3_cast(glm::quat(g.shown)));
                const int last = static_cast<int>(gspec->stages.size()) - 1;
                draw_parts(vis, *gspec, std::min<int>(g.state.stage, last), last, gmodel);
            }

            // The bulbs themselves: a small bright core with a soft halo, drawn facing the camera.
            if (!lamps.empty())
            {
                sl::Material bulb;
                bulb.texture = vis.glow_texture;
                bulb.lit = false;
                bulb.fog = false;
                bulb.cull = sl::CullMode::none;
                bulb.blend = sl::MaterialBlend::additive;
                const auto facing = [](const glm::vec3 &at, float half)
                {
                    const glm::vec3 to_eye = -glm::normalize(at);
                    const glm::vec3 right = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), to_eye));
                    const glm::vec3 up_v = glm::cross(to_eye, right);
                    return glm::mat4(glm::vec4(right * half, 0.0f), glm::vec4(to_eye, 0.0f), glm::vec4(up_v * half, 0.0f), glm::vec4(at, 1.0f));
                };
                for (const kosmos::PlacedLight &p : lamps)
                {
                    if (p.glow_size <= 0.0f || p.distance < 0.5f)
                        continue;
                    // Far lamps stay a few pixels wide so a strobe still reads from kilometres away.
                    const float size = std::max(p.glow_size, p.distance * p.far_size);
                    const glm::vec3 c = glm::clamp(p.glow_colour * 255.0f, 0.0f, 255.0f);
                    bulb.tint = {static_cast<std::uint8_t>(c.r), static_cast<std::uint8_t>(c.g), static_cast<std::uint8_t>(c.b), static_cast<std::uint8_t>(std::clamp(p.glow_strength * 230.0f, 0.0f, 255.0f))};
                    sl::draw_mesh(vis.quad, facing(p.light.position, size * 7.0f), bulb);
                    sl::draw_mesh(vis.quad, facing(p.light.position, size * 3.0f), bulb);
                    bulb.tint.alpha = static_cast<std::uint8_t>(std::clamp(p.glow_strength * 255.0f, 0.0f, 255.0f));
                    bulb.tint.red = static_cast<std::uint8_t>(std::min(255.0f, c.r * 0.5f + 128.0f));
                    bulb.tint.green = static_cast<std::uint8_t>(std::min(255.0f, c.g * 0.5f + 128.0f));
                    bulb.tint.blue = static_cast<std::uint8_t>(std::min(255.0f, c.b * 0.5f + 128.0f));
                    sl::draw_mesh(vis.quad, facing(p.light.position, size * 1.2f), bulb);
                }
            }

            // Engine and reaction-control exhaust.
            {
                const glm::mat4 hull = glm::translate(glm::mat4(1.0f), glm::vec3(fb.position - eye_world)) * glm::mat4(glm::mat3_cast(glm::quat(fb.orientation)));
                const double vacuum = home ? fx::smoothstep(8.0e3, 70.0e3, tm.altitude) : 1.0;
                exhaust_fx.update(*ship, vacuum, static_cast<float>(frame), !ship->on_rails() && sim.current_warp() < 4.5);
                exhaust_fx.draw(vis.glow, hull);
            }
            sl::end_surface_3d();
        }
        sl::clear_to_colour(sl::screen, {0, 0, 0});
        sl::draw_sprite(sl::surface_3d_bitmap(surface), 0, 0);
        if (map_mode)
            map.draw(universe, *ship, tm);

        const glm::mat4 view_proj = camera.projection_matrix(static_cast<float>(screen_w) / screen_h) * camera.view_matrix();

        // Orbit line about the sphere-of-influence body, hidden where the body covers it.
        sl::OrbitElements elements;
        const bool have_orbit = universe.world.orbit_of(ship->body_id(), soi, elements);
        if (have_orbit && show_orbit && !options.hide_info && !map_mode && cam_distance > 2.0e3 && !ship->on_rails())
        {
            const std::vector<sl::dvec3> path = sl::orbit_path(soi_body.gm, elements, 360, std::min(soi_body.soi, 1.0e13));
            const float body_r = static_cast<float>(soi_body.radius);
            const glm::vec3 body_p = glm::vec3(soi_pos - eye_world);
            bool have_previous = false;
            float px = 0.0f, py = 0.0f;
            for (const sl::dvec3 &point : path)
            {
                const glm::vec3 p = glm::vec3(point + soi_pos - eye_world);
                const float ray_length = glm::length(p);
                const glm::vec3 dir = p / ray_length;
                const float along = glm::dot(body_p, dir);
                const bool hidden = along > 0.0f && along < ray_length && glm::length(body_p - dir * along) < body_r * 0.999f;
                float x = 0.0f, y = 0.0f;
                const bool visible = !hidden && project(view_proj, p, x, y) && std::fabs(x) < 1.0e4f && std::fabs(y) < 1.0e4f;
                if (visible && have_previous)
                    sl::line(sl::screen, px, py, x, y, {80, 255, 120});
                have_previous = visible;
                px = x;
                py = y;
            }
        }

        // Labels for bodies too small on screen to recognise.
        const float focal = static_cast<float>(screen_h * 0.5 / std::tan(glm::radians(view_fov * 0.5)));
        const bool body_labels = props.get_bool("/sim/settings/labels");
        if (props.get_bool("/sim/settings/ship-labels") && !map_mode && !galaxy_mode)
            for (int s = 0; s < sim.ship_count(); ++s)
            {
                if (sim.ship(s) == ship)
                    continue;
                const sl::dvec3 rel = sim.ship(s)->body().position - eye_world;
                const double distance = glm::length(rel);
                float x = 0.0f, y = 0.0f;
                if (distance < 1.0 || !project(view_proj, glm::vec3(rel / distance * 1000.0), x, y) || x < 0 || x > screen_w || y < 0 || y > screen_h)
                    continue;
                sl::circle(sl::screen, x, y, 5.0f, {120, 255, 150}, 1.0f);
                sl::gprintf(static_cast<int>(x) + 9, static_cast<int>(y) - 6, {120, 255, 150}, "%s  %s", sim.ship(s)->name.c_str(), format_distance(distance).c_str());
            }
        if (!map_mode && !galaxy_mode)
            for (const kosmos::Ghost &g : netplay.ghosts())
            {
                if (!g.have_state || g.age > 5.0 || g.state.soi < 0 || g.state.soi >= static_cast<int>(universe.bodies.size()))
                    continue;
                const sl::dvec3 rel = universe.position(g.state.soi) + g.shown_position() - eye_world;
                const double distance = glm::length(rel);
                float x = 0.0f, y = 0.0f;
                if (distance < 1.0 || !project(view_proj, glm::vec3(rel / distance * 1000.0), x, y) || x < 0 || x > screen_w || y < 0 || y > screen_h)
                    continue;
                sl::circle(sl::screen, x, y, 6.0f, {255, 200, 90}, 1.0f);
                sl::gprintf(static_cast<int>(x) + 10, static_cast<int>(y) - 6, {255, 200, 90}, "%s  %s", g.nick.c_str(), format_distance(distance).c_str());
            }
        for (std::size_t i = 0; i < universe.bodies.size() && !map_mode && body_labels; ++i)
        {
            const kosmos::Body &b = universe.bodies[i];
            if (b.minor)
                continue;
            const sl::dvec3 rel = universe.position(static_cast<int>(i)) - eye_world;
            const double distance = glm::length(rel);
            const double pixel_radius = b.radius / distance * focal;
            if (pixel_radius > 5.0 || distance < 1.0)
                continue;
            float x = 0.0f, y = 0.0f;
            if (!project(view_proj, glm::vec3(rel / distance * 1000.0), x, y) || x < 0 || x > screen_w || y < 0 || y > screen_h)
                continue;
            sl::circle(sl::screen, x, y, 6.0f, {200, 220, 255}, 1.0f);
            sl::gprintf(static_cast<int>(x) + 10, static_cast<int>(y) - 6, {200, 220, 255}, "%s", b.name.c_str());
        }

        // Sound.
        double rocket_level = 0.0;
        for (const sl::Thruster &t : fb.thrusters)
            if (t.max_thrust > 0.0 && t.level > 0.0 && t.tank >= 0 && fb.tanks[static_cast<std::size_t>(t.tank)].mass > 0.0)
                rocket_level = std::max(rocket_level, t.level);
        const double air = home ? 1.0 - fx::smoothstep(20.0e3, 90.0e3, tm.altitude) : 0.0;
        // Wind: a calm breeze near the ground, rising with airspeed, and drowned out by the engines.
        const double near_ground = home ? 1.0 - fx::smoothstep(200.0, 3000.0, tm.altitude) : 0.0;
        const double wind_level = air * std::max(0.45 * near_ground, std::min(1.0, tm.surface_speed / 200.0)) * (1.0 - std::min(1.0, rocket_level * 2.0));
        sound.set(sound_on ? rocket_level * (0.35 + 0.65 * air) : 0.0, 0.0, sound_on ? wind_level : 0.0);

        // HUD.
        const std::string soi_name = soi_body.name;
        if (show_info)
            sl::gprintf(16, 12, {240, 240, 240}, "KOSMOS   %s   T+%s", ship->name.c_str(), format_time(universe.time()).c_str());
        if (ship->crashed())
            sl::gprintf_center(screen_h / 2 - 20, {255, 90, 80}, "*** VESSEL LOST - press R to reset ***");
        if (show_info)
        {
            sl::gprintf(16, 34, {200, 220, 255}, "%s  Alt %s   Speed %.1f m/s   Vertical %.1f m/s", soi_name.c_str(), format_distance(tm.altitude).c_str(), tm.surface_speed, tm.vertical_speed);
            if (ship->spec().aircraft())
                sl::gprintf(16, 52, {120, 255, 150}, "Climb %.1f m/s   Flight path %.1f deg   AoA %.1f deg   Slip %.1f deg   Q %.0f Pa   Flaps %.0f%%%s", tm.vertical_speed, tm.flight_path,
                            tm.alpha, tm.sideslip, tm.dynamic_pressure, props.get_double("/controls/flaps") * 100.0, props.get_double("/controls/brake") > 0.5 ? "   BRAKES" : "");
            else if (tm.bound)
                sl::gprintf(16, 52, {120, 255, 150}, "Apoapsis %s   Periapsis %s   Period %.1f min   Ecc %.3f", format_distance(tm.apoapsis).c_str(),
                            format_distance(tm.periapsis).c_str(), tm.period / 60.0, tm.eccentricity);
            else
                sl::gprintf(16, 52, {255, 200, 120}, "Escape trajectory   Periapsis %s", format_distance(tm.periapsis).c_str());
            sl::gprintf(16, 70, {255, 220, 150}, "Throttle %3.0f%%   Thrust %.0f kN   TWR %.2f   Stage %d/%d fuel %.0f kg   Mass %.1f t", props.get_double("/controls/throttle") * 100.0,
                        tm.thrust / 1000.0, tm.twr, ship->active_stage() + 1, ship->stage_count(), tm.stage_fuel, tm.mass / 1000.0);
            sl::gprintf(16, 88, {220, 220, 220}, "Pitch %.1f  Heading %.1f   Hold: %s   Warp x%g%s", tm.pitch, tm.heading, props.get_string("/controls/sas").c_str(), sim.current_warp(),
                        ship->on_rails() ? " (rails)" : "");
            const std::string status = props.get_string("/autopilot/status");
            if (props.get_bool("/autopilot/enabled") || !status.empty())
                sl::gprintf(16, 106, {120, 220, 255}, "Autopilot: %s", status.c_str());
            int nearest = -1;
            double nearest_distance = 0.0;
            for (std::size_t i = 0; i < sim.bases.size(); ++i)
            {
                const kosmos::Base &bs = sim.bases[i];
                const int body = universe.find(bs.body);
                if (body < 0)
                    continue;
                const double d = glm::length(universe.position(body) + bs.direction * (universe.bodies[static_cast<std::size_t>(body)].radius + bs.height) - fb.position);
                if (nearest < 0 || d < nearest_distance)
                {
                    nearest = static_cast<int>(i);
                    nearest_distance = d;
                }
            }
            if (nearest >= 0)
                sl::gprintf(16, props.get_bool("/autopilot/enabled") || !status.empty() ? 124 : 106, {190, 190, 255}, "Nearest base: %s (%s)   %s",
                            sim.bases[static_cast<std::size_t>(nearest)].name.c_str(), sim.bases[static_cast<std::size_t>(nearest)].body.c_str(), format_distance(nearest_distance).c_str());
            if (netplay.mode() != kosmos::NetPlay::Mode::none)
            {
                const int line_y = (props.get_bool("/autopilot/enabled") || !status.empty() ? 124 : 106) + (nearest >= 0 ? 18 : 0);
                std::string near_text;
                double near_d = 0.0;
                for (const kosmos::Ghost &g : netplay.ghosts())
                {
                    if (!g.have_state || g.age > 5.0 || g.state.soi < 0 || g.state.soi >= static_cast<int>(universe.bodies.size()))
                        continue;
                    const double d = glm::length(universe.position(g.state.soi) + g.shown_position() - fb.position);
                    if (near_text.empty() || d < near_d)
                    {
                        near_text = g.nick;
                        near_d = d;
                    }
                }
                sl::gprintf(16, line_y, {255, 200, 90}, "%s%s%s", netplay.status().c_str(), near_text.empty() ? "" : "   Nearest player: ", near_text.empty() ? "" : (near_text + " " + format_distance(near_d)).c_str());
            }
        }
        const std::vector<std::string> &messages = sim.messages();
        for (std::size_t i = 0; i < messages.size(); ++i)
            sl::gprintf(16, screen_h - 60 - static_cast<int>(messages.size() - i) * 16, {255, 200, 120}, "%s", messages[i].c_str());
        const bool dashboard = in_cockpit && !in_cockpit3d && ship->spec().cockpit.mfds;
        if (in_cockpit3d)
        {
            // Pick MFD buttons by casting the mouse ray at the two screen quads.
            const bool down = sl::mouse_button_down(sl::MouseButton::left);
            if (down && !mfd_mouse_prev)
            {
                const kosmos::Cockpit &cp = ship->spec().cockpit;
                const glm::vec3 fwd = glm::normalize(ship_rel);
                const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(up)));
                const glm::vec3 cup = glm::cross(right, fwd);
                const float ty = std::tan(glm::radians(25.0f));
                const float nx = (2.0f * sl::mouse_x() / screen_w - 1.0f) * ty * static_cast<float>(screen_w) / screen_h;
                const float ny = (1.0f - 2.0f * sl::mouse_y() / screen_h) * ty;
                const glm::vec3 dir = glm::normalize(fwd + right * nx + cup * ny);
                const glm::mat3 basis = cockpit_screen_basis();
                const glm::mat3 to_world = glm::mat3_cast(glm::quat(fb.orientation));
                const glm::vec3 origin_local = glm::transpose(to_world) * (-glm::vec3(fb.position - eye_world));
                const glm::vec3 dir_local = glm::transpose(to_world) * dir;
                for (int side = 0; side < 2; ++side)
                {
                    const CockpitScreen cs = cockpit_screen(cp, side);
                    const float denom = glm::dot(dir_local, basis[2]);
                    if (std::fabs(denom) < 1e-5f)
                        continue;
                    const float t = glm::dot(cs.centre - origin_local, basis[2]) / denom;
                    if (t <= 0.0f)
                        continue;
                    const glm::vec3 hit = origin_local + dir_local * t - cs.centre;
                    const float u = glm::dot(hit, basis[0]) / (cs.width * 1.18f) + 0.5f;
                    const float v = 0.5f - glm::dot(hit, basis[1]) / (cs.height * 1.13f);
                    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
                        continue;
                    const int button = sim.mfd.hit_test(side, mfd_target_layout(), u * mfd_target_size, v * mfd_target_size);
                    if (button >= 0)
                        sim.mfd.press(side, button);
                    break;
                }
            }
            mfd_mouse_prev = down;
        }
        if (dashboard)
        {
            const float size = std::min(260.0f, screen_h * 0.3f);
            const float top = screen_h - size - 100.0f;
            sl::rectfill(sl::screen, 0, top - 70.0f, static_cast<float>(screen_w), static_cast<float>(screen_h), {14, 16, 20, 235});
            sl::rect(sl::screen, 0, top - 70.0f, static_cast<float>(screen_w), static_cast<float>(screen_h), {70, 78, 90, 255}, 2.0f);
            kosmos::MfdLayout left, right;
            left.size = right.size = size;
            left.y = right.y = top;
            left.x = screen_w / 2.0f - size - 70.0f;
            right.x = screen_w / 2.0f + 70.0f;
            sim.mfd.update(frame);
            const bool down = sl::mouse_button_down(sl::MouseButton::left);
            sim.mfd.handle_mouse(left, right, down && !mfd_mouse_prev, sl::mouse_x(), sl::mouse_y());
            mfd_mouse_prev = down;
            sim.mfd.draw(0, left);
            sim.mfd.draw(1, right);
        }
        if (in_cockpit && !in_cockpit3d)
        {
            const std::string root = "/ships/ship[" + std::to_string(props.get_int("/sim/active-ship")) + "]";
            const std::vector<kosmos::Instrument> &panel = ship->spec().cockpit.panel;
            const int cell = 150;
            const int panel_y = dashboard ? static_cast<int>(screen_h - std::min(260.0f, screen_h * 0.3f) - 100.0f - 62.0f) : screen_h - 120;
            const int left = screen_w / 2 - static_cast<int>(panel.size()) * cell / 2;
            for (std::size_t i = 0; i < panel.size(); ++i)
            {
                std::string path = panel[i].property;
                const std::size_t token = path.find("{ship}");
                if (token != std::string::npos)
                    path.replace(token, 6, root);
                char value[64];
                std::snprintf(value, sizeof value, panel[i].format.c_str(), props.get_double(path) * panel[i].scale);
                const int x = left + static_cast<int>(i) * cell;
                sl::rectfill(sl::screen, x, panel_y, x + cell - 6, panel_y + 42, {10, 24, 20});
                sl::rect(sl::screen, x, panel_y, x + cell - 6, panel_y + 42, {60, 160, 110});
                sl::gprintf(x + 6, panel_y + 4, {110, 200, 150}, "%s", panel[i].label.c_str());
                sl::gprintf(x + 6, panel_y + 22, {200, 255, 220}, "%s", value);
            }
        }
        if (mfd_display > 0 && !dashboard && !in_cockpit3d && !map_mode && !galaxy_mode)
        {
            const bool hud = mfd_display == 2;
            const float size = hud ? 300.0f : std::min(320.0f, screen_h * 0.38f);
            kosmos::MfdLayout left, right;
            left.size = right.size = size;
            left.hud = right.hud = hud;
            left.y = right.y = hud ? 140.0f : screen_h - size - 90.0f;
            left.x = hud ? 20.0f : screen_w / 2.0f - size - 70.0f;
            right.x = hud ? screen_w - size - 20.0f : screen_w / 2.0f + 70.0f;
            sim.mfd.update(frame);
            const bool down = sl::mouse_button_down(sl::MouseButton::left);
            if (!hud)
                sim.mfd.handle_mouse(left, right, down && !mfd_mouse_prev, sl::mouse_x(), sl::mouse_y());
            mfd_mouse_prev = down;
            sim.mfd.draw(0, left);
            sim.mfd.draw(1, right);
        }
        if (show_info && !map_mode && props.get_bool("/sim/settings/help-text"))
        {
            sl::gprintf_center(screen_h - 40, {200, 210, 220}, "[Z/X] Throttle  [Shift/Ctrl] +/-  [Space] Stage  [WASDQE] Rotate  [Arrows] RCS  [T] Hold  [P] Autopilot");
            sl::gprintf_center(screen_h - 22, {200, 210, 220}, "[,/.] Warp  [R-drag] Look  [ [ ] ] Zoom  [O] Orbit  [Tab] Map  [C] Cockpit/3D  [V] Ship  [F2] Cameras  [F3] Settings  [F4] Properties  [F11] Lua  [F12] Multiplayer  [J] Jump to player  [F5-F9] MFD  [F10] Scenario  [R] Reset");
        }
        if (galaxy_mode)
            gview.draw(sim.galaxy, sim.system_index());
        if (sl::gui_available())
        {
            sl::gui_new_frame();
            if (settings_window)
            {
                ImGui::SetNextWindowPos(ImVec2(330.0f, 12.0f), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowBgAlpha(0.9f);
                if (ImGui::Begin("Settings [F3]", &settings_window, ImGuiWindowFlags_AlwaysAutoResize))
                {
                    auto flag = [&](const char *label, const char *path, const char *tip)
                    {
                        bool v = props.get_bool(path);
                        if (ImGui::Checkbox(label, &v))
                            props.set_bool(path, v);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("%s", tip);
                    };
                    ImGui::SeparatorText("Display");
                    flag("Body labels", "/sim/settings/labels", "Name tags on planets and moons too small to recognise");
                    flag("Ship labels", "/sim/settings/ship-labels", "Name and distance tags on other ships");
                    flag("Orbit line", "/sim/settings/orbit-line", "Draw the flown ship's orbit (O)");
                    flag("Key help text", "/sim/settings/help-text", "The key reminder lines at the bottom of the screen");
                    ImGui::SeparatorText("Sound");
                    flag("Sound on", "/sim/settings/sound", "Engine sound (M)");
                    int volume = props.get_int("/sim/settings/volume");
                    if (ImGui::SliderInt("Volume", &volume, 0, 100, "%d%%"))
                        props.set_int("/sim/settings/volume", volume);
                    ImGui::SeparatorText("Simulation");
                    int warp = props.get_int("/sim/warp-index");
                    if (ImGui::SliderInt("Time warp", &warp, 0, Sim::warp_levels - 1))
                        props.set_int("/sim/warp-index", warp);
                    ImGui::TextDisabled("Settings are saved when you exit.");
                }
                ImGui::End();
            }
            menu_bar.draw_windows(sim, menu_actions);
            if (part_window)
            {
                ImGui::SetNextWindowPos(ImVec2(330.0f, 80.0f), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowSize(ImVec2(520.0f, 360.0f), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowBgAlpha(0.92f);
                if (ImGui::Begin("Part editor", &part_window))
                {
                    kosmos::Ship &flown = *sim.active_ship();
                    kosmos::ShipSpec &fspec = flown.editable_spec();
                    if (blender_buffer[0] == '\0')
                        copy_text(blender_buffer, sizeof blender_buffer, props.get_string("/sim/settings/blender", "blender"));
                    ImGui::SetNextItemWidth(260.0f);
                    if (ImGui::InputText("Blender", blender_buffer, sizeof blender_buffer))
                        props.set_string("/sim/settings/blender", blender_buffer);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("The Blender executable. Saved with the settings; --blender PATH overrides it.");
                    // Stays after Blender closes, so the converted mesh can still be written into the ship file.
                    if (part_edit.converted && !part_edit.model.empty())
                    {
                        ImGui::Separator();
                        ImGui::Text("Converted: %s", part_edit.model.c_str());
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Use in ship.json"))
                        {
                            std::string error;
                            part_status = kosmos::write_part_model(fspec.source_file, part_edit.stage, part_edit.index, part_edit.model, error)
                                              ? "ship.json now uses " + part_edit.model + " (original kept as ship.json.bak); effective on the next launch"
                                              : "Not written: " + error;
                        }
                        ImGui::Separator();
                    }
                    if (edit_paused)
                    {
                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Editing %s in Blender", part_edit.file.filename().string().c_str());
                        ImGui::TextWrapped("The simulation is paused. Save in Blender (Ctrl+S) to see the change here; closing Blender resumes.");
                        if (ImGui::Button("Resume, keep Blender open"))
                        {
                            edit_paused = false;
                            props.set_bool("/sim/blender-edit", false);
                            props.set_bool("/sim/paused", edit_was_paused);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Cancel and restore"))
                        {
                            blender_edit.stop();
                            if (part_edit.converted && part_edit.part)
                                *part_edit.part = part_edit.original;
                            blender_edit.restore();
                            part_edit.model.clear();
                            part_edit.converted = false;
                            edit_paused = false;
                            props.set_bool("/sim/blender-edit", false);
                            props.set_bool("/sim/paused", edit_was_paused);
                            part_status = "Edit cancelled; the original was restored";
                        }
                    }
                    else
                    {
                        ImGui::SeparatorText("Meshes of the flown ship");
                        int stage_index = 0;
                        for (kosmos::StageSpec &stage : fspec.stages)
                        {
                            int part_index = 0;
                            for (kosmos::PartSpec &part : stage.parts)
                            {
                                ImGui::PushID(&part);
                                const bool is_model = !part.model.empty();
                                const std::string what = is_model ? std::filesystem::path(part.model).filename().string() : (part.prop.empty() ? std::string("profile") : part.prop);
                                ImGui::Text("%s: %s", stage.name.c_str(), what.c_str());
                                ImGui::SameLine(ImGui::GetWindowWidth() - 150.0f);
                                if (ImGui::SmallButton(is_model ? "Edit in Blender" : "Convert + edit") && !netplay.shared())
                                {
                                    std::filesystem::path file;
                                    std::string error;
                                    bool ok = true;
                                    kosmos::PartSpec saved = part;
                                    if (is_model)
                                    {
                                        file = std::filesystem::path(fspec.directory) / part.model;
                                        std::string ext = file.extension().string();
                                        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                                        if (ext != ".glb" && ext != ".gltf" && ext != ".obj")
                                        {
                                            error = "Blender can edit .glb, .gltf and .obj parts; convert " + ext + " first";
                                            ok = false;
                                        }
                                    }
                                    else
                                    {
                                        sl::MeshData data;
                                        ok = part.prop.empty() ? (data = make_lathe_data(part), !data.indices.empty()) : make_prop_data(part, data);
                                        if (!ok)
                                            error = "this part has no geometry to export";
                                        else
                                        {
                                            std::string slug = fspec.name + "_" + std::to_string(stage_index) + "_" + std::to_string(part_index);
                                            for (char &c : slug)
                                                if (!std::isalnum(static_cast<unsigned char>(c)))
                                                    c = '_';
                                            file = std::filesystem::path(fspec.directory) / "models" / (slug + ".glb");
                                            std::error_code ec;
                                            std::filesystem::create_directories(file.parent_path(), ec);
                                            // An earlier conversion may hold edits: use it rather than rebuild the shape.
                                            if (!std::filesystem::exists(file, ec))
                                                ok = kosmos::write_glb(data, file, error);
                                            else
                                                part_status = "Reusing the existing " + file.filename().string();
                                        }
                                    }
                                    if (ok && props.get_bool("/sim/blender-edit"))
                                    {
                                        ok = false;
                                        part_status = "Another Blender edit is already running";
                                    }
                                    if (ok && blender_edit.start(props.get_string("/sim/settings/blender", "blender"), std::filesystem::path(sim.root()) / "data" / "blender" / "kosmos_edit.py", file, error))
                                    {
                                        part_edit.part = &part;
                                        part_edit.original = saved;
                                        part_edit.converted = !is_model;
                                        part_edit.file = file;
                                        part_edit.stage = stage_index;
                                        part_edit.index = part_index;
                                        part_edit.model.clear();
                                        if (!is_model)
                                        {
                                            part.model = std::filesystem::relative(file, fspec.directory).generic_string();
                                            part.prop.clear();
                                            part_edit.model = part.model;
                                            const std::string snippet = "{\"model\": \"" + part.model + "\", \"offset\": [" + std::to_string(part.offset.x) + ", " + std::to_string(part.offset.y) + ", " + std::to_string(part.offset.z) + "]}";
                                            ImGui::SetClipboardText(snippet.c_str());
                                            sim.log("Copied part entry for ship.json: " + snippet);
                                        }
                                        edit_was_paused = props.get_bool("/sim/paused");
                                        props.set_bool("/sim/paused", true);
                                        props.set_bool("/sim/blender-edit", true);
                                        edit_paused = true;
                                        part_status = part_status.rfind("Reusing", 0) == 0 ? part_status + "; Blender started" : "Blender started";
                                    }
                                    else if (!error.empty())
                                        part_status = error;
                                }
                                ImGui::PopID();
                                ++part_index;
                            }
                            ++stage_index;
                        }
                        if (fspec.stages.empty())
                            ImGui::TextDisabled("This ship has no parts.");
                    }
                    if (!part_status.empty())
                        ImGui::TextWrapped("%s", part_status.c_str());
                }
                ImGui::End();
            }
            if (lua_window)
                lua_console.draw(lua_window);
            if (net_window)
                draw_multiplayer_window(netplay, net_ui, net_window, sim.system_index(), [&](const kosmos::Ghost &g)
                                        {
                                            if (snap_to_ghost(sim, *ship, g, terrain_set))
                                                sim.log("Moved next to " + g.nick);
                                        });
            if (property_window)
                draw_property_browser(props, property_window, property_browser);
            if (quantum_window)
            {
                // The quantum drive: pick a star within range and arrive 1 AU above its surface.
                double range_ly = 0.0;
                for (const kosmos::StageSpec &st : ship->spec().stages)
                    for (const kosmos::EngineSpec &e : st.engines)
                        if (e.type == kosmos::EngineType::hyperjump)
                            range_ly = std::max(range_ly, e.range_ly);
                ImGui::SetNextWindowPos(ImVec2(330.0f, 120.0f), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowSize(ImVec2(420.0f, 400.0f), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowBgAlpha(0.9f);
                if (ImGui::Begin("Quantum navigation", &quantum_window))
                {
                    ImGui::Text("Ship: %s", ship->spec().name.c_str());
                    ImGui::Text("Position: %s", sim.galaxy.star(sim.system_index()).name.c_str());
                    if (range_ly <= 0.0)
                        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f), "This ship has no quantum jump drive.");
                    else
                    {
                        ImGui::Text("Drive range: %.0f light years", range_ly);
                        ImGui::Separator();
                        const std::vector<int> near = sim.galaxy.nearest(sim.system_index(), 64);
                        if (ImGui::BeginChild("targets", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() * 2.0f)))
                        {
                            if (ImGui::BeginTable("stars", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
                            {
                                ImGui::TableSetupColumn("Star");
                                ImGui::TableSetupColumn("Class");
                                ImGui::TableSetupColumn("Planets");
                                ImGui::TableSetupColumn("Distance");
                                ImGui::TableHeadersRow();
                                for (const int id : near)
                                {
                                    const double d = sim.galaxy.distance_ly(sim.system_index(), id);
                                    if (d > range_ly)
                                        break;
                                    const kosmos::Star &st = sim.galaxy.star(id);
                                    ImGui::TableNextRow();
                                    ImGui::TableNextColumn();
                                    if (ImGui::Selectable((st.name + "##" + std::to_string(id)).c_str(), quantum_selected == id, ImGuiSelectableFlags_SpanAllColumns))
                                        quantum_selected = id;
                                    ImGui::TableNextColumn();
                                    ImGui::Text("%c", st.spectral_class);
                                    ImGui::TableNextColumn();
                                    ImGui::Text("%d", sim.galaxy.planet_count(id));
                                    ImGui::TableNextColumn();
                                    ImGui::Text("%.2f ly", d);
                                }
                                ImGui::EndTable();
                            }
                        }
                        ImGui::EndChild();
                        const bool ok = quantum_selected >= 0 && quantum_selected != sim.system_index() && sim.galaxy.distance_ly(sim.system_index(), quantum_selected) <= range_ly;
                        if (quantum_selected >= 0)
                        {
                            const kosmos::Star &st = sim.galaxy.star(quantum_selected);
                            ImGui::Text("%s: %.1f solar masses, %.2f solar radii, %.0f K", st.name.c_str(), st.mass, st.radius, st.temperature);
                        }
                        else
                            ImGui::TextDisabled("Select a target star.");
                        if (!ok)
                            ImGui::BeginDisabled();
                        if (ImGui::Button("Engage quantum jump"))
                        {
                            jump_to = quantum_selected;
                            jump_to_star_orbit = true;
                        }
                        if (!ok)
                            ImGui::EndDisabled();
                        ImGui::SameLine();
                        ImGui::TextDisabled("Arrives in orbit of the best world there.");
                    }
                }
                ImGui::End();
            }
            if (camera_window)
            {
                ImGui::SetNextWindowPos(ImVec2(12.0f, 120.0f), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowSize(ImVec2(300.0f, 380.0f), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowBgAlpha(0.85f);
                if (ImGui::Begin("Cameras [F2]", &camera_window))
                {
                    const int active = props.get_int("/sim/active-ship");
                    // Picking a ship's camera also hands you that ship's controls.
                    auto pick = [&](int ship_index, bool cockpit, bool three_d, int interior, int base_index, int base_camera)
                    {
                        if (ship_index >= 0 && ship_index != props.get_int("/sim/active-ship"))
                        {
                            props.set_bool("/autopilot/enabled", false);
                            props.set_string("/autopilot/status", "");
                            sim.activate(ship_index);
                            ship = sim.active_ship();
                            props.set_double("/controls/throttle", 0.0);
                            props.set_string("/controls/sas", "stability");
                            sas_index = 0;
                            cam_distance = ship->spec().camera_distance;
                        }
                        cockpit_view = cockpit;
                        cockpit3d = three_d;
                        interior_cam = interior;
                        base_view = base_index;
                        base_view_cam = base_camera;
                        look_yaw = look_pitch = 0.0f;
                        map_mode = galaxy_mode = false;
                    };
                    const bool chase_now = !cockpit_view && interior_cam < 0 && base_view < 0;
                    for (int si = 0; si < sim.ship_count(); ++si)
                    {
                        const Ship &other = *sim.ship(si);
                        const kosmos::ShipSpec &spec = other.spec();
                        const bool mine = si == active;
                        ImGui::PushID(si);
                        if (ImGui::TreeNodeEx((other.name + (mine ? "  (flying)" : "")).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                        {
                            if (ImGui::Selectable("External", mine && chase_now))
                                pick(si, false, false, -1, -1, 0);
                            if (spec.cockpit.present && ImGui::Selectable("Cockpit (2D panel)", mine && cockpit_view && !cockpit3d && interior_cam < 0 && base_view < 0))
                                pick(si, true, false, -1, -1, 0);
                            if (spec.cockpit.present && spec.cockpit.mfds && ImGui::Selectable("Cockpit (3D, MFDs)", mine && cockpit_view && cockpit3d && interior_cam < 0 && base_view < 0))
                                pick(si, true, true, -1, -1, 0);
                            for (std::size_t ci = 0; ci < spec.cameras.size(); ++ci)
                                if (ImGui::Selectable(spec.cameras[ci].name.c_str(), mine && interior_cam == static_cast<int>(ci) && base_view < 0))
                                    pick(si, false, false, static_cast<int>(ci), -1, 0);
                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    }
                    for (std::size_t bi = 0; bi < sim.bases.size(); ++bi)
                    {
                        const kosmos::Base &bs = sim.bases[bi];
                        if (bs.cameras.empty())
                            continue;
                        ImGui::PushID(1000 + static_cast<int>(bi));
                        if (ImGui::TreeNodeEx((bs.name + " base").c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                        {
                            for (std::size_t ci = 0; ci < bs.cameras.size(); ++ci)
                                if (ImGui::Selectable(bs.cameras[ci].name.c_str(), base_view == static_cast<int>(bi) && base_view_cam == static_cast<int>(ci)))
                                    pick(-1, false, false, -1, static_cast<int>(bi), static_cast<int>(ci));
                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    }
                    ImGui::Separator();
                    ImGui::TextDisabled("C cycles the flown ship's views. Right-drag looks around.");
                }
                ImGui::End();
            }
        }
        if (sl::gui_available())
            menu_bar.draw(sim, menu_windows, menu_actions);
        sl::gui_render();
        if (show_info)
            sl::draw_renderer_label();

        if (!options.shot.empty() && !shot_saved && clock >= options.shot_at)
        {
            sl::save_bitmap(sl::screen, options.shot);
            shot_saved = true;
            running = false;
        }
        if (screenshot_requested)
        {
            screenshot_requested = false;
            const std::string file = "kosmos-" + std::to_string(sl::time_ms()) + ".png";
            sl::save_bitmap(sl::screen, file);
            sim.log("Saved " + file);
        }
        sl::show_video_bitmap();
        sl::end_frame();
    }

    netplay.leave();
    netplay.lobby_disconnect();
    settings_save(sim.props);
    sl::gui_shutdown();
    sound.stop();
    vis.clear_parts();
    leave_system();
    for (sl::Mesh *m : vis.limb_pool)
        sl::destroy_mesh(m);
    for (sl::Mesh *m : {vis.sphere, vis.glow, vis.quad, vis.starfield, vis.tail})
        sl::destroy_mesh(m);
    sl::destroy_bitmap(vis.lights_texture);
    sl::destroy_bitmap(vis.glow_texture);
    for (sl::Bitmap *b : {vis.star_surface, vis.star_filaments, vis.star_corona, vis.star_corona_far, vis.star_rays, vis.star_limb, vis.sky})
        sl::destroy_bitmap(b);
    sl::destroy_bitmap(vis.puff_texture);
    sl::destroy_bitmap(vis.cloud_texture);
    sl::destroy_surface_3d(surface);
    sl::shutdown();
    return 0;
}
