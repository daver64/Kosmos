#include <cstdio>
#include "app_state.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>

// Science instruments on the flown ship: cameras (visual, infra-red, ultra-violet), radar and a spectrometer. The cameras are not Kosmos
// cameras: each frame is a second, small render of the 3D scene from the instrument's mount, drawn before the player's own frame so the
// player's camera state is simply rebuilt afterwards. The Instrument MFD (addons/stockmfd/instrument.lua) shows the result.

namespace kosmos::app
{
    namespace
    {
        double wall_seconds()
        {
            return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        // Warm "ironbow" palette for the infra-red band: 0 is cold (black to violet), 1 is hot (white).
        void thermal_colour(double t, std::uint8_t &r, std::uint8_t &g, std::uint8_t &b)
        {
            static const double stops[6][3] = {{0, 0, 0}, {60, 0, 110}, {190, 30, 60}, {250, 120, 0}, {255, 220, 40}, {255, 255, 235}};
            t = std::clamp(t, 0.0, 1.0) * 5.0;
            const int i = std::min(static_cast<int>(t), 4);
            const double f = t - i;
            r = static_cast<std::uint8_t>(stops[i][0] + (stops[i + 1][0] - stops[i][0]) * f);
            g = static_cast<std::uint8_t>(stops[i][1] + (stops[i + 1][1] - stops[i][1]) * f);
            b = static_cast<std::uint8_t>(stops[i][2] + (stops[i + 1][2] - stops[i][2]) * f);
        }

        // Turns the visual picture into the band the instrument sees. Thermal: land and rock are warm, sea is cold, cloud tops are cold even
        // though they are bright. Ultra-violet: scattering brightens the blue, so air and cloud glow while dark ground fades.
        void apply_band(const std::string &type, std::vector<std::uint8_t> &px)
        {
            if (type == "visual")
                return;
            for (std::size_t o = 0; o + 3 < px.size(); o += 4)
            {
                const double r = px[o] / 255.0, g = px[o + 1] / 255.0, b = px[o + 2] / 255.0;
                const double lum = 0.3 * r + 0.55 * g + 0.15 * b;
                if (type == "ir")
                {
                    const double hi = std::max({r, g, b}), lo = std::min({r, g, b});
                    double warm = 0.18 + 0.75 * std::clamp(r - b + 0.25, 0.0, 1.0) * 0.8 + 0.45 * lum;
                    if (hi - lo < 0.18 && lum > 0.55)
                        warm *= 0.45;
                    if (hi < 0.02)
                        warm = 0.0;
                    thermal_colour(warm, px[o], px[o + 1], px[o + 2]);
                }
                else
                {
                    const double uv = std::clamp(std::pow(0.15 * r + 0.3 * g + 0.75 * b, 1.35) * 1.5, 0.0, 1.0);
                    px[o] = static_cast<std::uint8_t>(255 * uv * 0.62);
                    px[o + 1] = static_cast<std::uint8_t>(255 * uv * 0.32);
                    px[o + 2] = static_cast<std::uint8_t>(255 * uv);
                }
            }
        }

        std::string sanitize(std::string name)
        {
            for (char &c : name)
                if (!std::isalnum(static_cast<unsigned char>(c)))
                    c = '_';
            return name;
        }

        struct Absorption
        {
            double centre, depth, width;
        };

        // What a body looks like to a spectrometer: a reflectance slope and the dips its gases and minerals cut in it, in micrometres.
        struct Composition
        {
            std::string species;
            std::vector<Absorption> bands;
            double uv_drop = 0.6, nir_slope = 1.1;
        };

        Composition composition_of(const kosmos::Body &b)
        {
            Composition c;
            const std::string &n = b.name;
            auto add = [&](double centre, double depth, double width) { c.bands.push_back({centre, depth, width}); };
            if (b.kind == kosmos::BodyKind::star)
            {
                c.species = "photosphere";
                return c;
            }
            if (n == "Earth")
            {
                c.species = "0.76 O2;0.94 H2O;1.13 H2O;1.40 H2O;1.90 H2O;2.01 CO2;0.33 O3";
                add(0.76, 0.5, 0.01);
                add(0.94, 0.3, 0.03);
                add(1.13, 0.35, 0.04);
                add(1.40, 0.8, 0.07);
                add(1.90, 0.85, 0.09);
                add(2.01, 0.3, 0.02);
                add(0.33, 0.7, 0.05);
                c.uv_drop = 1.3;
            }
            else if (n == "Mars")
            {
                c.species = "0.9 Fe3+;1.43 H2O;2.0 CO2;2.7 CO2;4.3 CO2";
                add(0.9, 0.25, 0.08);
                add(1.43, 0.2, 0.05);
                add(2.0, 0.55, 0.03);
                add(2.7, 0.8, 0.12);
                add(4.3, 0.9, 0.15);
                c.uv_drop = 0.35;
                c.nir_slope = 1.5;
            }
            else if (n == "Venus")
            {
                c.species = "0.35 SO2;1.6 CO2;2.0 CO2;2.7 CO2;4.3 CO2";
                add(0.35, 0.7, 0.06);
                add(1.6, 0.3, 0.04);
                add(2.0, 0.5, 0.03);
                add(2.7, 0.8, 0.1);
                add(4.3, 0.9, 0.15);
                c.uv_drop = 0.5;
            }
            else if (n == "Titan" || n == "Jupiter" || n == "Saturn" || n == "Uranus" || n == "Neptune")
            {
                c.species = "0.89 CH4;1.15 CH4;1.38 CH4;1.7 CH4;2.3 CH4;3.3 CH4";
                add(0.89, 0.55, 0.02);
                add(1.15, 0.45, 0.03);
                add(1.38, 0.7, 0.04);
                add(1.7, 0.6, 0.05);
                add(2.3, 0.85, 0.08);
                add(3.3, 0.95, 0.15);
                if (n == "Uranus" || n == "Neptune")
                    add(0.65, 0.7, 0.18);
                c.nir_slope = n == "Titan" ? 1.4 : 0.8;
            }
            else if (n == "Europa" || n == "Enceladus" || n == "Ganymede" || n == "Callisto" || n == "Triton" || n == "Pluto" || b.kind == kosmos::BodyKind::comet)
            {
                c.species = "1.04 H2O ice;1.25 H2O ice;1.5 H2O ice;2.0 H2O ice;3.0 H2O ice";
                add(1.04, 0.2, 0.03);
                add(1.25, 0.25, 0.04);
                add(1.5, 0.6, 0.07);
                add(2.0, 0.75, 0.09);
                add(3.0, 0.95, 0.25);
                c.nir_slope = 0.6;
            }
            else if (b.atmosphere_density > 0.05)
            {
                c.species = "2.0 CO2;2.7 CO2;4.3 CO2";
                add(2.0, 0.5, 0.03);
                add(2.7, 0.8, 0.1);
                add(4.3, 0.9, 0.15);
            }
            else
            {
                c.species = "0.95 pyroxene;1.9 pyroxene;2.8 OH";
                add(0.95, 0.2, 0.1);
                add(1.9, 0.15, 0.2);
                add(2.8, 0.35, 0.15);
                c.nir_slope = 1.35;
            }
            return c;
        }

        // The first body the ray from `from` along `dir` hits; returns its index or -1, with the distance and the hit point.
        int ray_body(const kosmos::Universe &universe, const sl::dvec3 &from, const sl::dvec3 &dir, double &distance)
        {
            int best = -1;
            double best_t = 1.0e30;
            for (std::size_t i = 0; i < universe.bodies.size(); ++i)
            {
                const sl::dvec3 oc = from - universe.position(static_cast<int>(i));
                const double bq = glm::dot(oc, dir);
                const double cq = glm::dot(oc, oc) - universe.bodies[i].radius * universe.bodies[i].radius;
                const double disc = bq * bq - cq;
                if (disc < 0.0)
                    continue;
                const double t = -bq - std::sqrt(disc);
                if (t > 0.0 && t < best_t)
                {
                    best_t = t;
                    best = static_cast<int>(i);
                }
            }
            distance = best_t;
            return best;
        }
    } // namespace

