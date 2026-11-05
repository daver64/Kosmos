#include "app_state.h"

namespace kosmos::app
{
    // Wall panels in a building the astronaut is inside show its interface, drawn live into a texture.
    void App::draw_building_panel_target()
    {
        if (!(in_eva && eva.building >= 0 && eva.building < sim.ship_count() && sim.ship(eva.building)->spec().interior &&
              !sim.ship(eva.building)->spec().interior->panels().empty() && !sim.ship(eva.building)->spec().cockpit.controls.empty()))
            return;
        const Ship &bld = *sim.ship(eva.building);
        render_panel_target(resolved_cockpit(bld.spec().cockpit, eva.building), bld.spec().directory);
    }

    void App::render_panel_target(const kosmos::Cockpit &pcp, const std::string &directory)
    {
        const int wanted = static_cast<int>(std::ceil(panel_target_width * pcp.panel_height / pcp.panel_width));
        if (!targets.panel_target || wanted != targets.panel_target_height)
            targets.panel_target = sl::create_render_target(panel_target_width, targets.panel_target_height = wanted);
        if (targets.panel_target)
            targets.panel_target->smooth = true;
        if (targets.panel_target && sl::begin_render_target(targets.panel_target))
        {
            sl::clear_render_target({18, 20, 26, 255});
            draw_panel(sim, pcp, pcp.panel_image.empty() ? "" : (std::filesystem::path(directory) / pcp.panel_image).string(), 0.0f, 0.0f,
                       static_cast<float>(panel_target_width) / pcp.panel_width);
            sl::end_render_target();
        }
    }

    int App::screen_supersample() const
    {
        const int f = props_ref->get_int("/sim/settings/gfx-screen-ss", 2);
        return f >= 4 ? 4 : f >= 2 ? 2 : 1;
    }

    // The scratch target the MFD and HUD are drawn into at `factor` times their size; made again when the factor changes.
    sl::Bitmap *App::supersample_target(int factor)
    {
        if (factor != targets.ss_factor)
        {
            if (targets.ss_big)
                sl::destroy_bitmap(targets.ss_big);
            if (targets.ss_mid)
                sl::destroy_bitmap(targets.ss_mid);
            targets.ss_big = targets.ss_mid = nullptr;
            targets.ss_factor = factor;
            // Even at 1x the scratch target is used, so the final blit puts every backend's render target the right way up.
            targets.ss_big = sl::create_render_target(mfd_target_size * factor, mfd_target_size * factor);
            if (targets.ss_big)
                targets.ss_big->smooth = true;
            if (factor > 2)
            {
                targets.ss_mid = sl::create_render_target(mfd_target_size * 2, mfd_target_size * 2);
                if (targets.ss_mid)
                    targets.ss_mid->smooth = true;
            }
        }
        return targets.ss_big;
    }

    // Halves the image in steps, so each bilinear tap is an exact box filter.
    void App::downsample_target(sl::Bitmap *source, sl::Bitmap *destination, sl::Colour clear, bool invert)
    {
        const auto blit = [&](sl::Bitmap *from, sl::Bitmap *to, bool flip) {
            if (!sl::begin_render_target(to))
                return;
            sl::clear_render_target(clear);
            sl::draw_sprite_region_flipped(from, 0, 0, from->width, from->height, 0.0f, 0.0f, static_cast<float>(to->width), static_cast<float>(to->height), false, flip);
            sl::end_render_target();
        };
        // OpenGL targets are stored bottom-up, so a flipped sprite is a plain copy there; on Vulkan and Direct3D it is a real flip,
        // and the single final one is what turns a top-down target the right way up for the cockpit meshes.
        if (targets.ss_mid && source != targets.ss_mid && source->width > destination->width * 2)
        {
            blit(source, targets.ss_mid, sl::graphics_backend() == sl::GraphicsBackend::opengl);
            source = targets.ss_mid;
        }
        blit(source, destination, !invert);
    }

