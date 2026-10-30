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
        if (!panel_target || wanted != panel_target_height)
            panel_target = sl::create_render_target(panel_target_width, panel_target_height = wanted);
        if (panel_target)
            panel_target->smooth = true;
        if (panel_target && sl::begin_render_target(panel_target))
        {
            sl::clear_render_target({18, 20, 26, 255});
            draw_panel(sim, pcp, pcp.panel_image.empty() ? "" : (std::filesystem::path(directory) / pcp.panel_image).string(), 0.0f, 0.0f,
                       static_cast<float>(panel_target_width) / pcp.panel_width);
            sl::end_render_target();
        }
    }

    void App::draw_cockpit_targets()
    {
        if (!in_cockpit3d)
            return;
        sim.mfd.bus_power = !sim.active_ship() || !sim.active_ship()->systems() || sim.active_ship()->systems()->instruments_powered();
        sim.mfd.update(frame);
        const kosmos::Cockpit pcp = resolved_cockpit(ship->spec().cockpit, props_ref->get_int("/sim/active-ship"));
        if (pcp.instruments.empty() && pcp.controls.empty())
        {
            for (int side = 0; side < 2; ++side)
                sim.mfd.draw_to_target(side, mfd_target[side], mfd_target_layout());
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
            // Capped so a star-sized distance cannot push the near plane past the far one.
            near_plane = std::max(ship_near, std::min(0.8 * nearest_surface, 1.0e8));
        }
        camera.near_plane = static_cast<float>(near_plane);
        camera.far_plane = static_cast<float>(layered || view_distance * 0.3 > 1.0e3 ? 1.0e10 : near_earth ? std::max(view.far_distance, soi_body.radius * 3.0) : std::max(soi_body.radius * 8.0, 6.0e8));
        ship_camera = camera;
        ship_camera.near_plane = static_cast<float>(ship_near);
        ship_camera.far_plane = 1.0e8f;
    }

    void App::setup_environment()
    {
        kosmos::Universe &universe = *universe_ref;
        sl::PropertyTree &props = *props_ref;
        const kosmos::Telemetry &tm = *tm_ref;
        const kosmos::Body &soi_body = *soi_body_ref;
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

        setup_local_sky();
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
