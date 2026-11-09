#undef NDEBUG
#include "orbiter_relief_colour_loader.h"

#include <cassert>
#include <cstdio>
#include <memory>

namespace
{
    class NaturalSource final : public sl::TerrainSource
    {
    public:
        double height(double, double) const override { return 0.0; }
        double min_height() const override { return 0.0; }
        double max_height() const override { return 0.0; }
    };
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: orbiter_relief_colour_test Surf.tree\n");
        return 2;
    }
    constexpr double radius = 3389500.0;
    const glm::dquat frame = glm::angleAxis(0.7, glm::normalize(glm::dvec3(1.0, 2.0, 3.0)));
    kosmos::OrbiterImagery image(argv[1], {frame}, radius);
    assert(image.ok());
    sl::Bitmap *original = kosmos::load_orbiter_relief_colour(image, nullptr, frame, 0, 0, 0);
    assert(original && original->width > 8 && original->height > 8);
    for (const int sx : {original->width / 5, original->width / 2, original->width * 4 / 5})
        for (const int sy : {original->height / 5, original->height / 2, original->height * 4 / 5})
        {
            const glm::dvec3 direction = kosmos::orbiter_tile_direction(
                (sx + 0.5) / original->width / 2.0, (sy + 0.5) / original->height);
            float ground[3];
            double resolution;
            assert(image.colour(frame * glm::dvec3(-direction.x, direction.y, -direction.z),
                                radius, ground, resolution));
            const std::size_t sample = (static_cast<std::size_t>(sy) * original->width + sx) * 4;
            for (int channel = 0; channel < 3; ++channel)
                assert(std::fabs(ground[channel] * 255.0f - original->pixels[sample + channel]) <= 2.0f);
        }
    const int x = original->width / 3, y = original->height / 3;
    const glm::dvec3 tile = kosmos::orbiter_tile_direction(
        (x + 0.5) / original->width / 2.0, (y + 0.5) / original->height);
    const glm::dvec3 centre = frame * glm::dvec3(-tile.x, tile.y, -tile.z);
    auto grid = std::make_shared<kosmos::MeshGrid>();
    grid->width = grid->height = 3;
    grid->cell = 10.0;
    grid->heights.assign(9, 0.0f);
    grid->colours.resize(27);
    for (std::size_t i = 0; i < grid->colours.size(); i += 3)
        grid->colours[i + 1] = 255;
    kosmos::MeshSource::Placed placed{};
    placed.origin = centre;
    placed.right = glm::normalize(glm::cross(centre, glm::dvec3(0, 1, 0)));
    placed.forward = glm::cross(centre, placed.right);
    placed.grid = grid;
    placed.use_colour = true;
    kosmos::MeshSource meshes(std::make_shared<NaturalSource>(), radius, {placed});
    sl::Bitmap *coloured = kosmos::load_orbiter_relief_colour(image, &meshes, frame, 0, 0, 0);
    assert(coloured && coloured->width == original->width && coloured->height == original->height);
    assert(coloured->pixels.size() == original->pixels.size());
    const std::size_t pixel = (static_cast<std::size_t>(y) * original->width + x) * 4;
    for (std::size_t i = 0; i < original->pixels.size(); ++i)
    {
        const auto expected = i == pixel ? 0 : i == pixel + 1 ? 255 : i == pixel + 2 ? 0 : original->pixels[i];
        assert(coloured->pixels[i] == expected);
    }
    assert(coloured->ram_dirty);
    sl::destroy_bitmap(coloured);
    sl::destroy_bitmap(original);
    std::puts("Orbiter DDS colour projection ok");
}