    void App::draw_cockpit_targets()
    {
        if (!in_cockpit3d)
            return;
        sim.mfd.bus_power = !sim.active_ship() || !sim.active_ship()->systems() || sim.active_ship()->systems()->instruments_powered();
        sim.mfd.update(frame);
        const kosmos::Cockpit pcp = resolved_cockpit(ship->spec().cockpit, props_ref->get_int("/sim/active-ship"));
        if ((pcp.instruments.empty() && pcp.controls.empty()) || !pcp.vc_model.empty())
        {
            const int factor = screen_supersample();
            sl::Bitmap *big = supersample_target(factor);
            for (int side = 0; side < 2; ++side)
                if (big)
                {
                    kosmos::MfdLayout layout = mfd_target_layout(!pcp.vc_model.empty());
                    layout.x *= factor;
                    layout.y *= factor;
                    layout.size *= factor;
                    sim.mfd.draw_to_target(side, big, layout);
                    downsample_target(big, targets.mfd_target[side], {18, 20, 26, 255}, pcp.vc_mfd_flip);
                }
                else
                    sim.mfd.draw_to_target(side, targets.mfd_target[side], mfd_target_layout(!pcp.vc_model.empty()));
            if (!pcp.vc_model.empty())
            {
                for (const kosmos::Cockpit::VcInstrument &vi : pcp.vc_instruments)
                {
                    if (vi.unit < 2 || vi.unit >= 8 || vi.unit >= sim.mfd.unit_count())
                        continue;
                    sl::Bitmap *&t = targets.instr_target[vi.unit];
                    if (!t)
                        t = sl::create_render_target(mfd_target_size, mfd_target_size);
                    sl::Bitmap *into = big ? big : t;
                    if (!t || !sl::begin_render_target(into))
                        continue;
                    t->smooth = true;
                    sl::clear_render_target({10, 12, 16, 255});
                    kosmos::MfdLayout layout;
                    layout.bare = true;
                    layout.size = static_cast<float>(into->width);
                    sim.mfd.draw(vi.unit, layout);
                    sl::end_render_target();
                    if (big)
                        downsample_target(big, t, {10, 12, 16, 255});
                }
                if (targets.gauge_target.size() != pcp.vc_gauges.size())
                {
                    targets.gauge_target.assign(pcp.vc_gauges.size(), nullptr);
                    targets.gauge_scratch.assign(pcp.vc_gauges.size(), nullptr);
                }
                for (std::size_t gi = 0; gi < pcp.vc_gauges.size(); ++gi)
                {
                    const kosmos::Cockpit::VcGauge &vg = pcp.vc_gauges[gi];
                    sl::Bitmap *art = sim.mfd.bitmap((std::filesystem::path(ship->spec().directory) / vg.image).string());
                    if (!art)
                        continue;
                    const int gw = art->width * 2, gh = art->height * 2;
                    if (!targets.gauge_target[gi])
                    {
                        targets.gauge_target[gi] = sl::create_render_target(gw, gh);
                        targets.gauge_scratch[gi] = sl::create_render_target(gw, gh);
                    }
                    sl::Bitmap *scratch = targets.gauge_scratch[gi], *out = targets.gauge_target[gi];
                    if (!scratch || !out || !sl::begin_render_target(scratch))
                        continue;
                    scratch->smooth = out->smooth = true;
                    sl::clear_render_target({0, 0, 0, 255});
                    sl::draw_sprite_region_flipped(art, 0, 0, art->width, art->height, 0.0f, 0.0f, static_cast<float>(gw), static_cast<float>(gh), false, false);
                    for (const kosmos::Cockpit::PanelControl &c : vg.controls)
                        draw_panel_control(sim, c, 0.0f, 0.0f, 2.0f);
                    sl::end_render_target();
                    if (sl::begin_render_target(out))
                    {
                        sl::clear_render_target({0, 0, 0, 255});
                        sl::draw_sprite_region_flipped(scratch, 0, 0, gw, gh, 0.0f, 0.0f, static_cast<float>(gw), static_cast<float>(gh), false, true);
                        sl::end_render_target();
                    }
                }
                render_vc_hud_target();
            }
        }
        else
            render_panel_target(pcp, ship->spec().directory);
    }