    sl::Surface3D *App::instrument_surface(int size)
    {
        auto found = instrument_surfaces.find(size);
        if (found == instrument_surfaces.end())
        {
            sl::Surface3D *s = sl::create_surface_3d(size, size, 1);
            found = instrument_surfaces.emplace(size, s).first;
        }
        return found->second;
    }

    // Draws the scene from the instrument's mount into `target`, then restores nothing: update_instruments() runs before the player's
    // camera is placed each frame, which rebuilds every camera member this touches.
    bool App::render_instrument_scene(std::size_t index, sl::Surface3D *target)
    {
        if (!target || !target->bitmap || first_frame || !ship || index >= ship->spec().instruments.size())
            return false;
        kosmos::Universe &universe = *universe_ref;
        const kosmos::Telemetry &tm = *tm_ref;
        const kosmos::ShipSpec::Instrument &spec = ship->spec().instruments[index];
        sl::FlightBody &fb = ship->body();

        eye_world = ship->instrument_position(index);
        const sl::dvec3 forward = ship->instrument_direction(index);
        ship_rel = glm::vec3(forward * 1000.0);
        up = fb.direction_to_world(sl::dvec3(0.0, 1.0, 0.0));
        if (std::abs(glm::dot(up, forward)) > 0.95)
            up = fb.direction_to_world(sl::dvec3(1.0, 0.0, 0.0));
        view_fov = spec.fov;
        soi = tm.soi;
        soi_body_ref = &universe.bodies[static_cast<std::size_t>(soi)];
        soi_pos = universe.position(soi);
        set_terrain_view();
        view.aspect = static_cast<double>(target->bitmap->width) / target->bitmap->height;
        view.viewport_height = target->bitmap->height;
        update_terrains();

        to_sun = glm::vec3(glm::normalize(-eye_world));
        camera = sl::Camera();
        camera.position = glm::vec3(0.0f);
        camera.target = ship_rel;
        camera.up = glm::vec3(up);
        camera.field_of_view = static_cast<float>(view_fov);
        eye_soi_altitude = glm::length(eye_world - soi_pos) - soi_body_ref->radius;
        view_distance = 1.0e9;
        in_user_mounted = true;
        ship_near = 1.0;
        setup_depth_layers();
        setup_environment();
        if (!sl::begin_surface_3d(target, camera))
            return false;
        sl::clear_surface_3d({0, 0, 0, 255});
        sl::set_environment_3d(environment);
        scene_lighting();
        scene_sky();
        scene_star_glare();
        scene_bodies();
        const bool was_cockpit = in_cockpit;
        in_cockpit = true; // the instrument sits inside the hull: don't draw the vessel around it
        scene_ships();
        in_cockpit = was_cockpit;
        sl::end_surface_3d();
        return true;
    }

