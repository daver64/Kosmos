#pragma once

#include "app.h"

#include <string>

namespace kosmos::app
{
    std::vector<sl::TerrainLayer> planet_layers();

extern bool detail_disabled;

    /** CPU time spent in the clutter layers, printed every few seconds by --perf. */
    struct ClutterPerf
    {
        double update_ms = 0.0, draw_ms = 0.0, window_s = 0.0;
        int frames = 0;
        std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();
    };

    extern ClutterPerf clutter_perf;

    template <class F> double time_ms(F &&f)
    {
        const auto t0 = std::chrono::steady_clock::now();
        f();
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }

    struct BodyTerrain
    {
        int body = -1;
        std::unique_ptr<sl::Terrain> terrain;
        std::shared_ptr<kosmos::OrbiterImagery> orbiter_imagery;
        bool has_orbiter_elevation = false;
        bool active = false;
        bool orbital_relief = false;
        sl::dvec3 eye_rel{0.0};
        // Trees, houses, rocks and outposts scattered over the ground; declared after the terrain so it is freed first.
        kosmos::SurfaceFrame frame;
        std::vector<kosmos::ClutterZone> clutter_zones;
        std::shared_ptr<const kosmos::Landclass> landclass;
        std::unique_ptr<sl::ClutterLayer> clutter;
        double clutter_density = 0.0;
        double clutter_range = 0.0;
        bool clutter_live = false;
    };

    std::unique_ptr<BodyTerrain> make_body_terrain(Sim &sim, int id, bool home);

    void settle(sl::Terrain &terrain, const sl::TerrainView &view);

    bool project(const glm::mat4 &view_proj, const glm::vec3 &p, float &x, float &y);
} // namespace kosmos::app