    // Out in space the planets and the ships are drawn with separate near planes; this picks them.
    void App::setup_depth_layers()
    {
        kosmos::Universe &universe = *universe_ref;
        const kosmos::Body &soi_body = *soi_body_ref;
        // As Orbiter does: out in space the planets are drawn first with a near plane set by the nearest body, then the depth buffer
        // is cleared and the ships go over them with their own near plane, so neither range has to share one depth buffer.
        layered = depth_layers_work && !in_interior && !in_base_cam && !in_user_mounted && eye_soi_altitude > 20.0e3;
        near_plane = ship_near;
        if (layered)
        {
            double nearest_surface = 1.0e30;
            for (std::size_t bi = 0; bi < universe.bodies.size(); ++bi)
                nearest_surface = std::min(nearest_surface, glm::length(universe.position(static_cast<int>(bi)) - eye_world) - universe.bodies[bi].radius);
            // Cloud decks and layers sit well above the surface and the near plane cuts across the view, so off-axis cloud would be
            // clipped if the plane sat at the ground distance; leave room for the clouds and a wide view.
            const double cloud_room = near_earth ? 12.0e3 : 0.0;
            // Capped so a star-sized distance cannot push the near plane past the far one.
            near_plane = std::max(ship_near, std::min(0.6 * (nearest_surface - cloud_room), 1.0e8));
            // The near plane is flat across the view, so ground straight below is nearer to it the more the camera looks toward the
            // horizon. Pull the plane in to match, or the cloud shell is sliced along a line across the view.
            if (near_earth)
            {
                const glm::dvec3 forward = glm::normalize(sl::dvec3(ship_rel));
                const double looking_down = std::acos(std::clamp(glm::dot(forward, -glm::normalize(eye_from_earth)), -1.0, 1.0));
                const double half_view = std::atan(std::tan(view_fov * 0.5 * 3.14159265358979 / 180.0) * 1.9);
                const double cos_worst = std::cos(std::min(looking_down + half_view, 1.45));
                if (cos_worst < 0.55)
                    near_plane = std::max(ship_near, near_plane * cos_worst / 0.55);
            }
        }
        shell_clearance = 1.8 * near_plane;
        if (layered && near_earth)
        {
            const glm::dvec3 forward = glm::normalize(sl::dvec3(ship_rel));
            const double looking_down = std::acos(std::clamp(glm::dot(forward, -glm::normalize(eye_from_earth)), -1.0, 1.0));
            const double half_view = std::atan(std::tan(view_fov * 0.5 * 3.14159265358979 / 180.0) * 1.9);
            shell_clearance = std::max(1.8 * near_plane, 1.1 * near_plane / std::max(std::cos(std::min(looking_down + half_view, 1.45)), 0.1));
        }
        camera.near_plane = static_cast<float>(near_plane);
        camera.far_plane = static_cast<float>(layered || view_distance * 0.3 > 1.0e3 ? 1.0e10 : near_earth ? std::max(view.far_distance, soi_body.radius * 3.0) : std::max(soi_body.radius * 8.0, 6.0e8));
        ship_camera = camera;
        ship_camera.near_plane = static_cast<float>(ship_near);
        ship_camera.far_plane = 1.0e8f;
    }

    void App::setup_environment()
    {
        const kosmos::Telemetry &tm = *tm_ref;
        sl::PropertyTree &props = *props_ref;
        environment = sl::Environment3D();
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
        if (near_earth && props.get_bool("/sim/settings/weather", true))
        {
            const double visibility = props.get_double("/environment/visibility", 1.0e5);
            if (visibility > 50.0 && visibility < 20000.0)
                environment.fog_density = std::max(environment.fog_density, static_cast<float>(3.0 / visibility));
        }

        setup_cloud_layers();
        setup_local_sky();
    }

