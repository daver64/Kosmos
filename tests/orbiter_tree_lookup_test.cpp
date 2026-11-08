#undef NDEBUG
#include "planet_data.h"
#include "terrain_visibility.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>

int main(int argc, char **argv)
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() /
                          ("kosmos-tree-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path legacy = root / "Orbiter High Res Earth" / "Surf.tree";
    const fs::path canonical = root / "Earth" / "Archive" / "Surf.tree";
    const fs::path standard = root / "defaults" / "Moon" / "Archive" / "Elev.tree";
    for (const fs::path &file : {legacy, canonical, standard})
    {
        fs::create_directories(file.parent_path());
        std::ofstream(file) << "fixture";
    }
    assert(kosmos::OrbiterTree::find_layer(root.string(), "Earth", "Surf") == canonical.string());
    assert(kosmos::OrbiterTree::find_layer(root.string(), "Moon", "Elev") == standard.string());
    assert(kosmos::OrbiterTree::find_layer(root.string(), "Earth", "Elev").empty());
    fs::remove(canonical);
    assert(kosmos::OrbiterTree::find_layer(root.string(), "Earth", "Surf") == legacy.string());
    fs::remove_all(root);

    constexpr double mars_radius = 3389500.0;
    assert(!kosmos::app::terrain_visible(200000.0, mars_radius, true, false));
    assert(kosmos::app::terrain_visible(200000.0, mars_radius, true, true));
    assert(!kosmos::app::terrain_visible(2000000.0, mars_radius, true, true));
    assert(kosmos::app::terrain_visible(200000.0, mars_radius, false, false));
    assert(kosmos::app::orbiter_max_height(false, true) == 30000.0);
    assert(kosmos::app::orbiter_max_height(false, false) == 12000.0);
    assert(kosmos::app::orbiter_max_height(true, false) == 9000.0);

    if (argc > 1)
    {
        kosmos::OrbiterElevation elevation(argv[1], {}, mars_radius, -12000.0, kosmos::app::orbiter_max_height(false, true),
                                          std::numeric_limits<double>::quiet_NaN());
        assert(elevation.ok());
        constexpr double pi = 3.14159265358979323846;
        const auto direction = [&](double lat, double lon) {
            lat *= pi / 180.0;
            lon *= pi / 180.0;
            return glm::dvec3(std::cos(lat) * std::cos(lon), std::sin(lat), -std::cos(lat) * std::sin(lon));
        };
        const glm::dvec3 olympus = direction(18.65, -133.8);
        const double summit = elevation.height_on_sphere(olympus, mars_radius);
        const double orbital = elevation.height_on_sphere_lod(olympus, mars_radius, 10000.0);
        std::printf("Mars Olympus Mons elevation: %.1f m, orbital LOD: %.1f m\n", summit, orbital);
        assert(summit > 12000.0 && summit < 30000.0);
        assert(orbital > 10000.0);
    }
}
