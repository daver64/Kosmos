#include "app_state.h"
#include "plasma_look.h"

namespace kosmos::app
{
    void App::scene_lighting()
    {
    kosmos::Universe &universe = *universe_ref;
    const kosmos::Telemetry &tm = *tm_ref;
        if ((near_earth || local_sky) && gfx_style == 0)
            sl::draw_sky_3d();
        light = sl::Light3D();
        light.direction = environment.sun_direction;
        // Above the atmosphere the Sun is not reddened by it, even when it sits on the horizon below.
        light.colour = (local_sky || (near_earth && eye_earth_distance < earth_body->radius + 1.0e6)) ? sl::atmosphere_sun_colour(environment) : glm::vec3(1.0f);
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
                light.ambient = static_cast<float>(glm::mix(static_cast<double>(options.ambient), std::min<double>(options.ambient, universe.world.air_density_at(eye_world) < 0.1 ? 0.02 : 0.07), dusk * dusk * (3.0 - 2.0 * dusk) * low));
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
        lamps = std::vector<kosmos::PlacedLight>();
        {
            std::vector<sl::dvec3> ship_positions;
            for (int s = 0; s < sim.ship_count(); ++s)
                ship_positions.push_back(sim.ship(s)->body().position);
            kosmos::collect_base_lights(universe, sim.bases, eye_world, static_cast<double>(sim.props.get_int("/sim/settings/gfx-light-radius")), clock, ship_positions, lamps);
            for (int s = 0; s < sim.ship_count(); ++s)
            {
                const Ship &lit_ship = *sim.ship(s);
                const sl::FlightBody &lb = lit_ship.body();
                const glm::dmat3 turn = glm::mat3_cast(lb.orientation);
                // A ship on the ground turns its lamps off while the Sun is up.
                bool daylight_on_ground = false;
                if (lit_ship.telemetry().landed && lit_ship.telemetry().soi > 0)
                {
                    const sl::dvec3 up_here = glm::normalize(lit_ship.origin() - universe.position(lit_ship.telemetry().soi));
                    daylight_on_ground = glm::dot(up_here, glm::normalize(universe.position(0) - lit_ship.origin())) > 0.05;
                }
                if (daylight_on_ground)
                    continue;
                for (const kosmos::LightSpec &spec : lit_ship.spec().lights)
                {
                    if (spec.stage >= 0 && lit_ship.active_stage() > spec.stage)
                        continue;
                    if (lit_ship.systems() && !lit_ship.systems()->instruments_powered())
                        continue;
                    if (!sim.props.get_bool(spec.navigation ? "/controls/lighting/nav" : "/controls/lighting/lamps", true))
                        continue;
                    for (int i = 0; i < spec.count; ++i)
                    {
                        const sl::dvec3 at = lit_ship.origin() + turn * sl::dvec3(spec.x + spec.step_x * i, spec.y + spec.step_y * i, spec.z + spec.step_z * i);
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
                            p.far_size = 0.0012f;
                            p.glow_strength *= 1.0f - 0.5f * std::clamp(static_cast<float>(distance) / 600.0f, 0.0f, 1.0f);
                        }
                        else if (spec.outer > 0.0f)
                        {
                            // A spotlight's glow is a lens seen down the beam: it fades off-axis and stays small from far away.
                            const float to_eye = glm::dot(p.light.direction, -glm::normalize(p.light.position));
                            p.glow_strength *= std::clamp((to_eye - std::cos(glm::radians(spec.outer))) / 0.3f, 0.0f, 1.0f);
                            p.far_size = 0.0015f;
                        }
                        p.distance = static_cast<float>(distance);
                        lamps.push_back(p);
                    }
                }
            }
            ship_weapons.collect_lights(universe, eye_world, sim.props.get_int("/sim/settings/gfx-weapon-lights"), lamps);
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
    }