    // A side-looking-style radar picture: heights under the beam from the body's terrain, lit by their slope along the beam, with speckle.
    void App::make_radar_image(std::size_t index, int size, sl::Bitmap *out)
    {
        kosmos::Universe &universe = *universe_ref;
        const kosmos::ShipSpec::Instrument &spec = ship->spec().instruments[index];
        const sl::dvec3 eye = ship->instrument_position(index);
        const sl::dvec3 forward = ship->instrument_direction(index);
        sl::dvec3 hint = ship->body().direction_to_world(sl::dvec3(0.0, 1.0, 0.0));
        if (std::abs(glm::dot(hint, forward)) > 0.95)
            hint = ship->body().direction_to_world(sl::dvec3(1.0, 0.0, 0.0));
        const sl::dvec3 right = glm::normalize(glm::cross(forward, hint));
        const sl::dvec3 upv = glm::cross(right, forward);
        const double half = std::tan(glm::radians(spec.fov) * 0.5);

        std::vector<double> height(static_cast<std::size_t>(size) * size, std::nan("")), range(height.size(), 0.0);
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                const double u = ((x + 0.5) / size * 2.0 - 1.0) * half, v = (1.0 - (y + 0.5) / size * 2.0) * half;
                const sl::dvec3 dir = glm::normalize(forward + right * u + upv * v);
                double t = 0.0;
                const int bi = ray_body(universe, eye, dir, t);
                if (bi < 0)
                    continue;
                const kosmos::Body &b = universe.bodies[static_cast<std::size_t>(bi)];
                const sl::Terrain *terr = nullptr;
                for (const std::unique_ptr<BodyTerrain> &bt : body_terrains)
                    if (bt->body == bi)
                        terr = bt->terrain.get();
                double h = 0.0;
                if (terr)
                    for (int pass = 0; pass < 2; ++pass)
                    {
                        const sl::dvec3 centre = universe.position(bi);
                        const sl::dvec3 hit = eye + dir * t - centre;
                        h = terr->height_at_direction(glm::normalize(hit));
                        const sl::dvec3 oc = eye - centre;
                        const double r = b.radius + h;
                        const double bq = glm::dot(oc, dir), cq = glm::dot(oc, oc) - r * r;
                        const double disc = bq * bq - cq;
                        if (disc >= 0.0)
                            t = -bq - std::sqrt(disc);
                    }
                height[static_cast<std::size_t>(y) * size + x] = h;
                range[static_cast<std::size_t>(y) * size + x] = t;
            }