    // Flat or volumetric cloud layers in the sky from the weather's layers; cloud_veil is how far the eye is inside thick cloud.
    void App::setup_cloud_layers()
    {
        sl::PropertyTree &props = *props_ref;
        cloud_veil = 0.0;
        const int style = props.get_int("/sim/settings/gfx-cloud-style");
        if (!near_earth || style == 0 || !gfx_clouds || options.no_clouds || !props.get_bool("/sim/settings/weather", true))
            return;
        const int count = std::min(props.get_int("/environment/cloud-layer-count"), 4);
        const double agl = ship->telemetry().altitude;
        const double ground = environment.camera_altitude - agl;
        const double wind_e = props.get_double("/environment/wind-east");
        const double wind_n = props.get_double("/environment/wind-north");
        for (int i = 0; i < count; ++i)
        {
            const std::string p = "/environment/cloud-layer-" + std::to_string(i) + "/";
            const double base = props.get_double(p + "base"), top = props.get_double(p + "top"), cover = props.get_double(p + "cover");
            const int type = std::clamp(props.get_int(p + "type"), 0, 5);
            sl::CloudLayer layer = sl::cloud_layer(static_cast<sl::CloudType>(type));
            // Far above a layer it is seen edge-on at the horizon as streaks, and the planet's own cloud map shows the weather from up there, so the layer fades out above its top.
            const double above = environment.camera_altitude - (ground + top);
            const double visible = 1.0 - fx::smoothstep(3000.0, 20000.0, above);
            layer.enabled = cover * visible > 0.04;
            layer.altitude = static_cast<float>(ground + base);
            layer.thickness = static_cast<float>(std::max(100.0, top - base));
            layer.coverage = static_cast<float>(std::clamp(cover * visible, 0.0, 1.0));
            layer.wind = glm::vec2(static_cast<float>(wind_e), static_cast<float>(wind_n));
            layer.volumetric = style == 2 && (type == 0 || type == 1 || type == 5);
            (i == 0 ? environment.clouds : environment.more_clouds[i - 1]) = layer;
            if (layer.enabled && agl > base - 100.0 && agl < top + 100.0)
            {
                const double edge = std::min({1.0, (agl - base + 100.0) / 200.0, (top + 100.0 - agl) / 200.0});
                cloud_veil = std::max(cloud_veil, edge * std::clamp(cover * 1.2, 0.0, 1.0) * (type == 5 ? 1.0 : 0.7));
            }
        }
    }

    void App::setup_local_sky()
    {
        kosmos::Universe &universe = *universe_ref;
        const kosmos::Telemetry &tm = *tm_ref;
        const kosmos::Body &soi_body = *soi_body_ref;
        local_sky = !near_earth && !home && soi_body.kind != kosmos::BodyKind::star && soi_body.atmosphere_density > 0.001 &&
                    eye_soi_altitude < soi_body.atmosphere_scale_height * 14.0;
        if (local_sky)
        {
            // The sky takes the colour the body's atmosphere is given: thin dusty air scatters the whole spectrum, tinted towards that colour.
            const sl::dvec3 from_centre = eye_world - universe.position(tm.soi);
            const double density = soi_body.atmosphere_density;
            const float strength = static_cast<float>(std::clamp(std::pow(density, 0.35), 0.15, 3.0));
            const glm::vec3 tint(soi_body.atmosphere_colour[0], soi_body.atmosphere_colour[1], soi_body.atmosphere_colour[2]);
            environment.planet = true;
            environment.planet_radius = static_cast<float>(soi_body.radius);
            environment.atmosphere_height = static_cast<float>(soi_body.atmosphere_scale_height * 14.0);
            environment.rayleigh = tint * tint * 1.6e-4f * strength;
            environment.rayleigh_scale_height = static_cast<float>(soi_body.atmosphere_scale_height);
            environment.mie = 3.0e-5f * strength;
            environment.mie_scale_height = static_cast<float>(soi_body.atmosphere_scale_height * 0.2);
            environment.up = glm::vec3(glm::normalize(from_centre));
            environment.camera_altitude = static_cast<float>(eye_soi_altitude);
            environment.camera_position = from_centre;
            environment.ground_albedo = tint * 0.3f;
            environment.fog_enabled = true;
            environment.fog_density = 1.0e-4f * strength;
        }
    }

    // The 3D scene.
    void App::draw_world()
    {
        setup_depth_layers();
        setup_environment();
        draw_building_panel_target();
        draw_cockpit_targets();
        if (eva.active)
            arsenal.render_screen(sim);
        if (sl::begin_surface_3d(surface, camera))
        {
            sl::clear_surface_3d({0, 0, 0, 255});
            sl::set_environment_3d(environment);
            scene_lighting();
            scene_sky();
            scene_star_glare();
            scene_bodies();
            scene_ships();
            scene_weather();
            scene_lightning();
            scene_lamp_bulbs();
            scene_exhaust_and_held();
            sl::end_surface_3d();
        }
        sl::clear_to_colour(sl::screen, {0, 0, 0});
        sl::draw_sprite(sl::surface_3d_bitmap(surface), 0, 0);
        if (map_mode)
            map.draw(*universe_ref, *ship, *tm_ref);

        view_proj = camera.projection_matrix(static_cast<float>(screen_w) / screen_h) * camera.view_matrix();
    }
} // namespace kosmos::app