    void App::scene_sky()
    {
    kosmos::Universe &universe = *universe_ref;
    const kosmos::Telemetry &tm = *tm_ref;
        // Where the sky and the stars sit: inside the far plane, and beyond the near plane even when that is pushed out by a distant nearest body.
        backdrop = std::clamp(std::min(camera.far_plane * 0.05, camera.near_plane * 1.0e5), camera.near_plane * 1.5, camera.far_plane * 0.9);
        // Every star is a real sphere at its true angular size, placed inside the far plane without changing how big it looks.
        place_star = [&](std::size_t i) -> StarPlace
        {
            StarPlace sp;
            const kosmos::Body &b = universe.bodies[i];
            const sl::dvec3 rel = universe.position(static_cast<int>(i)) - eye_world;
            const double dist = glm::length(rel);
            if (b.kind != kosmos::BodyKind::star || dist <= b.radius * 1.02)
                return sp;
            const double limit = backdrop;
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
        star_basis_base = [&](const StarPlace &sp, double distance, double half, float spin)
        {
            const glm::vec3 to_eye = -sp.dir;
            glm::vec3 right = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), to_eye));
            glm::vec3 up_v = glm::cross(to_eye, right);
            const float c = std::cos(spin), sn = std::sin(spin);
            const glm::vec3 r2 = right * c + up_v * sn, u2 = -right * sn + up_v * c;
            return glm::mat4(glm::vec4(r2 * static_cast<float>(half), 0.0f), glm::vec4(to_eye, 0.0f), glm::vec4(u2 * static_cast<float>(half), 0.0f),
                             glm::vec4(sp.dir * static_cast<float>(distance), 1.0f));
        };
        tint_of = [](const glm::vec3 &c, float alpha)
        {
            return sl::Colour{static_cast<std::uint8_t>(255.0f * std::clamp(c.r, 0.0f, 1.0f)), static_cast<std::uint8_t>(255.0f * std::clamp(c.g, 0.0f, 1.0f)),
                              static_cast<std::uint8_t>(255.0f * std::clamp(c.b, 0.0f, 1.0f)), static_cast<std::uint8_t>(255.0f * std::clamp(alpha, 0.0f, 1.0f))};
        };

        // The photosphere: an alpha-blended shell and an additive shell of filaments turning at different rates. Blended draws never write
        // depth, so the layers cannot z-fight at star distances where the depth buffer has no resolution; the late star loop calls this.
        draw_photosphere = [&](const StarPlace &sp)
        {
            if (!sp.valid || sp.angular < 2.0e-4)
                return;
            const glm::mat4 at = glm::translate(glm::mat4(1.0f), sp.dir * static_cast<float>(sp.distance));
            sl::Material surface_material;
            surface_material.texture = vis.star_surface;
            surface_material.lit = false;
            surface_material.fog = false;
            surface_material.blend = sl::MaterialBlend::alpha;
            surface_material.tint = tint_of(sp.tint * 1.0f, 1.0f);
            sl::draw_mesh(vis.sphere, at * glm::rotate(glm::mat4(1.0f), static_cast<float>(clock * 0.015), glm::vec3(0, 1, 0)) *
                                          glm::scale(glm::mat4(1.0f), glm::vec3(static_cast<float>(sp.radius))), surface_material);
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
        // Stars fade in as the sky darkens.
        {
            const double altitude = home ? tm.altitude : local_sky ? eye_soi_altitude * 8500.0 / universe.bodies[static_cast<std::size_t>(tm.soi)].atmosphere_scale_height : 1.0e6;
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
                // Nebulae only away from Sol, and behind the stars. Always lit: they are far-off emission, whatever side of a planet you are on.
                if (sim.system_index() != 0)
                {
                    if (vis.nebulae_system != sim.system_index())
                    {
                        if (vis.nebula_atlas)
                            sl::destroy_bitmap(vis.nebula_atlas);
                        vis.nebula_atlas = make_nebula_texture(sim.system_index());
                        vis.nebulae_system = sim.system_index();
                    }
                    if (vis.nebula_atlas)
                    {
                        sl::Material neb = sky;
                        neb.texture = vis.nebula_atlas;
                        sl::draw_mesh(vis.sphere, glm::rotate(glm::mat4(1.0f), 1.5708f, glm::vec3(1, 0, 0)) * glm::scale(glm::mat4(1.0f), glm::vec3(static_cast<float>(backdrop) * 0.999f)), neb);
                    }
                }
                sl::draw_mesh(vis.starfield, glm::scale(glm::mat4(1.0f), glm::vec3(static_cast<float>(backdrop))), stars);
            }
        }
    }

    void App::scene_star_glare()
    {
    kosmos::Universe &universe = *universe_ref;
    sl::PropertyTree &props = *props_ref;
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

        // Far houses and trees are unlit billboards baked in daylight; dim them to the ground's own light so they are no brighter than it.
        const auto impostor_light = [&](int body_index)
        {
            const sl::dvec3 from_centre = eye_world - universe.position(body_index);
            const double sun_height = glm::dot(glm::normalize(from_centre), glm::normalize(universe.position(0) - eye_world));
            const double lit = std::clamp(sun_height * 4.0, 0.0, 1.0);
            return static_cast<float>(glm::mix(static_cast<double>(light.ambient), 1.0, lit));
        };
        if (near_earth)
        {
            if (!earth_as_sphere)
            {
                sl::Material ground;
                if (props.get_bool("/sim/settings/gfx-earth-relief"))
                {
                    ground.pbr = true;
                    ground.metallic = 0.0f;
                    ground.roughness = 0.4f;
                }
                terrain->draw(eye_from_earth, ground);
                if (body_terrains[0]->clutter_live)
                {
                    body_terrains[0]->clutter->set_impostor_light(impostor_light(body_terrains[0]->body));
                    clutter_perf.draw_ms += time_ms([&] { body_terrains[0]->clutter->draw(eye_from_earth); });
                }
            }
        }

        for (std::size_t k = 1; k < body_terrains.size(); ++k)
            if (body_terrains[k]->active)
            {
                sl::Material ground;
                body_terrains[k]->terrain->draw(body_terrains[k]->eye_rel, ground);
                if (body_terrains[k]->clutter_live)
                {
                    body_terrains[k]->clutter->set_impostor_light(impostor_light(body_terrains[k]->body));
                    clutter_perf.draw_ms += time_ms([&] { body_terrains[k]->clutter->draw(body_terrains[k]->eye_rel); });
                }
            }
        {
            sl::Material prop_material;
            base_renderer.draw(universe, sim.bases, eye_world, 15000.0, prop_material);
        }

        // A sunlit limb: a camera-facing disc whose opacity follows the atmosphere's thickness along each grazing ray.
        limb_used = 0;
        draw_atmosphere_glow = [&](const sl::dvec3 &rel, const kosmos::Body &b)
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
    }

    void App::draw_comet(std::size_t i, const kosmos::Body &b, const sl::dvec3 &rel, const sl::Light3D &body_light)
    {
    kosmos::Universe &universe = *universe_ref;
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

    // The terrain stands in for the surface sphere, but the weather above it still has to be drawn.
    void App::draw_terrain_body_weather(std::size_t i, const kosmos::Body &b)
    {
        kosmos::Universe &universe = *universe_ref;
        if (i < vis.body_cloud_meshes.size() && vis.body_cloud_meshes[i] && !options.no_clouds && gfx_clouds)
        {
            const sl::dvec3 rel = universe.position(static_cast<int>(i)) - eye_world;
            const double distance = glm::length(rel);
            if (distance - b.radius > b.cloud_height * 1.5)
            {
                sl::Material weather;
                weather.texture = vis.body_cloud_textures[i];
                weather.blend = sl::MaterialBlend::alpha;
                weather.fog = false;
                weather.tint = {static_cast<std::uint8_t>(b.cloud_colour[0] * 255.0f), static_cast<std::uint8_t>(b.cloud_colour[1] * 255.0f),
                                static_cast<std::uint8_t>(b.cloud_colour[2] * 255.0f), 255};
                // Terrain is coarse from afar, so the shell is lifted with the depth resolution, as Earth's is.
                const double altitude_now = distance - b.radius;
                const double resolution = altitude_now * altitude_now / (near_plane * 1.68e7);
                double shell_height = std::clamp(2.0 * resolution, b.cloud_height, 600.0e3);
                if (layered)
                    shell_height = std::min(shell_height, std::max(1.0e3, altitude_now - 1.05 * near_plane));
                shell_height = std::max(shell_height, b.cloud_height);
                const float k = static_cast<float>((b.radius + shell_height) / (b.radius + b.cloud_height));
                const glm::mat4 shell_frame = glm::translate(glm::mat4(1.0f), glm::vec3(rel)) * glm::rotate(glm::mat4(1.0f), static_cast<float>(b.tilt), glm::vec3(0, 0, 1));
                sl::draw_mesh(vis.body_cloud_meshes[i], shell_frame * glm::mat4_cast(glm::quat(universe.orientation(static_cast<int>(i)))) * glm::scale(glm::mat4(1.0f), glm::vec3(k)), weather);
            }
        }
    }

    void App::draw_body_surface(std::size_t i, const kosmos::Body &b, const sl::dvec3 &rel, const glm::mat4 &model, sl::Material &rock)
    {
        if (static_cast<int>(i) == earth_id && vis.earth_tiles)
        {
            // The tile mesh already runs east the same way as the body body_frame, so only the half turn above is needed.
            const glm::mat4 tile_model = model;
            const glm::mat4 view_projection = camera.projection_matrix(static_cast<float>(screen_w) / screen_h) * camera.view_matrix() * tile_model;
            const glm::dvec3 eye_local = glm::dvec3(glm::inverse(tile_model) * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            // The terrain below the switch altitude has no sea glint, so it fades out on the way down instead of vanishing at the swap.
            const double glint_altitude = (glm::length(rel) - b.radius) * 6.371e6 / b.radius;
            vis.earth_tiles->set_specular_strength(0.45f * static_cast<float>(fx::smoothstep(earth_sphere_switch_m, 300.0e3, glint_altitude)));
            vis.earth_tiles->update(eye_local, glm::radians(view_fov), screen_h, &view_projection);
            vis.earth_tiles->draw(tile_model, rock);
        }
        else
        {
            // The single-image sphere mesh runs east the other way, so mirror it (and swap the culling to match).
            if (static_cast<int>(i) == earth_id)
                rock.cull = sl::CullMode::front;
            sl::draw_mesh(i < vis.body_meshes.size() && vis.body_meshes[i] ? vis.body_meshes[i]->parts[0].mesh : vis.sphere,
                          static_cast<int>(i) == earth_id ? model * glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 1.0f, -1.0f)) : model, rock);
        }
    }

    void App::draw_body_layers(std::size_t i, const kosmos::Body &b, const sl::dvec3 &rel, double distance, const glm::dquat &sphere_flip, const sl::Light3D &body_light)
    {
        kosmos::Universe &universe = *universe_ref;
        // Depth precision thins with distance, so each layer over the surface is scaled about the eye towards the camera: it looks the
        // same but sits clear of the planet in depth. The layers are lifted by different amounts so they keep their order too.
        const double depth_gap = std::min(16.0 * distance * distance / (near_plane * 1.68e7), 0.3 * distance);
        auto lifted = [&](double fraction)
        {
            const float lift = static_cast<float>(1.0 - depth_gap * fraction / distance);
            return glm::translate(glm::mat4(1.0f), glm::vec3(rel) * lift) * glm::rotate(glm::mat4(1.0f), static_cast<float>(b.tilt), glm::vec3(0, 0, 1)) *
                   glm::mat4_cast(glm::quat(universe.orientation(static_cast<int>(i)) * sphere_flip)) *
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
            // Drawn on the same single-image sphere as the surface, so it takes the same mirroring.
            lights.cull = sl::CullMode::front;
            sl::draw_mesh(vis.sphere, lifted(0.25) * glm::scale(glm::mat4(1.0f), glm::vec3(1.0005f, 1.0005f, -1.0005f)), lights);
            sl::set_light_3d(body_light);
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
                            static_cast<std::uint8_t>(b.cloud_colour[2] * 255.0f), 200};
            const float k = static_cast<float>(1.0 - depth_gap / distance);
            const glm::mat4 shell_frame = glm::translate(glm::mat4(1.0f), glm::vec3(rel) * k) * glm::rotate(glm::mat4(1.0f), static_cast<float>(b.tilt), glm::vec3(0, 0, 1));
            sl::draw_mesh(vis.body_cloud_meshes[i], shell_frame * glm::mat4_cast(glm::quat(universe.orientation(static_cast<int>(i)) * sphere_flip)) * glm::scale(glm::mat4(1.0f), glm::vec3(k)), weather);
        }
        draw_atmosphere_glow(rel, b);
    }

    void App::draw_body_rings(std::size_t i, const kosmos::Body &b, const glm::mat4 &body_frame)
    {
        // Rings are lit from one side or the other; edge-on they thin to a line.
        const glm::vec3 axis = glm::normalize(glm::vec3(body_frame * glm::vec4(0, 1, 0, 0)));
        sl::Material ring;
        ring.lit = false;
        ring.fog = false;
        ring.cull = sl::CullMode::none;
        ring.blend = sl::MaterialBlend::alpha;
        ring.tint.alpha = static_cast<std::uint8_t>(255.0f * (0.45f + 0.55f * std::fabs(glm::dot(axis, to_sun))));
        sl::draw_mesh(vis.rings[i], body_frame * glm::scale(glm::mat4(1.0f), glm::vec3(static_cast<float>(b.ring_outer))), ring);
    }

    void App::draw_body(std::size_t i, const sl::Light3D &body_light)
    {
    kosmos::Universe &universe = *universe_ref;
        const kosmos::Body &b = universe.bodies[i];
        if (b.kind == kosmos::BodyKind::star || (static_cast<int>(i) == earth_id && near_earth && !earth_as_sphere))
            return;
        bool terrain_drawn = false;
        for (std::size_t k = 1; k < body_terrains.size(); ++k)
            terrain_drawn = terrain_drawn || (body_terrains[k]->body == static_cast<int>(i) && body_terrains[k]->active);
        if (terrain_drawn)
        {
            draw_terrain_body_weather(i, b);
            return;
        }
        const sl::dvec3 rel = universe.position(static_cast<int>(i)) - eye_world;
        const double distance = glm::length(rel);
        if (distance - b.radius > camera.far_plane * 0.95)
            return;
        // Earth's sphere image has to be turned half a revolution to put longitude 0 where the terrain and the bases have it.
        const glm::dquat sphere_flip = static_cast<int>(i) == earth_id ? glm::angleAxis(pi, sl::dvec3(0.0, 1.0, 0.0)) : glm::dquat(1.0, 0.0, 0.0, 0.0);
        sl::Material rock;
        rock.texture = vis.body_textures[i];
        rock.normal_map = vis.body_normals[i];
        rock.fog = false;
        const glm::mat4 body_frame = glm::translate(glm::mat4(1.0f), glm::vec3(rel)) * glm::rotate(glm::mat4(1.0f), static_cast<float>(b.tilt), glm::vec3(0, 0, 1));
        const glm::mat4 model = body_frame * glm::mat4_cast(glm::quat(universe.orientation(static_cast<int>(i)) * sphere_flip)) *
                                glm::scale(glm::mat4(1.0f), glm::vec3(static_cast<float>(b.radius)));
        draw_body_surface(i, b, rel, model, rock);
        draw_body_layers(i, b, rel, distance, sphere_flip, body_light);
        if (vis.rings[i])
            draw_body_rings(i, b, body_frame);

        if (b.kind == kosmos::BodyKind::comet)
        {
        draw_comet(i, b, rel, body_light);
        }
    }

    void App::scene_bodies()
    {
    kosmos::Universe &universe = *universe_ref;
        // Other bodies are spheres while inside the far plane; farther ones become markers on the overlay.
        // Planets and their clouds have no fill light of their own: the night side stays dark (the ambient term is for ships).
        sl::Light3D body_light = light;
        body_light.ambient = std::min(light.ambient, 0.04f);
        sl::set_light_3d(body_light);
        for (std::size_t i = 0; i < universe.bodies.size(); ++i)
        {
            draw_body(i, body_light);
        }

        if (near_earth && !options.no_clouds && gfx_clouds)
        {
            // From space the terrain hides the sky's clouds, so the weather is a translucent shell lifted as depth coarsens.
            sl::Material cloud;
            cloud.texture = home_cloud_texture ? home_cloud_texture : vis.cloud_texture;
            if (home_cloud_texture)
                cloud.tint = {static_cast<std::uint8_t>(earth_body->cloud_colour[0] * 255.0f), static_cast<std::uint8_t>(earth_body->cloud_colour[1] * 255.0f), static_cast<std::uint8_t>(earth_body->cloud_colour[2] * 255.0f), 255};
            cloud.blend = sl::MaterialBlend::alpha;
            cloud.fog = false;
            const double altitude_now = eye_earth_distance - earth_body->radius;
            const double resolution = altitude_now * altitude_now / (near_plane * 1.68e7);
            double shell_height = earth_as_sphere ? home_cloud_height : std::clamp(2.0 * resolution, home_cloud_height, 600.0e3);
            // A shell nearer the eye than the near plane is sliced off along a line across the view, so keep it well beyond that plane, as the plane also cuts off-axis parts of a wide view.
            shell_height = std::min(shell_height, std::max(1.0e3, altitude_now - 1.8 * near_plane));
            const float shell_scale = static_cast<float>((earth_body->radius + shell_height) / (earth_body->radius + home_cloud_height));
            sl::draw_mesh(cloud_shell, glm::translate(glm::mat4(1.0f), glm::vec3(earth_pos - eye_world)) * glm::scale(glm::mat4(1.0f), glm::vec3(shell_scale)), cloud);
        }

        if (earth_id >= 0 && near_earth)
            draw_atmosphere_glow(universe.position(earth_id) - eye_world, universe.bodies[static_cast<std::size_t>(earth_id)]);
        sl::set_light_3d(light);

        if (layered)
        {
            if (sl::clear_depth_3d())
                sl::set_camera_3d(ship_camera);
            else
                depth_layers_work = false;
        }
    }

    void App::draw_ship_interior(int s, Ship &other, const sl::FlightBody &ob, const glm::mat4 &model)
    {
                const Visuals::InteriorMeshes &rooms = vis.interior_meshes(*other.spec().interior);
                const kosmos::Interior &shell_room = *other.spec().interior;
                const bool inside_here = eva.active && eva.building == s;
                const bool powered = !other.systems() || other.systems()->powered();
                sl::Material shell;
                shell.cull = sl::CullMode::back;
                // From inside, the room is lit by its own lamps (and a dim glow when the bus is dead) instead of the Sun.
                const sl::Light3D saved_light = sl::get_light_3d();
                const std::vector<sl::LocalLight3D> saved_lamps = sl::get_local_lights_3d();
                if (inside_here)
                {
                    sl::Light3D dim;
                    dim.ambient = powered ? 0.22f : 0.06f;
                    dim.colour = glm::vec3(0.0f);
                    sl::set_light_3d(dim);
                    std::vector<sl::LocalLight3D> room_lamps;
                    if (powered)
                        for (const kosmos::Interior::Light &l : shell_room.lights())
                        {
                            sl::LocalLight3D lamp;
                            lamp.position = glm::vec3(model * glm::vec4(glm::vec3(shell_room.to_ship(l.pos)), 1.0f));
                            lamp.colour = glm::vec3(l.colour);
                            lamp.intensity = static_cast<float>(l.intensity);
                            lamp.range = static_cast<float>(l.range);
                            room_lamps.push_back(lamp);
                        }
                    sl::set_local_lights_3d(room_lamps);
                }
                if (rooms.model)
                {
                    const glm::mat4 place = model * glm::translate(glm::mat4(1.0f), glm::vec3(shell_room.to_ship(sl::dvec3(0.0)))) *
                                            glm::mat4(glm::mat3(glm::vec3(shell_room.dir_to_ship({1, 0, 0})), glm::vec3(shell_room.dir_to_ship({0, 1, 0})), glm::vec3(shell_room.dir_to_ship({0, 0, 1}))));
                    sl::draw_model(rooms.model, place);
                }
                else
                {
                    if (rooms.outside)
                        sl::draw_mesh(rooms.outside, model, shell);
                    if (inside_here)
                        shell.lit = true;
                    else
                    {
                        shell.lit = false;
                        shell.tint = {170, 170, 170, 255};
                    }
                    shell.fog = false;
                    if (rooms.inside)
                        sl::draw_mesh(rooms.inside, model, shell);
                    if (rooms.glass)
                    {
                        sl::Material pane;
                        pane.lit = false;
                        pane.fog = false;
                        pane.blend = sl::MaterialBlend::alpha;
                        sl::draw_mesh(rooms.glass, model, pane);
                    }
                }
                if (inside_here)
                {
                    sl::set_light_3d(saved_light);
                    sl::set_local_lights_3d(saved_lamps);
                }
                {
                    const kosmos::Interior &room = *other.spec().interior;
                    sl::Material slab;
                    slab.lit = false;
                    slab.fog = false;
                    slab.tint = {96, 116, 140, 255};
                    const glm::mat3 axes(glm::vec3(room.dir_to_ship({1, 0, 0})), glm::vec3(room.dir_to_ship({0, 1, 0})), glm::vec3(room.dir_to_ship({0, 0, 1})));
                    const double dh = room.door_height(), c = room.cell();
                    for (std::size_t di = 0; di < room.doors().size() && di < other.interior_state.door_open.size(); ++di)
                    {
                        const kosmos::Interior::Door &door = room.doors()[di];
                        const double open = other.interior_state.door_open[di], shown = dh * (1.0 - open);
                        if (shown < 0.02)
                            continue;
                        const sl::dvec3 mid = door.centre + sl::dvec3(0.0, dh - shown * 0.5, 0.0);
                        const glm::vec3 size = door.along_x ? glm::vec3(static_cast<float>(c), static_cast<float>(shown), static_cast<float>(c * 0.3)) : glm::vec3(static_cast<float>(c * 0.3), static_cast<float>(shown), static_cast<float>(c));
                        sl::draw_mesh(vis.cockpit_box, model * glm::translate(glm::mat4(1.0f), glm::vec3(room.to_ship(mid))) * glm::mat4(axes) * glm::scale(glm::mat4(1.0f), size), slab);
                    }
                }
                if (panel_target && in_eva && eva.building == s && !other.spec().cockpit.controls.empty())
                {
                    const kosmos::Cockpit &cp = other.spec().cockpit;
                    const double aspect = cp.panel_height / cp.panel_width;
                    sl::Material board;
                    board.texture = panel_target;
                    board.lit = false;
                    board.fog = false;
                    board.cull = sl::CullMode::none;
                    for (const kosmos::Interior::Panel &pn : other.spec().interior->panels())
                    {
                        const sl::dvec3 right = glm::cross(sl::dvec3(0.0, 1.0, 0.0), pn.normal);
                        const glm::mat3 basis(glm::vec3(other.spec().interior->dir_to_ship(right)), glm::vec3(other.spec().interior->dir_to_ship(sl::dvec3(0.0, 1.0, 0.0))), glm::vec3(other.spec().interior->dir_to_ship(pn.normal)));
                        const glm::vec3 centre = glm::vec3(other.spec().interior->to_ship(pn.pos + pn.normal * 0.02));
                        const float w = static_cast<float>(pn.width);
                        sl::draw_mesh(vis.screen_quad, model * glm::translate(glm::mat4(1.0f), centre) * glm::mat4(basis) * glm::scale(glm::mat4(1.0f), glm::vec3(w, static_cast<float>(w * aspect), 1.0f)), board);
                    }
                }
    }

    glm::mat4 App::vc_animation_matrix(Ship &other, const kosmos::VcAnimation &va)
    {
        double v = 0.0;
        if (va.source == "gear")
            v = other.gear_position();
        else if (va.source.rfind("anim:", 0) == 0)
        {
            const auto &ac = other.spec().animation_controls;
            for (std::size_t i = 0; i < ac.size(); ++i)
                if (ac[i].property == va.source.substr(5))
                    v = other.animation_position(i);
        }
        else if (!va.source.empty())
            v = sim.props.get_double(va.source, 0.0);
        if (va.slide)
            return glm::translate(glm::mat4(1.0f), glm::vec3(va.move[0], va.move[1], va.move[2]) * static_cast<float>(glm::clamp(v, 0.0, 1.0) - va.initial));
        const glm::vec3 pivot(va.pivot[0], va.pivot[1], va.pivot[2]);
        return glm::translate(glm::mat4(1.0f), pivot) * glm::rotate(glm::mat4(1.0f), glm::radians(static_cast<float>(va.angle_deg * (glm::clamp(v, 0.0, 1.0) - va.initial))), glm::normalize(glm::vec3(va.axis[0], va.axis[1], va.axis[2]))) * glm::translate(glm::mat4(1.0f), -pivot);
    }

    void App::draw_ship_cockpit3d(Ship &other, const sl::FlightBody &ob, const glm::mat4 &model)
    {
                const kosmos::Cockpit &cp = other.spec().cockpit;
                const glm::mat3 basis = cockpit_screen_basis();
                const glm::mat3 iface = interface_frame(other.spec());
                sl::Material console;
                console.tint = {38, 40, 46, 255};
                console.cull = sl::CullMode::none;
                const bool has_vc = !cp.vc_model.empty();
                if (has_vc)
                {
                    if (sl::Model *vc = vis.vc_model(cp, other.spec().directory))
                    {
                        for (sl::ModelPart &mp : vc->parts)
                        {
                            glm::mat4 lever(1.0f);
                            for (const kosmos::VcAnimation &va : cp.vc_animations)
                            {
                                bool match = false;
                                for (const std::string &pn : va.parts)
                                    match = match || mp.name.find(pn) != std::string::npos;
                                if (!match)
                                    continue;
                                lever = vc_animation_matrix(other, va);
                                break;
                            }
                            mp.transform = lever;
                            mp.visible = true;
                            for (const std::string &hn : cp.vc_hide)
                                if (mp.name.find(hn) != std::string::npos)
                                    mp.visible = false;
                            const bool left = mp.name.find("LMFD_DISPLAY") != std::string::npos;
                            if (left || mp.name.find("RMFD_DISPLAY") != std::string::npos)
                            {
                                mp.material.texture = mfd_target[left ? 0 : 1];
                                mp.material.tint = {255, 255, 255, 255};
                                mp.material.lit = false;
                                mp.material.fog = false;
                            }
                            else if (const kosmos::Cockpit::VcInstrument *vi = [&]() -> const kosmos::Cockpit::VcInstrument * {
                                         for (const kosmos::Cockpit::VcInstrument &c : cp.vc_instruments)
                                             if (c.unit >= 0 && c.unit < 8 && instr_target[c.unit] && mp.name.find(c.part) != std::string::npos)
                                                 return &c;
                                         return nullptr; }())
                            {
                                mp.material.texture = instr_target[vi->unit];
                                mp.material.tint = {255, 255, 255, 255};
                                mp.material.lit = false;
                                mp.material.fog = false;
                            }
                            else if (mp.name.find("HUDDISP") != std::string::npos)
                            {
                                mp.material.texture = hud_target;
                                mp.material.tint = {255, 255, 255, 255};
                                mp.material.lit = false;
                                mp.material.fog = false;
                                mp.material.cull = sl::CullMode::none;
                                mp.material.blend = sl::MaterialBlend::alpha; // the HUD is drawn with its own alpha
                            }
                            else
                            {
                                // Orbiter cockpits are ambient lit, so the sun-facing shading would leave the interior black.
                                mp.material.lit = false;
                                mp.material.fog = false;
                                const bool glass = mp.name.find("glass") != std::string::npos || mp.name.find("visor") != std::string::npos;
                                const bool hidden = false;
                                // The loader drops Kd, so plain materials would come out white.
                                const Uint8 level = mp.material.texture ? 217 : 100;
                                mp.material.tint = {level, level, level, static_cast<Uint8>(hidden ? 0 : glass ? 60 : 255)};
                                if (glass || hidden || mp.name.find("LIT_SURF") != std::string::npos)
                                    mp.material.blend = sl::MaterialBlend::alpha; // label strips are text on a transparent background
                            }
                        }
                        sl::draw_model(vc, model);
                    }
                    return;
                }
                if (!cp.instruments.empty() || !cp.controls.empty())
                {
                    const glm::vec3 centre = glm::vec3(cp.eye) + iface * glm::vec3(cp.panel_pos);
                    const float pw = static_cast<float>(cp.panel_size), ph = pw * cp.panel_height / cp.panel_width;
                    const glm::mat3 pb = iface * panel_basis(cp);
                    // The console is a slab just behind the board.
                    if (!has_vc)
                        sl::draw_mesh(vis.cockpit_box, model * glm::translate(glm::mat4(1.0f), centre - pb[2] * 0.1f) * glm::mat4(pb) * glm::scale(glm::mat4(1.0f), glm::vec3(pw * 1.04f, ph * 1.06f, 0.18f)), console);
                    if (panel_target)
                    {
                        sl::Material board;
                        board.texture = panel_target;
                        board.lit = false;
                        board.fog = false;
                        board.cull = sl::CullMode::none;
                        sl::draw_mesh(vis.screen_quad, model * glm::translate(glm::mat4(1.0f), centre) * glm::mat4(pb) * glm::scale(glm::mat4(1.0f), glm::vec3(pw, ph, 1.0f)), board);
                    }
                }
                else
                {
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
    }

    void App::scene_ships()
    {
    kosmos::Universe &universe = *universe_ref;
        // Ships and their jettisoned stages.
        for (int s = 0; s < sim.ship_count(); ++s)
        {
            Ship &other = *sim.ship(s);
            const sl::FlightBody &ob = other.body();
            const glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(other.origin() - eye_world)) *
                                    glm::mat4(glm::mat3_cast(glm::quat(ob.orientation)));
            if (other.spec().interior)
            {
        draw_ship_interior(s, other, ob, model);
            }
            if (!((in_cockpit || in_interior) && &other == ship))
                draw_parts(vis, other.spec(), other.active_stage(), other.stage_count() - 1, model, true, other.gear_position(), other.pod_angle(false), other.pod_angle(true), &other);
            else if (in_cockpit3d)
            {
        draw_ship_cockpit3d(other, ob, model);
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
    }

    void App::scene_weather()
    {
    kosmos::Universe &universe = *universe_ref;
    sl::PropertyTree &props = *props_ref;
        // Weather clouds: a layer at the reported cloud base over the flown ship, below 100 km only.
        if (props.get_bool("/sim/settings/weather", true) && props.get_bool("/sim/settings/gfx-clouds") && gfx_style == 0 && !map_mode && props.get_int("/sim/settings/gfx-cloud-style") == 0)
        {
            const kosmos::Telemetry &wt = ship->telemetry();
            const double cover = props.get_double("/environment/cloud-cover");
            if (wt.soi >= 0 && wt.soi < static_cast<int>(universe.bodies.size()) && cover > 0.04 && wt.altitude < 100.0e3 && universe.bodies[static_cast<std::size_t>(wt.soi)].atmosphere_density > 0.0)
            {
                const kosmos::Body &wb = universe.bodies[static_cast<std::size_t>(wt.soi)];
                const sl::dvec3 centre_world = universe.position(wt.soi);
                const sl::dvec3 from_centre = eye_world - centre_world;
                const double eye_distance = glm::length(from_centre);
                const sl::dvec3 local_up = from_centre / eye_distance;
                const double eye_alt = eye_distance - wb.radius;
                const double ground = eye_alt - wt.altitude;
                const double layer = ground + std::max(150.0, props.get_double("/environment/cloud-base", 1500.0));
                if (!vis.local_cloud)
                    vis.local_cloud = make_cloud_deck_mesh();
                update_cloud_deck(vis.local_cloud, centre_world - eye_world, local_up, universe.orientation(wt.soi), wb.radius, layer, eye_alt, cover,
                                  props.get_double("/environment/wind-east") * universe.time(), glm::dot(local_up, sl::dvec3(to_sun)), props.get_double("/environment/rain") > 0.1);
                sl::Material deck;
                deck.lit = false;
                deck.fog = false;
                deck.cull = sl::CullMode::none;
                deck.blend = sl::MaterialBlend::alpha;
                sl::draw_mesh(vis.local_cloud, glm::mat4(1.0f), deck);
            }
        }

        // Falling rain in the air around the camera, below the cloud base.
        if (props.get_bool("/sim/settings/weather", true) && props.get_bool("/sim/settings/weather-rain", true) && gfx_style == 0 && !map_mode)
        {
            const kosmos::Telemetry &rt = ship->telemetry();
            const double rain = props.get_double("/environment/rain");
            if (rain > 0.02 && rt.soi >= 0 && rt.soi < static_cast<int>(universe.bodies.size()) && rt.altitude < std::max(props.get_double("/environment/cloud-base", 3000.0), 1500.0))
            {
                const sl::dvec3 from_centre = eye_world - universe.position(rt.soi);
                const sl::dvec3 local_up = glm::normalize(from_centre);
                const sl::dvec3 air_v = universe.world.air_velocity_at(ship->body().position);
                const sl::dvec3 fall = -local_up * 9.0;
                const sl::dvec3 relative = air_v - ship->body().velocity + fall;
                if (!vis.rain_mesh)
                    vis.rain_mesh = make_rain_mesh();
                update_rain_mesh(vis.rain_mesh, local_up, relative, vis.rain_offset, frame, std::min(1.0, rain * 1.1), fx::smoothstep(-0.12, 0.3, glm::dot(local_up, sl::dvec3(to_sun))));
                sl::Material drops;
                drops.lit = false;
                drops.fog = false;
                drops.cull = sl::CullMode::none;
                drops.blend = sl::MaterialBlend::alpha;
                sl::draw_mesh(vis.rain_mesh, glm::mat4(1.0f), drops);
            }
        }
    }

    void App::scene_lightning()
    {
    kosmos::Universe &universe = *universe_ref;
    sl::PropertyTree &props = *props_ref;
        // Lightning: the flash is drawn over the finished scene, the bolt here from the cloud base to the ground.
        lightning_flash = lightning.flash();
        {
            const kosmos::Telemetry &lt = ship->telemetry();
            const double cloud_agl = props.get_double("/environment/cloud-base", 1500.0);
            if (lt.soi >= 0 && lt.soi < static_cast<int>(universe.bodies.size()) && lt.altitude < cloud_agl && lt.altitude < 20.0e3)
            {
                const sl::dvec3 local_up = glm::normalize(eye_world - universe.position(lt.soi));
                const sl::dvec3 e1 = glm::normalize(glm::cross(std::fabs(local_up.y) < 0.9 ? sl::dvec3(0, 1, 0) : sl::dvec3(1, 0, 0), local_up));
                const sl::dvec3 e2 = glm::cross(local_up, e1);
                if (!vis.bolt_bitmaps_tried)
                {
                    vis.bolt_bitmaps_tried = true;
                    static const char *files[] = {"lightning", "lightning_1", "lightning_2", "lightning3"};
                    for (int i = 0; i < kosmos::Lightning::bolt_textures; ++i)
                        vis.bolt_bitmaps[i] = sl::load_bitmap((sim.root() + "/data/textures/lightning/" + files[i] + ".png").c_str());
                }
                for (int strike = 0; strike < std::min(lightning.strike_count(), kosmos::Lightning::max_bolts); ++strike)
                {
                    std::vector<sl::Vertex3D> local(static_cast<std::size_t>(kosmos::Lightning::bolt_quads) * 4);
                    const int used = lightning.bolt_vertices(strike, -lt.altitude, cloud_agl - lt.altitude, local.data(), kosmos::Lightning::bolt_quads);
                    if (used <= 0)
                        continue;
                    sl::Mesh *&mesh = vis.bolt_meshes[strike];
                    if (!mesh)
                    {
                        std::vector<sl::Vertex3D> blank(local.size());
                        std::vector<std::uint32_t> indices;
                        for (int i = 0; i < kosmos::Lightning::bolt_quads; ++i)
                            for (const std::uint32_t k : {0u, 1u, 2u, 2u, 1u, 3u})
                                indices.push_back(static_cast<std::uint32_t>(i * 4) + k);
                        mesh = sl::create_mesh(blank, indices);
                    }
                    for (std::size_t i = 0; i < local.size(); ++i)
                    {
                        const glm::dvec3 p = e1 * static_cast<double>(local[i].position[0]) + e2 * static_cast<double>(local[i].position[1]) + local_up * static_cast<double>(local[i].position[2]);
                        for (int k = 0; k < 3; ++k)
                        {
                            local[i].position[k] = static_cast<float>(p[k]);
                            local[i].normal[k] = static_cast<float>(local_up[k]);
                        }
                    }
                    sl::update_mesh_vertices(mesh, local);
                    sl::Material bolt;
                    bolt.texture = vis.bolt_bitmaps[lightning.strike_texture(strike)];
                    bolt.lit = false;
                    bolt.fog = false;
                    bolt.cull = sl::CullMode::none;
                    bolt.blend = sl::MaterialBlend::additive;
                    sl::draw_mesh(mesh, glm::mat4(1.0f), bolt);
                }
            }
        }
    }

    void App::scene_lamp_bulbs()
    {
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
    }

    // The plasma sheath and wake of a ship falling fast through air, sized by the shape of the vehicle (see plasma_look).
    void App::draw_reentry_plasma()
    {
        kosmos::Universe &universe = *universe_ref;
        sl::Material glow;
        glow.lit = false;
        glow.fog = false;
        glow.blend = sl::MaterialBlend::additive;
        glow.cull = sl::CullMode::none;
        const float flicker = 0.9f + 0.1f * std::sin(static_cast<float>(sl::time_ms() % 100000) * 0.037f);
        for (int s = 0; s < sim.ship_count(); ++s)
        {
            Ship &other = *sim.ship(s);
            const sl::FlightBody &ob = other.body();
            if (other.on_rails() || other.crashed() || ob.mode != sl::FlightBodyMode::dynamic || other.telemetry().soi < 0)
                continue;
            const double rho = universe.world.air_density_at(ob.position);
            const sl::dvec3 air = ob.velocity - universe.world.air_velocity_at(ob.position);
            const double speed = glm::length(air);
            if (rho < 1.0e-8 || speed < 1500.0)
                continue;
            const kosmos::ShipSpec &spec = other.spec();
            double radius = 0.5;
            for (std::size_t i = static_cast<std::size_t>(std::max(other.active_stage(), 0)); i < spec.stages.size(); ++i)
                radius = std::max(radius, spec.stages[i].radius);
            const double nose = spec.thermal.enabled ? spec.thermal.nose_radius : radius * 0.6;
            const kosmos::PlasmaLook look = kosmos::plasma_look(kosmos::stagnation_flux(rho, speed, nose), radius, nose);
            if (look.brilliance <= 0.0f)
                continue;
            const bool aboard = &other == ship && (in_cockpit || in_interior);
            const float k = look.brilliance * flicker * (aboard ? 0.35f : 1.0f);

            // The frame trails the flow: +Z points back along the airstream.
            const glm::vec3 back = glm::vec3(-air / speed);
            const glm::vec3 side = std::fabs(back.y) < 0.95f ? glm::normalize(glm::cross(glm::vec3(0, 1, 0), back)) : glm::vec3(1, 0, 0);
            const glm::vec3 up_axis = glm::cross(back, side);
            const glm::mat4 basis(glm::vec4(side, 0.0f), glm::vec4(up_axis, 0.0f), glm::vec4(back, 0.0f), glm::vec4(0, 0, 0, 1));
            // The sheath starts at the part of the hull that leads into the flow and trails behind the whole vehicle.
            const glm::vec3 flow_local = glm::vec3(glm::normalize(ob.direction_to_local(air)));
            float lead = -1.0e9f, trail = 1.0e9f;
            glm::vec3 lead_point(0.0f);
            const glm::vec3 lateral = std::fabs(flow_local.x) + std::fabs(flow_local.y) > 1.0e-3f ? glm::normalize(glm::vec3(flow_local.x, flow_local.y, 0.0f)) : glm::vec3(1, 0, 0);
            for (std::size_t i = static_cast<std::size_t>(std::max(other.active_stage(), 0)); i < spec.stages.size(); ++i)
            {
                const kosmos::StageSpec &st = spec.stages[i];
                for (const float dz : {-0.5f, 0.5f})
                    for (const float sgn : {-1.0f, 1.0f})
                    {
                        const glm::vec3 p = glm::vec3(0.0f, 0.0f, static_cast<float>(st.z + dz * st.length)) + lateral * static_cast<float>(st.radius) * sgn;
                        const float d = glm::dot(p, flow_local);
                        if (d > lead)
                            lead = d, lead_point = p;
                        trail = std::min(trail, d);
                    }
            }
            const float depth = lead > trail ? lead - trail : 0.0f;
            const glm::mat4 at = glm::translate(glm::mat4(1.0f), glm::vec3(other.origin() - eye_world)) * glm::mat4(glm::mat3_cast(glm::quat(ob.orientation))) *
                                 glm::translate(glm::mat4(1.0f), lead_point) * glm::mat4(glm::mat3(glm::inverse(glm::mat3_cast(glm::quat(ob.orientation)))) * glm::mat3(basis));
            const float wake = look.length + depth * 0.8f;

            // The glow stays orange-red and only turns yellow-white at the very hottest.
            const float h = look.heat;
            const auto mix8 = [](float a, float b, float t) { return static_cast<std::uint8_t>(std::clamp(a + (b - a) * t, 0.0f, 255.0f)); };
            const sl::Colour haze{235, mix8(32, 60, h), 5, 255};
            const sl::Colour body{255, mix8(55, 125, h), mix8(10, 45, h), 255};
            const sl::Colour core{255, mix8(110, 190, h), mix8(30, 110, h), 255};
            const auto layer = [&](const sl::Colour &c, float alpha, const glm::vec3 &centre, const glm::vec3 &radii)
            {
                glow.tint = c;
                glow.tint.alpha = static_cast<std::uint8_t>(std::clamp(alpha * 255.0f, 0.0f, 255.0f));
                sl::draw_mesh(vis.glow, at * glm::translate(glm::mat4(1.0f), centre) * glm::scale(glm::mat4(1.0f), radii), glow);
            };
            // Many faint nested layers so no single outline shows.
            constexpr int layers = 28;
            for (int i = 0; i < layers; ++i)
            {
                const float t = static_cast<float>(i) / (layers - 1);
                const float grow = 1.0f + 1.2f * t;
                layer(haze, 0.016f * k * (1.0f - 0.5f * t), glm::vec3(0, 0, wake * 0.45f * grow),
                      glm::vec3(look.width * grow * 0.9f, look.width * grow * 0.9f, wake * 0.55f * grow));
            }
            for (int i = 0; i < layers; ++i)
            {
                const float t = static_cast<float>(i) / (layers - 1);
                layer(body, 0.024f * k, glm::vec3(0, 0, wake * 0.25f * (1.0f - t * 0.5f)),
                      glm::vec3(look.width * (1.0f - 0.7f * t), look.width * (1.0f - 0.7f * t), wake * 0.4f * (1.0f - 0.6f * t)));
            }
            // The shock cap ahead of the nose.
            for (int i = 0; i < layers; ++i)
            {
                const float t = static_cast<float>(i) / (layers - 1);
                const float r = look.cap * (1.2f - 0.9f * t);
                layer(i < layers / 2 ? body : core, 0.028f * k, glm::vec3(0, 0, -look.cap * 0.5f), glm::vec3(r, r, r * 0.7f));
            }
        }
    }

    void App::scene_exhaust_and_held()
    {
    kosmos::Universe &universe = *universe_ref;
    const kosmos::Telemetry &tm = *tm_ref;
    sl::FlightBody &fb = *fb_ref;
        // Engine and reaction-control exhaust.
        {
            const glm::mat4 hull = glm::translate(glm::mat4(1.0f), glm::vec3(ship->origin() - eye_world)) * glm::mat4(glm::mat3_cast(glm::quat(fb.orientation)));
            const double vacuum = home ? fx::smoothstep(8.0e3, 70.0e3, tm.altitude) : 1.0;
            const double com_altitude = universe.world.altitude_of(fb.position);
            const sl::dvec3 world_up = glm::normalize(fb.position - universe.world.body(tm.soi)->position);
            const glm::vec3 local_up = glm::vec3(glm::inverse(fb.orientation) * world_up);
            exhaust_fx.update(*ship, vacuum, static_cast<float>(frame), !ship->on_rails() && sim.current_warp() < 4.5, com_altitude < 150.0 && !ship->on_rails(), static_cast<float>(com_altitude), local_up);
            exhaust_fx.draw(vis.glow, hull);
        }
        draw_reentry_plasma();
        arsenal.draw_world(universe, eye_world, vis.glow);
        // Beams run for kilometres, and the ship camera's near plane is centimetres, which leaves no depth precision at that range
        // and cuts a beam off a couple of hundred metres out. Shots get a fresh depth buffer and a camera suited to their range.
        if (sl::clear_depth_3d())
        {
            sl::Camera shot_camera = ship_camera;
            shot_camera.near_plane = 0.5f;
            shot_camera.far_plane = 1.0e6f;
            sl::set_camera_3d(shot_camera);
        }
        ship_weapons.draw(universe, eye_world, vis.quad, vis.glow_texture);
        sl::set_camera_3d(layered ? ship_camera : camera);
        if (in_eva && arsenal.held(sim) && sl::clear_depth_3d())
        {
            sl::Camera held_camera = ship_camera;
            held_camera.near_plane = 0.03f;
            held_camera.far_plane = 50.0f;
            sl::set_camera_3d(held_camera);
            const glm::vec3 fwd = glm::vec3(eva.forward()), upv = glm::vec3(eva.view_up());
            if (!options.clean)
                arsenal.draw_held(sim, glm::mat4(glm::mat3(glm::cross(fwd, upv), upv, -fwd)));
        }
    }
} // namespace kosmos::app
