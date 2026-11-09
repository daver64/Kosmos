#undef NDEBUG
#include "base.h"
#include "orbiter_tile_terrain.h"
#include "terrain_mesh.h"

#include <cassert>
#include <cmath>
#include <memory>

namespace
{
    class NaturalSource final : public sl::TerrainSource
    {
    public:
        double height(double, double) const override { return 10.0; }
        double height_on_sphere(const sl::dvec3 &, double) const override { return 10.0; }
        double height_on_sphere_lod(const sl::dvec3 &, double, double cell) const override { return 10.0 + cell; }
        double min_height() const override { return 10.0; }
        double max_height() const override { return 100.0; }
    };
}

int main()
{
    constexpr double radius = 6371000.0;
    const glm::dvec3 origin(1.0, 0.0, 0.0), right(0.0, 0.0, 1.0), forward(0.0, 1.0, 0.0);
    auto grid = std::make_shared<kosmos::MeshGrid>();
    grid->width = grid->height = 5;
    grid->cell = 10.0;
    grid->heights.assign(25, 4.0f);
    grid->max_height = 9.0;
    kosmos::MeshSource::Placed placed{};
    placed.origin = origin;
    placed.right = right;
    placed.forward = forward;
    placed.datum = 100.0;
    placed.blend = 4.0;
    placed.grid = grid;
    placed.mips = {std::vector<float>(9, 8.0f), std::vector<float>(4, 9.0f)};
    auto natural = std::make_shared<NaturalSource>();
    auto mesh = std::make_shared<kosmos::MeshSource>(natural, radius, std::vector<kosmos::MeshSource::Placed>{placed});
    auto final_source = std::make_shared<kosmos::FlattenSource>(
        mesh, radius, std::vector<kosmos::FlattenSite>{{origin, origin, 200.0, 3.0, 2.0}});

    sl::TerrainConfig config;
    config.shape = sl::TerrainShape::sphere;
    config.radius = radius;
    config.worker_threads = 0;
    sl::Terrain collision(config, final_source);
    const glm::dvec3 axis = glm::normalize(glm::dvec3(0.2, 0.9, -0.3));
    const glm::dquat initial = glm::angleAxis(0.9, glm::normalize(glm::dvec3(1.0, 2.0, 3.0)));
    for (double angle : {0.0, 0.7, -1.2})
    {
        collision.set_rotation(angle, axis);
        const glm::dquat turn = glm::angleAxis(angle, axis);
        struct Sample { double offset, full, coarse; };
        for (const Sample sample : {Sample{0.0, 200.0, 200.0}, {4.0, 152.0, 154.0},
                                    {10.0, 104.0, 108.0}, {18.0, 57.0, 69.0}, {30.0, 10.0, 30.0}})
        {
            const glm::dvec3 local = glm::normalize(origin + right * (sample.offset / radius));
            const glm::dvec3 body = glm::inverse(initial) * local;
            const glm::dvec3 tile(-body.x, body.y, -body.z);
            const glm::dvec3 world = kosmos::orbiter_tile_orientation(turn * initial) * tile;
            const double collision_height = collision.height_at_direction(world);
            const double visual = kosmos::orbiter_tile_height(*final_source, initial, tile, radius, 0.0) * radius;
            assert(std::fabs(collision_height - sample.full) < 1e-5);
            assert(std::fabs(visual - collision_height) < 1e-5);
            const double lod_visual = kosmos::orbiter_tile_height(*final_source, initial, tile, radius, 20.0) * radius;
            assert(std::fabs(lod_visual - sample.coarse) < 1e-5);
        }
    }
}
