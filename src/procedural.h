#pragma once

#include "sl.h"

namespace kosmos
{
    // Procedural textures and meshes for stars, nebulae, sky, clouds, rain and rings: pure generators with no state.

    sl::Bitmap *make_glow_texture(int size = 256);
    sl::Bitmap *make_star_surface(int layer, std::uint32_t seed, int width = 512);
    sl::Bitmap *make_star_corona(std::uint32_t seed, double scale, int size = 256);
    sl::Bitmap *make_star_rays(int size = 256);
    sl::Bitmap *make_star_limb(int size = 256);
    sl::Bitmap *make_puff_texture(int size = 64);
    sl::Mesh *make_starfield();
    sl::Bitmap *make_nebula_texture(int system, int width = 2048);
    sl::Bitmap *make_sky_texture(int width = 1024);
    sl::Mesh *make_limb_mesh();
    sl::Mesh *make_cloud_deck_mesh();
    void update_cloud_deck(sl::Mesh *mesh, const sl::dvec3 &centre, const sl::dvec3 &up, const glm::dquat &orientation, double radius, double layer_altitude,
                           double eye_altitude, double cover, double drift_east, double sun_up, bool raining);
    sl::Mesh *make_rain_mesh();
    void update_rain_mesh(sl::Mesh *mesh, const sl::dvec3 &up, const sl::dvec3 &relative_velocity, sl::dvec3 &offset, double dt, double intensity, double daylight);
    void update_limb_mesh(sl::Mesh *mesh, const glm::vec3 &rel, double radius, double thickness, double scale_height, const glm::vec3 &to_sun, const glm::vec3 &colour);
    sl::Mesh *make_ring_mesh(double inner_ratio, const float colour[3]);
    sl::Mesh *make_tail_mesh();
} // namespace kosmos
