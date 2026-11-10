#undef NDEBUG
#include "orbiter_microtexture.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>

int main(int argc, char **argv)
{
    assert(kosmos::orbiter_microtexture_near_surface(1020000.0, 1000000.0, 6000.0));
    assert(!kosmos::orbiter_microtexture_near_surface(1021000.0, 1000000.0, 6000.0));
    assert(kosmos::orbiter_microtexture_near_surface(1014999.0, 1000000.0, -1000.0));
    assert(!kosmos::orbiter_microtexture_near_surface(1015000.0, 1000000.0, -1000.0));
    assert(kosmos::orbiter_microtexture_near_surface(1021000.0, 1000000.0,
                                                      std::numeric_limits<double>::quiet_NaN()));
    using Config = std::unordered_map<std::string, kosmos::OrbiterMicrotextures>;
    Config bodies;
    std::string error;
    std::istringstream valid(
        " // comment\nBODY Moon\nNORMALS 1\nLEVEL 2 c.dds 0.8\nLEVEL 0 a.dds 42\nLEVEL 1 b.dds 6 // detail\n"
        "BODY Mars\nLEVEL 0 mars.dds 20\nNORMALS 0\nLEVEL 1 mars.dds 3\nLEVEL 2 mars.dds 1\n");
    assert(kosmos::parse_orbiter_microtextures(valid, bodies, error));
    assert(error.empty() && bodies.size() == 2);
    assert(bodies.at("Moon").normals && !bodies.at("Mars").normals);
    assert(bodies.at("Moon").levels[0].file == "a.dds" && bodies.at("Moon").levels[2].pixels_per_metre == 0.8);
    assert(bodies.at("Mars").levels[1].file == bodies.at("Mars").levels[2].file);

    for (const char *invalid : {
             "LEVEL 0 a.dds 1\n", "BODY Moon\nNORMALS 1\n", "BODY Moon\nNORMALS 2\n",
             "BODY Moon\nLEVEL 3 a.dds 1\n", "BODY Moon\nLEVEL -1 a.dds 1\n",
             "BODY Moon\nLEVEL 0 a.dds 0\n", "BODY Moon\nLEVEL 0 a.dds -1\n",
             "BODY Moon\nLEVEL 0 a.dds 1\nLEVEL 0 b.dds 2\n",
             "BODY Moon\nNORMALS 1\nNORMALS 0\n",
             "BODY Moon\nNORMALS 1\nLEVEL 0 a 1\nLEVEL 1 b 2\nLEVEL 2 c 3\n"
             "BODY Moon\nNORMALS 1\nLEVEL 0 a 1\nLEVEL 1 b 2\nLEVEL 2 c 3\n",
             "BODY Moon\nUNKNOWN 1\n", "BODY Moon extra\n"})
    {
        std::istringstream input(invalid);
        assert(!kosmos::parse_orbiter_microtextures(input, bodies, error));
        assert(!error.empty() && bodies.size() == 2 && bodies.at("Moon").levels[0].file == "a.dds");
    }
    if (argc > 1)
    {
        std::ifstream input(argv[1]);
        assert(input && kosmos::parse_orbiter_microtextures(input, bodies, error));
        assert(bodies.at("Moon").levels[0].pixels_per_metre == 42.0);
        assert(bodies.at("Mars").levels[2].pixels_per_metre == 1.0);
        kosmos::OrbiterMicrotextureAssets actual;
        const auto root = std::filesystem::path(argv[1]).parent_path().parent_path();
        assert(kosmos::discover_orbiter_microtextures(root, "Moon", actual, error) == kosmos::OrbiterMicrotextureDiscovery::available);
        assert(actual.paths[1] == root / "Textures" / "D3D9Moon_C2.dds");
        assert(kosmos::discover_orbiter_microtextures(root, "Mars", actual, error) == kosmos::OrbiterMicrotextureDiscovery::available);
        assert(actual.paths[1] == actual.paths[2]);
        assert(kosmos::discover_orbiter_microtextures(root, "Earth", actual, error) == kosmos::OrbiterMicrotextureDiscovery::not_configured);
    }

    namespace fs = std::filesystem;
    const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const fs::path root = fs::temp_directory_path() / ("kosmos_microtexture_test_" + unique);
    assert(fs::create_directory(root));
    kosmos::OrbiterMicrotextureAssets assets;
    assets.config.levels[0].file = "unchanged";
    assert(kosmos::discover_orbiter_microtextures(root, "Moon", assets, error) == kosmos::OrbiterMicrotextureDiscovery::not_configured);
    assert(error.empty() && assets.config.levels[0].file == "unchanged");
    assert(fs::create_directory(root / "Config"));
    assert(fs::create_directory(root / "Textures"));
    const auto write_config = [&](const std::string &filename)
    {
        std::ofstream out(root / "Config" / "MicroTex.cfg");
        out << "BODY Moon\nNORMALS 1\nLEVEL 0 " << filename << " 42\nLEVEL 1 b.dds 6\nLEVEL 2 b.dds 1\n";
        assert(out.good());
    };
    write_config("a.dds");
    assert(kosmos::discover_orbiter_microtextures(root, "Earth", assets, error) == kosmos::OrbiterMicrotextureDiscovery::not_configured);
    assert(kosmos::discover_orbiter_microtextures(root, "Moon", assets, error) == kosmos::OrbiterMicrotextureDiscovery::error);
    assert(error.find("a.dds") != std::string::npos && assets.config.levels[0].file == "unchanged");
    std::ofstream(root / "Textures" / "a.dds").put('a');
    std::ofstream(root / "Textures" / "b.dds").put('b');
    assert(kosmos::discover_orbiter_microtextures(root, "Moon", assets, error) == kosmos::OrbiterMicrotextureDiscovery::available);
    assert(error.empty() && assets.config.normals && assets.paths[0] == root / "Textures" / "a.dds" &&
           assets.paths[1] == assets.paths[2]);
    write_config("../a.dds");
    assert(kosmos::discover_orbiter_microtextures(root, "Moon", assets, error) == kosmos::OrbiterMicrotextureDiscovery::error);
    assert(error.find("invalid texture filename") != std::string::npos && assets.config.levels[0].file == "a.dds");
    write_config("/absolute.dds");
    assert(kosmos::discover_orbiter_microtextures(root, "Moon", assets, error) == kosmos::OrbiterMicrotextureDiscovery::error);
    write_config("sub\\a.dds");
    assert(kosmos::discover_orbiter_microtextures(root, "Moon", assets, error) == kosmos::OrbiterMicrotextureDiscovery::error);
    write_config("Config");
    assert(fs::create_directory(root / "Textures" / "Config"));
    assert(kosmos::discover_orbiter_microtextures(root, "Moon", assets, error) == kosmos::OrbiterMicrotextureDiscovery::error);
    assert(fs::remove(root / "Textures" / "Config"));
    {
        std::ofstream out(root / "Config" / "MicroTex.cfg");
        out << "BODY Moon\n";
    }
    assert(kosmos::discover_orbiter_microtextures(root, "Moon", assets, error) == kosmos::OrbiterMicrotextureDiscovery::error);
    assert(error.find("incomplete body") != std::string::npos && assets.config.levels[0].file == "a.dds");
    assert(fs::remove_all(root) == 6);

    kosmos::OrbiterMicrotextureRange parent, child, next;
    assert(kosmos::orbiter_microtexture_range(6, 21, 17, 0, 0, 1737400.0, 2048, 42.0, parent, error));
    assert(kosmos::orbiter_microtexture_range(8, 86, 69, 2, 0, 1737400.0, 2048, 42.0, child, error));
    assert(kosmos::orbiter_microtexture_range(8, 87, 69, 2, 0, 1737400.0, 2048, 42.0, next, error));
    assert(std::fabs(child.u_scale * 4 - parent.u_scale) < 1e-9);
    assert(std::fabs(child.v_scale * 4 - parent.v_scale) < 1e-9);
    const auto fraction = [](double x) { return x - std::floor(x); };
    assert(std::fabs(fraction(child.u_offset + child.u_scale) - next.u_offset) < 1e-9);
    assert(std::fabs(child.v_offset - next.v_offset) < 1e-9);
    for (int layer = 0; layer < 3; ++layer)
    {
        assert(kosmos::orbiter_microtexture_range(6, 0, 63, 0, layer, 1737400.0, 2048, 42.0, child, error));
        const int multiple = layer == 0 ? 16 : layer == 1 ? 8 : 2;
        assert(std::fabs(child.u_scale - multiple) < 1e-8);
        assert(std::fmod(child.v_scale, multiple) == 0.0);
    }
    const auto unchanged = child;
    assert(!kosmos::orbiter_microtexture_range(6, 128, 0, 0, 0, 1737400.0, 2048, 42.0, child, error));
    assert(!error.empty() && child.u_scale == unchanged.u_scale);
    assert(!kosmos::orbiter_microtexture_range(6, 0, 0, 7, 0, 1737400.0, 2048, 42.0, child, error));
    assert(!kosmos::orbiter_microtexture_range(6, 0, 0, 0, 0, 1737400.0, 2048, 0.0, child, error));
    assert(!kosmos::orbiter_microtexture_range(6, 0, 0, 0, 0, std::numeric_limits<double>::infinity(), 2048, 1.0, child, error));
    assert(!kosmos::orbiter_microtexture_range(6, 0, 0, 0, 0, std::numeric_limits<double>::max(), 1,
                                              std::numeric_limits<double>::max(), child, error));
}
