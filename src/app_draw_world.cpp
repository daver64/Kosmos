#include "app_state.h"

namespace kosmos::app
{
    // The 3D scene.
    void App::draw_world()
    {
    kosmos::Universe &universe = *universe_ref;
    sl::PropertyTree &props = *props_ref;
    const kosmos::Telemetry &tm = *tm_ref;
    sl::FlightBody &fb = *fb_ref;
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
            const double vis = props.get_double("/environment/visibility", 1.0e5);
            if (vis > 50.0 && vis < 20000.0)
                environment.fog_density = std::max(environment.fog_density, static_cast<float>(3.0 / vis));
        }

        if (in_eva && eva.building >= 0 && eva.building < sim.ship_count() && sim.ship(eva.building)->spec().interior &&
            !sim.ship(eva.building)->spec().interior->panels().empty() && !sim.ship(eva.building)->spec().cockpit.controls.empty())
        {
            // Wall panels show the building's interface, drawn live into a texture.
            const Ship &bld = *sim.ship(eva.building);
            const kosmos::Cockpit pcp = resolved_cockpit(bld.spec().cockpit, eva.building);
            const int wanted = static_cast<int>(std::ceil(panel_target_width * pcp.panel_height / pcp.panel_width));
            if (!panel_target || wanted != panel_target_height)
                panel_target = sl::create_render_target(panel_target_width, panel_target_height = wanted);
            if (panel_target)
                panel_target->smooth = true;
            if (panel_target && sl::begin_render_target(panel_target))
            {
                sl::clear_render_target({18, 20, 26, 255});
                draw_panel(sim, pcp, pcp.panel_image.empty() ? "" : (std::filesystem::path(bld.spec().directory) / pcp.panel_image).string(),
                           0.0f, 0.0f, static_cast<float>(panel_target_width) / pcp.panel_width);
                sl::end_render_target();
            }
        }
        if (in_cockpit3d)
        {
            sim.mfd.bus_power = !sim.active_ship() || !sim.active_ship()->systems() || sim.active_ship()->systems()->instruments_powered();
            sim.mfd.update(frame);
            const kosmos::Cockpit pcp = resolved_cockpit(ship->spec().cockpit, props.get_int("/sim/active-ship"));
            if (pcp.instruments.empty() && pcp.controls.empty())
            {
                for (int side = 0; side < 2; ++side)
                    sim.mfd.draw_to_target(side, mfd_target[side], mfd_target_layout());
            }
            else
            {
                const int wanted = static_cast<int>(std::ceil(panel_target_width * pcp.panel_height / pcp.panel_width));
                if (!panel_target || wanted != panel_target_height)
                    panel_target = sl::create_render_target(panel_target_width, panel_target_height = wanted);
                if (panel_target)
                    panel_target->smooth = true;
                if (panel_target && sl::begin_render_target(panel_target))
                {
                    sl::clear_render_target({18, 20, 26, 255});
                    draw_panel(sim, pcp, pcp.panel_image.empty() ? "" : (std::filesystem::path(ship->spec().directory) / pcp.panel_image).string(),
                               0.0f, 0.0f, static_cast<float>(panel_target_width) / pcp.panel_width);
                    sl::end_render_target();
                }
            }
        }
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
            map.draw(universe, *ship, tm);

        view_proj = camera.projection_matrix(static_cast<float>(screen_w) / screen_h) * camera.view_matrix();
    }
} // namespace kosmos::app
