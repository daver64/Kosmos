#undef NDEBUG
#include "base.h"
#include "orbiter_tile_terrain.h"
#include "orbiter_tile_colour.h"
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
    assert(mesh->height_on_sphere(-origin, radius) == 10.0);
    assert(mesh->height_on_sphere_lod(-origin, radius, 20.0) == 30.0);
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

    {
        constexpr int level = 2, column = 1, row = 1, size = 8, target_x = 4, target_y = 3;
        const double rows = std::ldexp(1.0, level);
        const glm::dvec3 tile = kosmos::orbiter_tile_direction(
            (column + (target_x + 0.5) / size) / (2.0 * rows),
            (row + (target_y + 0.5) / size) / rows);
        const glm::dvec3 centre = initial * glm::dvec3(-tile.x, tile.y, -tile.z);
        auto coloured = std::make_shared<kosmos::MeshGrid>();
        coloured->width = coloured->height = 5;
        coloured->cell = 1000.0;
        coloured->heights.assign(25, 0.0f);
        coloured->colours.resize(75);
        for (std::size_t k = 0; k < coloured->colours.size(); k += 3)
            coloured->colours[k] = 255;
        kosmos::MeshSource::Placed placement{};
        placement.origin = centre;
        placement.right = glm::normalize(glm::cross(centre, glm::dvec3(0, 1, 0)));
        placement.forward = glm::cross(centre, placement.right);
        placement.grid = coloured;
        placement.use_colour = true;
        kosmos::MeshSource coloured_source(natural, radius, {placement});
        assert(coloured_source.colour_may_overlap(centre, 0.0));
        assert(!coloured_source.colour_may_overlap(-centre, 0.0));
        assert(coloured_source.colour_may_overlap(-centre, glm::pi<double>()));
        assert(!coloured_source.colour_may_overlap(placement.right, 0.1));
        float opposite_rgb[3] = {0.25f, 0.25f, 0.25f};
        assert(!coloured_source.colour(-centre, opposite_rgb));
        assert(opposite_rgb[0] == 0.25f && opposite_rgb[1] == 0.25f && opposite_rgb[2] == 0.25f);
        sl::Bitmap bitmap;
        bitmap.width = bitmap.height = size;
        bitmap.pixels.resize(size * size * 4, 64);
        for (std::size_t k = 3; k < bitmap.pixels.size(); k += 4)
            bitmap.pixels[k] = 137;
        assert(kosmos::apply_orbiter_mesh_colours(bitmap, coloured_source, initial, level, column, row));
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                const std::size_t k = static_cast<std::size_t>(y * size + x) * 4;
                const bool target = x == target_x && y == target_y;
                assert(bitmap.pixels[k] == (target ? 255 : 64));
                assert(bitmap.pixels[k + 1] == (target ? 0 : 64));
                assert(bitmap.pixels[k + 2] == (target ? 0 : 64));
                assert(bitmap.pixels[k + 3] == 137);
            }
        placement.x = 1000.0;
        placement.blend = 2000.0;
        kosmos::MeshSource blended_source(natural, radius, {placement});
        std::fill(bitmap.pixels.begin(), bitmap.pixels.end(), 64);
        assert(kosmos::apply_orbiter_mesh_colours(bitmap, blended_source, initial, level, column, row));
        const std::size_t k = static_cast<std::size_t>(target_y * size + target_x) * 4;
        assert(bitmap.pixels[k] == 160 && bitmap.pixels[k + 1] == 32 && bitmap.pixels[k + 2] == 32);
        assert(bitmap.pixels[k + 3] == 64);
        for (int l = 0; l <= 12; ++l)
        {
            const int r = 1 << l, cols = 2 * r;
            for (int c : {0, cols / 2, cols - 1})
                for (int t : {0, r / 2, r - 1})
                {
                    const glm::dvec3 middle = kosmos::orbiter_tile_direction((c + 0.5) / cols, (t + 0.5) / r);
                    for (int y : {0, size - 1})
                        for (int x : {0, size - 1})
                        {
                            const glm::dvec3 p = kosmos::orbiter_tile_direction(
                                (c + (x + 0.5) / size) / cols, (t + (y + 0.5) / size) / r);
                            kosmos::MeshSource::Placed sample = placement;
                            sample.origin = p;
                            kosmos::MeshSource bound_source(natural, radius, {sample});
                            assert(bound_source.colour_may_overlap(middle, glm::pi<double>() / r));
                        }
                }
        }
        bitmap.pixels.clear();
        assert(!kosmos::apply_orbiter_mesh_colours(bitmap, coloured_source, initial, level, column, row));
    }
}
