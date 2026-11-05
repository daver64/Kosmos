#pragma once

#include "sl.h"

namespace kosmos
{
    // Procedural textures and meshes for stars, nebulae, sky, clouds, rain and rings: pure generators with no state.

    sl::Bitmap *make_glow_texture(int size = 256);
    /** A re-entry wake: across is x (64 px), along is y (head at the top, tail at the bottom), colour white-yellow through orange to dark red, soft edges and fine streaks along the flow. */
    sl::Bitmap *make_plasma_texture(int across = 64, int along = 256);
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
    // A sphere that carries an atmosphere's single-scattering haze: per-vertex colour and alpha worked out each frame by update_haze_mesh().
    struct HazeMesh
    {
        sl::Mesh *mesh = nullptr; // scattered light, drawn additively
        std::vector<sl::Vertex3D> vertices;
        sl::Mesh *dark = nullptr; // the surface dimmed along the view path, drawn with alpha
        std::vector<sl::Vertex3D> dark_vertices;
    };
    HazeMesh make_haze_mesh();
    // `eye` is the camera's position from the body's centre and `to_sun` the unit direction to the Sun, in the frame the mesh is drawn in. The mesh
    // is a unit sphere; draw it scaled to the body's radius. Returns false when the haze is too faint to be worth drawing.
    bool update_haze_mesh(HazeMesh &haze, const glm::vec3 &eye, double radius, double scale_height, double density, const glm::vec3 &colour, const glm::vec3 &to_sun);
    sl::Mesh *make_ring_mesh(double inner_ratio, const float colour[3]);
    sl::Mesh *make_tail_mesh();
} // namespace kosmos