        out->pixels.assign(static_cast<std::size_t>(size) * size * 4, 255);
        std::uint32_t seed = 12345u;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                const std::size_t i = static_cast<std::size_t>(y) * size + x;
                double v = 0.0;
                if (!std::isnan(height[i]))
                {
                    const double hn = x + 1 < size && !std::isnan(height[i + 1]) ? height[i + 1] : height[i];
                    const double hs = y + 1 < size && !std::isnan(height[i + size]) ? height[i + size] : height[i];
                    const double footprint = std::max(range[i] * 2.0 * half / size, 1.0);
                    const double slope = ((height[i] - hn) + 0.5 * (height[i] - hs)) / footprint;
                    v = std::clamp(0.45 + slope * 1.3, 0.05, 1.0);
                    seed = seed * 1664525u + 1013904223u;
                    v *= 0.75 + 0.5 * ((seed >> 16) & 0xff) / 255.0;
                }
                v = std::clamp(v, 0.0, 1.0);
                std::uint8_t *p = &out->pixels[i * 4];
                p[0] = static_cast<std::uint8_t>(40 * v);
                p[1] = static_cast<std::uint8_t>(255 * v);
                p[2] = static_cast<std::uint8_t>(110 * v);
                p[3] = 255;
            }
        out->ram_dirty = true;
    }

    // Publishes what the instrument is pointing at, and for a spectrometer the spectrum of the target.
    void App::update_spectrum(std::size_t index)
    {
        kosmos::Universe &universe = *universe_ref;
        sl::PropertyTree &props = *props_ref;
        double t = 0.0;
        const int bi = ray_body(universe, ship->instrument_position(index), ship->instrument_direction(index), t);
        if (bi < 0)
        {
            props.set_string("/sim/instrument/target-name", "");
            props.set_double("/sim/instrument/target-range-km", 0.0);
            props.set_int("/sim/instrument/spectrum/n", 0);
            props.set_string("/sim/instrument/species", "");
            return;
        }
        const kosmos::Body &b = universe.bodies[static_cast<std::size_t>(bi)];
        props.set_string("/sim/instrument/target-name", b.name);
        props.set_double("/sim/instrument/target-range-km", t / 1000.0);
        const Composition comp = composition_of(b);
        props.set_string("/sim/instrument/species", comp.species);
        constexpr int points = 96;
        props.set_int("/sim/instrument/spectrum/n", points);
        for (int k = 0; k < points; ++k)
        {
            const double lambda = 0.3 + 4.7 * k / (points - 1);
            // The Sun's light, roughly a 5800 K blackbody, times the body's reflectance.
            const double x = 14388.0 / (lambda * 5800.0);
            const double sun = std::pow(lambda, -5.0) / (std::exp(x) - 1.0);
            const double sun_peak = std::pow(0.5, -5.0) / (std::exp(14388.0 / (0.5 * 5800.0)) - 1.0);
            double refl;
            if (b.kind == kosmos::BodyKind::star)
                refl = 1.0;
            else
            {
                const double cr = b.colour[0], cg = b.colour[1], cb = b.colour[2];
                if (lambda < 0.45)
                    refl = cb * (lambda < 0.4 ? comp.uv_drop + (1.0 - comp.uv_drop) * (lambda - 0.3) / 0.1 : 1.0);
                else if (lambda < 0.6)
                    refl = cb + (cg - cb) * (lambda - 0.45) / 0.15;
                else if (lambda < 0.7)
                    refl = cg + (cr - cg) * (lambda - 0.6) / 0.1;
                else
                    refl = cr * (1.0 + (comp.nir_slope - 1.0) * std::min((lambda - 0.7) / 1.0, 1.0));
                refl = std::clamp(refl * 1.4, 0.02, 0.95);
                for (const Absorption &a : comp.bands)
                {
                    const double d = (lambda - a.centre) / a.width;
                    refl *= 1.0 - a.depth * std::exp(-d * d);
                }
            }
            const double v = std::clamp(refl * sun / sun_peak, 0.0, 1.5);
            props.set_double("/sim/instrument/spectrum/v[" + std::to_string(k) + "]", v);
        }
    }

    void App::update_instruments()
    {
        sl::PropertyTree &props = *props_ref;
        if (!ship || ship->spec().instruments.empty())
        {
            if (props.get_int("/sim/instrument/count") != 0)
                props.set_int("/sim/instrument/count", 0);
            return;
        }
        const std::size_t count = ship->spec().instruments.size();
        props.set_int("/sim/instrument/count", static_cast<int>(count));
        const std::size_t sel = static_cast<std::size_t>(std::clamp(props.get_int("/sim/instrument/selected", 0), 0, static_cast<int>(count) - 1));
        const kosmos::ShipSpec::Instrument &spec = ship->spec().instruments[sel];
        kosmos::Ship::InstrumentState &state = ship->instruments()[sel];
        const double now = wall_seconds();

        // The gallery is the folder of pictures this ship's instruments have taken, kept between sessions.
        const std::filesystem::path dir = std::filesystem::path(kosmos::Sim::user_dir()) / "instrument-shots" / sanitize(ship->name);
        if (instrument_gallery_ship != ship->name)
        {
            instrument_gallery_ship = ship->name;
            instrument_gallery.clear();
            std::error_code ec;
            for (const auto &entry : std::filesystem::directory_iterator(dir, ec))
                if (entry.path().extension() == ".png" || entry.path().extension() == ".csv")
                    instrument_gallery.push_back(entry.path().string());
            std::sort(instrument_gallery.begin(), instrument_gallery.end());
        }
        props.set_int("/sim/instrument/gallery/count", static_cast<int>(instrument_gallery.size()));
        for (std::size_t i = 0; i < instrument_gallery.size(); ++i)
            props.set_string("/sim/instrument/gallery/file[" + std::to_string(i) + "]", instrument_gallery[i]);

        static double last_tick = -1;
        static double tick_wall = -10.0;
        const double tick = props.get_double("/sim/instrument/view-tick", 0.0);
        if (tick != last_tick)
        {
            last_tick = tick;
            tick_wall = now;
        }
        const bool viewing = now - tick_wall < 0.6;
        const bool has_picture = spec.type != "spectrometer";
        const bool usable = state.powered && !state.shutter;
        if (viewing || state.pending)
            update_spectrum(sel);
        if (!usable)
        {
            sim.mfd.set_live_bitmap("instrument", nullptr);
            return;
        }

        const auto fill = [&](int size, sl::Bitmap *out) -> bool {
            if (spec.type == "radar")
            {
                make_radar_image(sel, size, out);
                return true;
            }
            sl::Surface3D *surf = instrument_surface(size);
            if (!render_instrument_scene(sel, surf))
                return false;
            sl::Bitmap *src = sl::surface_3d_bitmap(surf);
            if (!src || !sl::acquire_bitmap(src))
                return false;
            const std::size_t n = static_cast<std::size_t>(size) * size * 4;
            if (src->pixels.size() < n)
            {
                sl::release_bitmap(src);
                return false;
            }
            out->pixels.assign(src->pixels.begin(), src->pixels.begin() + static_cast<std::ptrdiff_t>(n));
            sl::release_bitmap(src);
            apply_band(spec.type, out->pixels);
            out->ram_dirty = true;
            return true;
        };

        // Live view: small and at a few frames a second, since it is a whole extra scene.
        if (viewing && has_picture && now - instrument_live_time > (spec.type == "radar" ? 0.5 : 0.2))
        {
            constexpr int live_size = 320;
            if (!instrument_live)
                instrument_live = sl::create_bitmap(live_size, live_size);
            if (instrument_live && fill(live_size, instrument_live))
            {
                instrument_live->smooth = true;
                sim.mfd.set_live_bitmap("instrument", instrument_live);
                instrument_live_time = now;
            }
        }

        if (state.pending && !first_frame)
        {
            state.pending = 0;
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            char stamp[32];
            const std::time_t t = std::time(nullptr);
            std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", std::localtime(&t));
            const std::string base = (dir / (sanitize(spec.id) + "-" + stamp + "-" + std::to_string(state.shots + 1))).string();
            bool saved = false;
            std::string file;
            if (has_picture)
            {
                sl::Bitmap *shot = sl::create_bitmap(spec.resolution, spec.resolution);
                if (shot && fill(spec.resolution, shot))
                {
                    file = base + ".png";
                    saved = sl::save_bitmap(shot, file);
                    if (!saved)
                        std::fprintf(stderr, "[instrument] could not save %s\n", file.c_str());
                }
                else
                    std::fprintf(stderr, "[instrument] render failed at %d\n", spec.resolution);
                if (shot)
                    sl::destroy_bitmap(shot);
            }
            else
            {
                file = base + ".csv";
                std::ofstream out(file);
                if (out)
                {
                    out << "# " << spec.name << " spectrum of " << props.get_string("/sim/instrument/target-name") << "\nwavelength_um,radiance\n";
                    const int n = props.get_int("/sim/instrument/spectrum/n");
                    for (int k = 0; k < n; ++k)
                        out << 0.3 + 4.7 * k / std::max(n - 1, 1) << "," << props.get_double("/sim/instrument/spectrum/v[" + std::to_string(k) + "]") << "\n";
                    saved = true;
                }
            }
            if (saved)
            {
                --state.frames;
                ++state.shots;
                instrument_gallery.push_back(file);
                props.set_string("/sim/instrument/last-shot", file);
                props.set_double("/sim/instrument/last-shot-time", now);
            }
        }
    }
} // namespace kosmos::app
