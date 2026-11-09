#undef NDEBUG
#include "planet_data.h"
#include "orbiter_tile_terrain.h"
#include "terrain_visibility.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <thread>
#include <zlib.h>

static void test_shared_terrain_frame()
{
    class Terrain final : public sl::TerrainSource
    {
    public:
        mutable glm::dvec3 direction{0.0};
        mutable double radius = 0.0, cell = 0.0;
        double height(double, double) const override { return 0.0; }
        double height_on_sphere_lod(const glm::dvec3 &d, double r, double c) const override
        {
            direction = d;
            radius = r;
            cell = c;
            return 250.0;
        }
        double min_height() const override { return 0.0; }
        double max_height() const override { return 250.0; }
    } terrain;
    const glm::dquat frame = glm::angleAxis(0.7, glm::normalize(glm::dvec3(1.0, 2.0, 3.0)));
    const glm::dvec3 tile = glm::normalize(glm::dvec3(0.3, 0.7, -0.5));
    const glm::dvec3 expected = frame * glm::dvec3(-tile.x, tile.y, -tile.z);
    assert(kosmos::orbiter_tile_height(terrain, frame, tile, 5000.0, 12.0) == 0.05);
    assert(glm::length(terrain.direction - expected) < 1e-12);
    assert(terrain.radius == 5000.0 && terrain.cell == 12.0);
}

static void test_collision_frame()
{
    class Terrain final : public sl::TerrainSource
    {
    public:
        double height(double, double) const override { return 0.0; }
        double height_on_sphere(const glm::dvec3 &d, double) const override
        {
            return 100.0 * d.x + 200.0 * d.y + 300.0 * d.z;
        }
        double height_on_sphere_lod(const glm::dvec3 &d, double radius, double) const override
        {
            return height_on_sphere(d, radius);
        }
        double min_height() const override { return -400.0; }
        double max_height() const override { return 400.0; }
    };
    constexpr double radius = 6371000.0;
    auto source = std::make_shared<Terrain>();
    sl::TerrainConfig config;
    config.shape = sl::TerrainShape::sphere;
    config.radius = radius;
    config.worker_threads = 0;
    sl::Terrain terrain(config, source);
    const glm::dvec3 axis = glm::normalize(glm::dvec3(0.2, 0.9, -0.3));
    const glm::dquat initial = glm::angleAxis(0.8, axis) *
                               glm::angleAxis(1.1, glm::dvec3(0.0, 1.0, 0.0));
    for (double angle : {0.0, 0.7, -1.2})
    {
        terrain.set_rotation(angle, axis);
        const glm::dquat current = glm::angleAxis(angle, axis) * initial;
        for (const glm::dvec3 tile : {glm::normalize(glm::dvec3(0.3, 0.7, -0.5)),
                                       glm::normalize(glm::dvec3(-0.9, 0.2, 0.1))})
        {
            const glm::dvec3 world = kosmos::orbiter_tile_orientation(current) * tile;
            const double collision = terrain.height_at_direction(world);
            const double visual = kosmos::orbiter_tile_height(*source, initial, tile, radius, 0.0) * radius;
            assert(std::fabs(collision - visual) < 1e-8);
        }
    }
}

static void test_tile_cache_budget()
{
    struct Tile
    {
        std::size_t bytes;
        std::size_t memory_bytes() const { return bytes; }
    };
    kosmos::TileCache<Tile> cache(3, 10, [](const Tile &tile) { return tile.memory_bytes(); });
    int loads = 0;
    const auto get = [&](std::uint64_t key, std::size_t bytes)
    {
        return cache.get(key, [&]
        {
            ++loads;
            return std::make_shared<Tile>(Tile{bytes});
        });
    };
    const auto held = get(1, 4);
    get(2, 4);
    assert(get(1, 4) == held && loads == 2);
    get(3, 4);
    assert(loads == 3 && held->bytes == 4);
    get(2, 4);
    assert(loads == 4);
    get(4, 11);
    get(4, 11);
    assert(loads == 6);
    get(5, 0);
    get(6, 0);
    get(7, 0);
    get(8, 0);
    assert(loads == 10);
    get(5, 0);
    assert(loads == 11);
}

static void test_index_limits(const std::filesystem::path &path)
{
    const auto rejected = [&](std::uint32_t count, std::uint32_t offset, std::uint64_t length,
                              const std::string &expected_error)
    {
        std::uint8_t header[48] = {};
        std::memcpy(header, "TX\x01\x00", 4);
        const std::uint32_t header_size = 48;
        std::memcpy(header + 4, &header_size, 4);
        std::memcpy(header + 12, &offset, 4);
        std::memcpy(header + 16, &length, 8);
        std::memcpy(header + 24, &count, 4);
        {
            std::ofstream out(path, std::ios::binary);
            out.write(reinterpret_cast<const char *>(header), sizeof header);
            assert(out.good());
        }
        kosmos::OrbiterTree tree;
        std::string error;
        assert(!tree.open(path.string(), error));
        assert(error.find(expected_error) != std::string::npos);
    };
    rejected((128u << 20) / 32 + 1, 48, 0, "128 MiB limit");
    rejected(1, 48, 0, "data bounds");
    rejected(0, 49, 0, "data bounds");
    rejected(0, 48, 1, "data bounds");
    rejected(0, 48, std::numeric_limits<std::uint64_t>::max(), "data bounds");
}

static void test_ancestor(const std::filesystem::path &path)
{
    struct Node
    {
        std::int64_t pos = 0;
        std::uint32_t size = 0;
        std::uint32_t child[4] = {kosmos::OrbiterTree::none, kosmos::OrbiterTree::none,
                                  kosmos::OrbiterTree::none, kosmos::OrbiterTree::none};
        std::uint32_t pad = 0;
    };
    static_assert(sizeof(Node) == 32);
    std::uint8_t header[48] = {};
    std::memcpy(header, "TX\x01\x00", 4);
    const std::uint32_t header_size = 48, count = 6, missing = kosmos::OrbiterTree::none;
    const std::uint32_t data_offset = 48 + count * sizeof(Node);
    const std::uint32_t first_root = 0, second_root = 1;
    std::memcpy(header + 4, &header_size, 4);
    std::memcpy(header + 12, &data_offset, 4);
    std::memcpy(header + 24, &count, 4);
    for (int i = 0; i < 3; ++i)
        std::memcpy(header + 28 + i * 4, &missing, 4);
    std::memcpy(header + 40, &first_root, 4);
    std::memcpy(header + 44, &second_root, 4);
    Node nodes[6];
    nodes[0].size = nodes[1].size = nodes[2].size = nodes[3].size = 1;
    nodes[0].child[3] = 2;
    nodes[2].child[1] = 3;
    nodes[0].child[0] = 4;
    nodes[4].child[3] = 5;
    std::vector<std::uint8_t> dds(136, 0);
    std::memcpy(dds.data(), "DDS ", 4);
    const std::uint32_t dimension = 4;
    const std::uint32_t pixel_flags = 4;
    std::memcpy(dds.data() + 12, &dimension, 4);
    std::memcpy(dds.data() + 16, &dimension, 4);
    std::memcpy(dds.data() + 80, &pixel_flags, 4);
    std::memcpy(dds.data() + 84, "DXT1", 4);
    std::vector<std::uint8_t> compressed;
    for (int i = 0; i < 6; ++i)
    {
        nodes[i].pos = static_cast<std::int64_t>(compressed.size());
        if (i == 4)
            continue;
        std::vector<std::uint8_t> payload = dds;
        if (i == 2)
            payload.resize(8);
        nodes[i].size = static_cast<std::uint32_t>(payload.size());
        uLongf length = compressBound(static_cast<uLong>(payload.size()));
        std::vector<std::uint8_t> packed(length);
        assert(compress2(packed.data(), &length, payload.data(), static_cast<uLong>(payload.size()), Z_DEFAULT_COMPRESSION) == Z_OK);
        compressed.insert(compressed.end(), packed.begin(), packed.begin() + static_cast<std::ptrdiff_t>(length));
    }
    const std::uint64_t data_length = compressed.size();
    std::memcpy(header + 16, &data_length, 8);
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char *>(header), sizeof header);
        out.write(reinterpret_cast<const char *>(nodes), sizeof nodes);
        out.write(reinterpret_cast<const char *>(compressed.data()), static_cast<std::streamsize>(compressed.size()));
        assert(out.good());
    }
    kosmos::OrbiterTree tree;
    std::string error;
    assert(tree.open(path.string(), error));
    auto tile = tree.ancestor(7, 4, 6);
    assert(tile.node == 3 && tile.level == 6 && tile.row == 2 && tile.column == 3);
    assert(tile.u0 == 0.0 && tile.v0 == 0.0 && tile.u1 == 0.5 && tile.v1 == 0.5);
    tile = tree.ancestor(7, 7, 5);
    assert(tile.node == 2 && tile.level == 5 && tile.row == 1 && tile.column == 1);
    assert(tile.u0 == 0.25 && tile.v0 == 0.75 && tile.u1 == 0.5 && tile.v1 == 1.0);
    tile = tree.ancestor(4, 0, 1);
    assert(tile.node == 1 && tile.u0 == 0.0 && tile.v0 == 0.0 && tile.u1 == 1.0 && tile.v1 == 1.0);
    assert(tree.ancestor(3, 0, 0).node == kosmos::OrbiterTree::none);
    assert(tree.ancestor(23, 0, 0).node == kosmos::OrbiterTree::none);
    assert(tree.ancestor(7, 8, 0).node == kosmos::OrbiterTree::none);
    assert(tree.ancestor(7, 0, 16).node == kosmos::OrbiterTree::none);
    assert(tree.ancestor(7, -1, 0).node == kosmos::OrbiterTree::none);

    kosmos::OrbiterImagery imagery(path.string(), {}, 6371000.0);
    assert(imagery.ok());
    assert(imagery.has_tile(4, 0, 0));
    assert(imagery.has_tile(6, 2, 3));
    assert(!imagery.has_tile(6, 3, 3));
    assert(!imagery.has_tile(5, 0, 0));
    assert(imagery.has_children(5, 0, 0));
    assert(imagery.has_tile(6, 1, 1));
    assert(!imagery.has_children(6, 1, 1));
    assert(!imagery.has_children(23, 0, 0));
    assert(!imagery.has_tile(23, 0, 0));
    std::vector<std::uint8_t> image;
    assert(imagery.read_tile(7, 4, 6, tile, image));
    assert(tile.node == 3 && tile.level == 6 && tile.u0 == 0.0 && tile.v0 == 0.0 && tile.u1 == 0.5 && tile.v1 == 0.5);
    assert(image == dds);
    assert(!imagery.read_tile(7, 4, 6, tile, image, dds.size() - 1));
    assert(image.empty());
    assert(imagery.read_tile(7, 4, 6, tile, image, dds.size()));
    assert(image == dds);
    assert(imagery.read_tile(7, 7, 5, tile, image));
    assert(tile.node == 0 && tile.level == 4 && tile.u0 == 0.625 && tile.v0 == 0.875 && tile.u1 == 0.75 && tile.v1 == 1.0);
    assert(image == dds);
    assert(imagery.read_tile(5, 0, 0, tile, image));
    assert(tile.node == 0 && tile.level == 4 && tile.u0 == 0.0 && tile.v0 == 0.0 && tile.u1 == 0.5 && tile.v1 == 0.5);
    assert(imagery.read_tile(6, 1, 1, tile, image));
    assert(tile.node == 5 && tile.level == 6 && image == dds);
    constexpr double pi = 3.14159265358979323846;
    const double latitude = -67.5 * pi / 180.0, longitude = -56.25 * pi / 180.0;
    const glm::dvec3 corrupt_direction(std::cos(latitude) * std::cos(longitude), std::sin(latitude),
                                       -std::cos(latitude) * std::sin(longitude));
    float rgb[3];
    double resolution = 0.0;
    assert(imagery.colour(corrupt_direction, 1.0, rgb, resolution));
    assert(std::fabs(resolution - pi * 6371000.0 / 4.0) < 0.01);
    assert(!imagery.read_tile(23, 0, 0, tile, image));
    assert(image.empty());

    const glm::dquat frame = glm::angleAxis(0.7, glm::normalize(glm::dvec3(1.0, 2.0, 3.0)));
    kosmos::OrbiterImagery shared(path.string(), {frame}, 6371000.0);
    assert(shared.ok());
    const double lat = -22.5 * pi / 180.0, lon = -22.5 * pi / 180.0;
    const glm::dvec3 direction = frame * glm::dvec3(std::cos(lat) * std::cos(lon), std::sin(lat),
                                                    -std::cos(lat) * std::sin(lon));
    std::thread ground([&]
    {
        for (int i = 0; i < 32; ++i)
        {
            float colour[3];
            double metres = 0.0;
            assert(shared.colour(direction, 1.0, colour, metres));
            assert(std::fabs(metres - pi * 6371000.0 / 16.0) < 0.01);
        }
    });
    std::thread orbital([&]
    {
        for (int i = 0; i < 32; ++i)
        {
            kosmos::OrbiterTree::Tile exact;
            std::vector<std::uint8_t> bytes;
            assert(shared.has_tile(6, 2, 3));
            assert(shared.read_tile(6, 2, 3, exact, bytes, 2u << 20));
            assert(exact.level == 6 && bytes == dds);
        }
    });
    ground.join();
    orbital.join();

    const auto scenery = path.parent_path() / "individual-scenery";
    const auto archive = scenery / "Archive" / "Surf.tree";
    std::filesystem::create_directories(archive.parent_path());
    std::filesystem::copy_file(path, archive);
    const auto override_dir = scenery / "Surf" / "08" / "000011";
    std::filesystem::create_directories(override_dir);
    std::vector<std::uint8_t> red = dds;
    red[128] = 0x00;
    red[129] = 0xf8;
    {
        std::ofstream out(override_dir / "000013.dds", std::ios::binary);
        out.write(reinterpret_cast<const char *>(red.data()), static_cast<std::streamsize>(red.size()));
        assert(out.good());
    }
    for (int kind = 0; kind < 4; ++kind)
    {
        std::vector<std::uint8_t> image(dds.begin(), dds.begin() + 128);
        std::memcpy(image.data() + 84, kind == 0 ? "DXT3" : "DX10", 4);
        if (kind > 0)
        {
            image.resize(148, 0);
            const std::uint32_t format = kind == 1 ? 71 : kind == 2 ? 77 : 74;
            const std::uint32_t dimension = 3, array_size = 1;
            std::memcpy(image.data() + 128, &format, 4);
            std::memcpy(image.data() + 132, &dimension, 4);
            std::memcpy(image.data() + 140, &array_size, 4);
        }
        if (kind != 1)
            image.insert(image.end(), 8, 255);
        image.insert(image.end(), red.begin() + 128, red.end());
        char name[16];
        std::snprintf(name, sizeof name, "%06d.dds", kind == 3 ? 18 : 14 + kind);
        const auto output = override_dir / name;
        std::ofstream out(output, std::ios::binary);
        out.write(reinterpret_cast<const char *>(image.data()), static_cast<std::streamsize>(image.size()));
        assert(out.good());
    }
    for (int kind = 0; kind < 3; ++kind)
    {
        std::vector<std::uint8_t> invalid(dds.begin(), dds.begin() + 128);
        std::memcpy(invalid.data() + 84, "DX10", 4);
        if (kind == 2)
            invalid.resize(140, 0);
        else
        {
            invalid.resize(148, 0);
            const std::uint32_t format = 71, dimension = kind == 0 ? 4 : 3, array_size = kind == 1 ? 2 : 1;
            std::memcpy(invalid.data() + 128, &format, 4);
            std::memcpy(invalid.data() + 132, &dimension, 4);
            std::memcpy(invalid.data() + 140, &array_size, 4);
            invalid.insert(invalid.end(), red.begin() + 128, red.end());
        }
        char name[16];
        std::snprintf(name, sizeof name, "%06d.dds", kind == 0 ? 17 : 18 + kind);
        std::ofstream out(override_dir / name, std::ios::binary);
        out.write(reinterpret_cast<const char *>(invalid.data()), static_cast<std::streamsize>(invalid.size()));
        assert(out.good());
    }
    const auto root_override = scenery / "Surf" / "04" / "000000";
    std::filesystem::create_directories(root_override);
    {
        std::ofstream out(root_override / "000000.dds", std::ios::binary);
        out.write(reinterpret_cast<const char *>(red.data()), static_cast<std::streamsize>(red.size()));
        assert(out.good());
    }
    std::vector<std::uint8_t> large(red.begin(), red.begin() + 128);
    const std::uint32_t large_dimension = 8;
    std::memcpy(large.data() + 12, &large_dimension, 4);
    std::memcpy(large.data() + 16, &large_dimension, 4);
    for (int i = 0; i < 4; ++i)
        large.insert(large.end(), red.begin() + 128, red.end());
    {
        std::ofstream out(root_override / "000001.dds", std::ios::binary);
        out.write(reinterpret_cast<const char *>(large.data()), static_cast<std::streamsize>(large.size()));
        assert(out.good());
    }
    const auto bad_dir = scenery / "Surf" / "06" / "000002";
    std::filesystem::create_directories(bad_dir);
    {
        std::ofstream out(bad_dir / "000003.dds", std::ios::binary);
        out << "invalid DDS";
    }
    {
        std::ofstream out(bad_dir / "000002.dds", std::ios::binary);
        out.write(reinterpret_cast<const char *>(large.data()), static_cast<std::streamsize>(large.size()));
        assert(out.good());
    }
    kosmos::OrbiterImagery individual(archive.string(), {}, 6371000.0);
    assert(individual.ok());
    assert(individual.has_children(4, 0, 0));
    assert(individual.has_children(7, 5, 6));
    assert(individual.has_tile(8, 11, 13));
    assert(individual.read_tile(4, 0, 0, tile, image));
    assert(tile.node == kosmos::OrbiterTree::none && tile.level == 4 && image == red);
    assert(individual.read_tile(4, 0, 1, tile, image) && tile.node == kosmos::OrbiterTree::none && image == large);
    assert(individual.read_tile(4, 0, 1, tile, image, 2u << 20, 8u * 8u * 4u) && image == large);
    assert(individual.read_tile(4, 0, 1, tile, image, 2u << 20, 4u * 4u * 4u) &&
           tile.node == 1 && tile.level == 4 && image == dds);
    assert(!individual.read_tile(4, 0, 1, tile, image, 2u << 20, 4u * 4u * 4u - 1));
    assert(individual.read_tile(6, 2, 2, tile, image, 2u << 20, 4u * 4u * 4u) &&
           tile.level == 4 && tile.u0 == 0.5 && tile.v0 == 0.5 && image == red);
    assert(individual.read_tile(9, 22, 27, tile, image));
    assert(tile.node == kosmos::OrbiterTree::none && tile.level == 8 &&
           tile.u0 == 0.5 && tile.v0 == 0.0 && image == red);
    assert(individual.read_tile(6, 2, 3, tile, image));
    assert(tile.node == 3 && tile.level == 6 && image == dds);
    const double override_lat = pi * (0.5 - 11.5 / 16.0);
    const double override_lon = pi * (13.5 / 16.0 - 1.0);
    const glm::dvec3 override_direction(std::cos(override_lat) * std::cos(override_lon), std::sin(override_lat),
                                        -std::cos(override_lat) * std::sin(override_lon));
    assert(individual.colour(override_direction, 1.0, rgb, resolution));
    assert(rgb[0] > 0.99f && rgb[1] == 0.0f && rgb[2] == 0.0f);
    assert(std::fabs(resolution - pi * 6371000.0 / 64.0) < 0.01);
    for (int column : {14, 15, 16, 18})
    {
        const double lon = pi * ((column + 0.5) / 16.0 - 1.0);
        const glm::dvec3 d(std::cos(override_lat) * std::cos(lon), std::sin(override_lat),
                           -std::cos(override_lat) * std::sin(lon));
        assert(individual.has_tile(8, 11, column));
        assert(individual.read_tile(8, 11, column, tile, image) && tile.level == 8);
        assert(individual.colour(d, 1.0, rgb, resolution));
        assert(rgb[0] > 0.99f && rgb[1] == 0.0f && rgb[2] == 0.0f);
    }
    for (int column : {17, 19, 20})
        assert(individual.read_tile(8, 11, column, tile, image) && tile.level < 8);
    assert(!individual.read_tile(8, 11, 13, tile, image, red.size() - 1));
    assert(image.empty());
    std::filesystem::remove(archive);
    kosmos::OrbiterImagery individual_only(archive.string(), {}, 6371000.0);
    assert(individual_only.ok());
    assert(individual_only.read_tile(8, 11, 13, tile, image) && tile.level == 8 && image == red);
    assert(individual_only.colour(override_direction, 1.0, rgb, resolution));
    assert(rgb[0] > 0.99f && rgb[1] == 0.0f && rgb[2] == 0.0f);
    std::ofstream(archive, std::ios::binary) << "bad archive";
    kosmos::OrbiterImagery corrupt_archive(archive.string(), {}, 6371000.0);
    assert(corrupt_archive.ok() && corrupt_archive.read_tile(8, 11, 13, tile, image) && image == red);
}

static void test_elevation_fallback(const std::filesystem::path &path)
{
    struct Node
    {
        std::int64_t pos = 0;
        std::uint32_t size = 0;
        std::uint32_t child[4] = {kosmos::OrbiterTree::none, kosmos::OrbiterTree::none,
                                  kosmos::OrbiterTree::none, kosmos::OrbiterTree::none};
        std::uint32_t pad = 0;
    };
    static_assert(sizeof(Node) == 32);
    std::uint8_t header[48] = {};
    std::memcpy(header, "TX\x01\x00", 4);
    const std::uint32_t header_size = 48, count = 11, missing = kosmos::OrbiterTree::none;
    const std::uint32_t data_offset = 48 + count * sizeof(Node), first_root = 0;
    std::memcpy(header + 4, &header_size, 4);
    std::memcpy(header + 12, &data_offset, 4);
    std::memcpy(header + 24, &count, 4);
    for (int i = 0; i < 3; ++i)
        std::memcpy(header + 28 + i * 4, &missing, 4);
    std::memcpy(header + 40, &first_root, 4);
    std::memcpy(header + 44, &missing, 4);
    Node nodes[11];
    for (int i = 0; i < 4; ++i)
        nodes[0].child[i] = i + 1;
    nodes[4].child[0] = 5;
    nodes[4].child[1] = 6;
    nodes[4].child[2] = 7;
    nodes[4].child[3] = 8;
    nodes[7].child[0] = 9;
    nodes[8].child[0] = 10;

    std::vector<std::uint8_t> compressed;
    for (int i = 0; i < 11; ++i)
    {
        const std::int32_t size = i == 1 ? -1 : 100;
        const std::int32_t type = i == 3 || i == 5 ? 8 : i == 6 ? -16 : i == 7 || i == 9 ? -8 : i == 8 || i == 10 ? 16 : 0;
        const std::int32_t dimension = i == 2 ? std::numeric_limits<std::int32_t>::max() : 259;
        const double scale = i == 8 ? 0.1 : 2.0, offset = i == 0 ? 123.0 : 0.0;
        std::vector<std::uint8_t> payload(100, 0);
        std::memcpy(payload.data(), "ELE\x01", 4);
        std::memcpy(payload.data() + 4, &size, 4);
        std::memcpy(payload.data() + 8, &type, 4);
        std::memcpy(payload.data() + 12, &dimension, 4);
        std::memcpy(payload.data() + 16, &dimension, 4);
        std::memcpy(payload.data() + 28, &scale, 8);
        std::memcpy(payload.data() + 36, &offset, 8);
        if (i == 5)
            payload.resize(100 + 259 * 259, 7);
        if (i == 6)
        {
            const std::int16_t sample = -4;
            payload.resize(100 + 2 * 259 * 259);
            for (std::size_t p = 100; p < payload.size(); p += 2)
                std::memcpy(payload.data() + p, &sample, 2);
        }
        if (i == 7)
            payload.resize(100 + 259 * 259, 156);
        if (i == 8)
        {
            const std::uint16_t sample = 60000;
            payload.resize(100 + 2 * 259 * 259);
            for (std::size_t p = 100; p < payload.size(); p += 2)
                std::memcpy(payload.data() + p, &sample, 2);
        }
        nodes[i].pos = static_cast<std::int64_t>(compressed.size());
        nodes[i].size = static_cast<std::uint32_t>(payload.size());
        uLongf length = compressBound(static_cast<uLong>(payload.size()));
        std::vector<std::uint8_t> packed(length);
        assert(compress2(packed.data(), &length, payload.data(), static_cast<uLong>(payload.size()), Z_DEFAULT_COMPRESSION) == Z_OK);
        compressed.insert(compressed.end(), packed.begin(), packed.begin() + static_cast<std::ptrdiff_t>(length));
    }
    const std::uint64_t data_length = compressed.size();
    std::memcpy(header + 16, &data_length, 8);
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char *>(header), sizeof header);
        out.write(reinterpret_cast<const char *>(nodes), sizeof nodes);
        out.write(reinterpret_cast<const char *>(compressed.data()), static_cast<std::streamsize>(compressed.size()));
        assert(out.good());
    }
    kosmos::OrbiterElevation elevation(path.string(), {}, 6371000.0, -10000.0, 10000.0,
                                      std::numeric_limits<double>::quiet_NaN());
    assert(elevation.ok());
    const auto sample = [&](double latitude, double longitude, double cell_size = 0.0)
    {
        constexpr double pi = 3.14159265358979323846;
        const double lat = latitude * pi / 180.0, lon = longitude * pi / 180.0;
        return elevation.height_on_sphere_lod({std::cos(lat) * std::cos(lon), std::sin(lat),
                                               -std::cos(lat) * std::sin(lon)}, 6371000.0, cell_size);
    };
    assert(sample(45.0, -135.0) == 123.0);
    assert(sample(45.0, -45.0) == 123.0);
    assert(sample(-45.0, -135.0) == 123.0);
    assert(sample(-22.5, -67.5) == 14.0);
    assert(sample(-22.5, -22.5) == -8.0);
    assert(sample(-67.5, -67.5) == -200.0);
    assert(sample(-67.5, -22.5) == 6000.0);
    assert(sample(-56.25, -78.75) == -200.0);
    assert(sample(-56.25, -33.75) == 6000.0);
    assert(sample(-67.5, -22.5, 40000.0) == 0.0);

    std::vector<std::uint8_t> mods;
    for (int i = 0; i < 11; ++i)
    {
        nodes[i].pos = static_cast<std::int64_t>(mods.size());
        nodes[i].size = 0;
        if (i != 0 && i != 4 && i != 5 && i != 6 && i != 7)
            continue;
        const std::int32_t header_bytes = 100, type = i == 5 ? 8 : i == 6 ? -16 : 0;
        const std::int32_t dimension = i == 7 ? 1 : 259;
        const double scale = 2.0, offset = i == 0 ? 200.0 : i == 4 ? 400.0 : 3.0;
        std::vector<std::uint8_t> payload(100, 0);
        std::memcpy(payload.data(), "ELE\x01", 4);
        std::memcpy(payload.data() + 4, &header_bytes, 4);
        std::memcpy(payload.data() + 8, &type, 4);
        std::memcpy(payload.data() + 12, &dimension, 4);
        std::memcpy(payload.data() + 16, &dimension, 4);
        std::memcpy(payload.data() + 28, &scale, 8);
        std::memcpy(payload.data() + 36, &offset, 8);
        if (type == 8)
        {
            payload.resize(100 + 259 * 259);
            for (int y = 0; y < 259; ++y)
                for (int x = 0; x < 259; ++x)
                    payload[100 + y * 259 + x] = x % 2 ? 255 : 14;
        }
        if (type == -16)
        {
            payload.resize(100 + 2 * 259 * 259);
            for (int y = 0; y < 259; ++y)
                for (int x = 0; x < 259; ++x)
                {
                    const std::int16_t value = x % 2 ? 32767 : -20;
                    std::memcpy(payload.data() + 100 + 2 * (y * 259 + x), &value, 2);
                }
        }
        nodes[i].size = static_cast<std::uint32_t>(payload.size());
        uLongf length = compressBound(static_cast<uLong>(payload.size()));
        std::vector<std::uint8_t> packed(length);
        assert(compress2(packed.data(), &length, payload.data(), static_cast<uLong>(payload.size()), Z_DEFAULT_COMPRESSION) == Z_OK);
        mods.insert(mods.end(), packed.begin(), packed.begin() + static_cast<std::ptrdiff_t>(length));
    }
    const std::uint64_t mod_length = mods.size();
    std::memcpy(header + 16, &mod_length, 8);
    {
        std::ofstream out(path.parent_path() / "Elev_mod.tree", std::ios::binary);
        out.write(reinterpret_cast<const char *>(header), sizeof header);
        out.write(reinterpret_cast<const char *>(nodes), sizeof nodes);
        out.write(reinterpret_cast<const char *>(mods.data()), static_cast<std::streamsize>(mods.size()));
        assert(out.good());
    }
    kosmos::OrbiterElevation modified(path.string(), {}, 6371000.0, -10000.0, 10000.0,
                                     std::numeric_limits<double>::quiet_NaN());
    const auto sample_modified = [&](double latitude, double longitude, double cell_size = 0.0)
    {
        constexpr double pi = 3.14159265358979323846;
        const double lat = latitude * pi / 180.0, lon = longitude * pi / 180.0;
        return modified.height_on_sphere_lod({std::cos(lat) * std::cos(lon), std::sin(lat),
                                              -std::cos(lat) * std::sin(lon)}, 6371000.0, cell_size);
    };
    assert(sample_modified(45.0, -135.0) == 200.0);
    assert(sample_modified(-22.5, -67.5) == 14.0);
    assert(std::fabs(sample_modified(-22.5, -67.5 + 45.0 / 512.0) - 22.5) < 1e-8);
    assert(sample_modified(-22.5, -22.5) == -8.0);
    assert(std::fabs(sample_modified(-22.5, -22.5 + 45.0 / 512.0) + 22.5) < 1e-8);
    assert(sample_modified(-67.5, -67.5) == -200.0);
    assert(sample_modified(-67.5, -22.5) == 6000.0);
    assert(sample_modified(-67.5, -22.5, 40000.0) == 400.0);

    const auto body = path.parent_path() / "individual-elevation";
    const auto archive = body / "Archive" / "Elev.tree";
    std::filesystem::create_directories(archive.parent_path());
    std::filesystem::copy_file(path, archive);
    std::filesystem::copy_file(path.parent_path() / "Elev_mod.tree", archive.parent_path() / "Elev_mod.tree");
    const auto write_flat = [&](const char *layer, int level, int row, int column, double value)
    {
        char relative[64];
        std::snprintf(relative, sizeof relative, "%s/%02d/%06d/%06d.elv", layer, level, row, column);
        const auto output = body / relative;
        std::filesystem::create_directories(output.parent_path());
        std::vector<std::uint8_t> payload(100, 0);
        const std::int32_t header_bytes = 100, dimension = 259;
        const double scale = 1.0;
        std::memcpy(payload.data(), "ELE\x01", 4);
        std::memcpy(payload.data() + 4, &header_bytes, 4);
        std::memcpy(payload.data() + 12, &dimension, 4);
        std::memcpy(payload.data() + 16, &dimension, 4);
        std::memcpy(payload.data() + 28, &scale, 8);
        std::memcpy(payload.data() + 36, &value, 8);
        std::ofstream out(output, std::ios::binary);
        out.write(reinterpret_cast<const char *>(payload.data()), static_cast<std::streamsize>(payload.size()));
        assert(out.good());
    };
    write_flat("Elev", 6, 2, 2, 250.0);
    write_flat("Elev_mod", 6, 2, 2, 275.0);
    write_flat("Elev", 8, 8, 8, 350.0);
    write_flat("Elev_mod", 6, 2, 3, 500.0);
    const auto corrupt = body / "Elev" / "06" / "000002" / "000003.elv";
    std::filesystem::create_directories(corrupt.parent_path());
    std::ofstream(corrupt, std::ios::binary) << "bad";
    const auto corrupt_mod = body / "Elev_mod" / "05" / "000001" / "000001.elv";
    std::filesystem::create_directories(corrupt_mod.parent_path());
    std::ofstream(corrupt_mod, std::ios::binary) << "bad";
    kosmos::OrbiterElevation individual(archive.string(), {}, 6371000.0, -10000.0, 10000.0,
                                       std::numeric_limits<double>::quiet_NaN());
    const auto sample_individual = [&](double latitude, double longitude, double cell_size = 0.0)
    {
        constexpr double pi = 3.14159265358979323846;
        const double lat = latitude * pi / 180.0, lon = longitude * pi / 180.0;
        return individual.height_on_sphere_lod({std::cos(lat) * std::cos(lon), std::sin(lat),
                                                -std::cos(lat) * std::sin(lon)}, 6371000.0, cell_size);
    };
    assert(sample_individual(-22.5, -67.5) == 275.0);
    assert(sample_individual(-5.625, -84.375) == 350.0);
    assert(sample_individual(-22.5, -22.5) == 500.0);
    assert(sample_individual(45.0, -135.0) == 200.0);
    assert(sample_individual(-22.5, -67.5, 40000.0) == 400.0);
    std::filesystem::remove(archive.parent_path() / "Elev_mod.tree");
    kosmos::OrbiterElevation individual_mod_only(archive.string(), {}, 6371000.0, -10000.0, 10000.0,
                                                std::numeric_limits<double>::quiet_NaN());
    const double lat = -22.5 * 3.14159265358979323846 / 180.0;
    const double lon = -67.5 * 3.14159265358979323846 / 180.0;
    assert(individual_mod_only.height_on_sphere({std::cos(lat) * std::cos(lon), std::sin(lat),
                                                -std::cos(lat) * std::sin(lon)}, 6371000.0) == 275.0);
    std::filesystem::remove(archive);
    kosmos::OrbiterElevation individual_only(archive.string(), {}, 6371000.0, -10000.0, 10000.0,
                                             std::numeric_limits<double>::quiet_NaN());
    assert(individual_only.ok());
    assert(individual_only.height_on_sphere({std::cos(lat) * std::cos(lon), std::sin(lat),
                                             -std::cos(lat) * std::sin(lon)}, 6371000.0) == 275.0);
    std::ofstream(archive, std::ios::binary) << "bad archive";
    kosmos::OrbiterElevation corrupt_archive(archive.string(), {}, 6371000.0, -10000.0, 10000.0,
                                            std::numeric_limits<double>::quiet_NaN());
    assert(corrupt_archive.ok());
    assert(corrupt_archive.height_on_sphere({std::cos(lat) * std::cos(lon), std::sin(lat),
                                             -std::cos(lat) * std::sin(lon)}, 6371000.0) == 275.0);
    {
        std::vector<std::uint8_t> payload(100 + 259 * 259, 0);
        const std::int32_t header_bytes = 100, type = 8, dimension = 259;
        const double scale = 2.0, offset = 3.0;
        std::memcpy(payload.data(), "ELE\x01", 4);
        std::memcpy(payload.data() + 4, &header_bytes, 4);
        std::memcpy(payload.data() + 8, &type, 4);
        std::memcpy(payload.data() + 12, &dimension, 4);
        std::memcpy(payload.data() + 16, &dimension, 4);
        std::memcpy(payload.data() + 28, &scale, 8);
        std::memcpy(payload.data() + 36, &offset, 8);
        for (int y = 0; y < 259; ++y)
            for (int x = 0; x < 259; ++x)
                payload[100 + y * 259 + x] = x % 2 ? 255 : 14;
        std::ofstream out(body / "Elev_mod" / "06" / "000002" / "000002.elv", std::ios::binary);
        out.write(reinterpret_cast<const char *>(payload.data()), static_cast<std::streamsize>(payload.size()));
        assert(out.good());
    }
    kosmos::OrbiterElevation individual_mask(archive.string(), {}, 6371000.0, -10000.0, 10000.0,
                                            std::numeric_limits<double>::quiet_NaN());
    const double shifted_lon = (-67.5 + 45.0 / 512.0) * 3.14159265358979323846 / 180.0;
    assert(std::fabs(individual_mask.height_on_sphere({std::cos(lat) * std::cos(shifted_lon), std::sin(lat),
                                                       -std::cos(lat) * std::sin(shifted_lon)}, 6371000.0) - 140.5) < 1e-8);
}

int main(int argc, char **argv)
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() /
                          ("kosmos-tree-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path legacy = root / "import/orbiter/Orbiter 2016 High Res: Earth/Surf.tree";
    const fs::path legacy_minor = root / "import/orbiter/Orbiter High Res Minor Bodies/Io/Archive/Surf.tree";
    const fs::path canonical = root / "import/orbiter/Earth/Archive/Surf.tree";
    const fs::path minor = root / "import/orbiter/MinorBodies/Io/Archive/Surf.tree";
    const fs::path standard = root / "data/orbiter-defaults/earth/Earth/Archive/Surf.tree";
    const fs::path planet = root / "data/orbiter-defaults/planets/Moon/Archive/Elev.tree";
    const fs::path stray = root / "import/orbiter/unrelated/Moon/Archive/Surf.tree";
    for (const fs::path &file : {legacy, legacy_minor, canonical, minor, standard, planet, stray})
    {
        fs::create_directories(file.parent_path());
        std::ofstream(file) << "fixture";
    }
    test_index_limits(root / "invalid-index.tree");
    test_ancestor(root / "ancestor.tree");
    test_elevation_fallback(root / "elevation.tree");
    test_shared_terrain_frame();
    test_collision_frame();
    test_tile_cache_budget();
    const auto lookup = [&](const char *body, const char *layer)
    { return fs::path(kosmos::OrbiterTree::find_layer(root.string(), body, layer)).lexically_normal(); };
    assert(lookup("Earth", "Surf") == canonical.lexically_normal());
    assert(lookup("Moon", "Elev") == planet.lexically_normal());
    assert(lookup("Io", "Surf") == minor.lexically_normal());
    assert(kosmos::OrbiterTree::find_layer(root.string(), "Earth", "Elev").empty());
    assert(kosmos::OrbiterTree::find_layer(root.string(), "Moon", "Surf").empty());
    fs::remove(canonical);
    assert(lookup("Earth", "Surf") == standard.lexically_normal());
    fs::remove(standard);
    fs::remove(minor);
    assert(kosmos::OrbiterTree::find_layer(root.string(), "Earth", "Surf").empty());
    assert(kosmos::OrbiterTree::find_layer(root.string(), "Io", "Surf").empty());
    assert(kosmos::OrbiterTree::find_layer(root.string(), "../Earth", "Surf").empty());
    assert(kosmos::OrbiterTree::find_layer(root.string(), "Earth", "Mask").empty());
    const fs::path individual = root / "import/orbiter/Earth/Surf/04/000000/000000.dds";
    fs::create_directories(individual.parent_path());
    std::ofstream(individual) << "fixture";
    assert(lookup("Earth", "Surf") == canonical.lexically_normal());
    fs::remove_all(root);

    constexpr double mars_radius = 3389500.0;
    assert(!kosmos::app::terrain_visible(200000.0, mars_radius, true, false));
    assert(kosmos::app::terrain_visible(200000.0, mars_radius, true, true));
    assert(!kosmos::app::terrain_visible(2000000.0, mars_radius, true, true));
    assert(kosmos::app::terrain_visible(200000.0, mars_radius, false, false));
    assert(!kosmos::app::earth_sphere_visible(119000.0, 6371000.0, false, false, true, 120000.0));
    assert(kosmos::app::earth_sphere_visible(121000.0, 6371000.0, false, false, true, 120000.0));
    assert(kosmos::app::earth_sphere_visible(121000.0, 6371000.0, true, true, false, 120000.0));
    assert(!kosmos::app::earth_sphere_visible(121000.0, 6371000.0, true, false, false, 120000.0));
    assert(!kosmos::app::earth_sphere_visible(400000.0, 6371000.0, false, true, false, 120000.0));
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
        const glm::dquat terrain_frame = glm::angleAxis(0.7, glm::normalize(glm::dvec3(1.0, 2.0, 3.0)));
        kosmos::OrbiterElevation rotated(argv[1], {terrain_frame}, mars_radius, -12000.0,
                                        kosmos::app::orbiter_max_height(false, true),
                                        std::numeric_limits<double>::quiet_NaN());
        assert(rotated.ok());
        const glm::dvec3 tile_direction(-olympus.x, olympus.y, -olympus.z);
        for (double cell : {1.0, 1000.0, 10000.0})
        {
            const double expected = elevation.height_on_sphere_lod(olympus, mars_radius, cell);
            const double tile_height = kosmos::orbiter_tile_height(rotated, terrain_frame, tile_direction, mars_radius, cell) * mars_radius;
            assert(std::fabs(tile_height - expected) < 1.0e-5);
        }
    }
    if (argc > 2)
    {
        kosmos::OrbiterImagery imagery(argv[2], {}, 6371000.0);
        assert(imagery.ok());
        kosmos::OrbiterTree::Tile tile;
        std::vector<std::uint8_t> dds;
        assert(imagery.read_tile(4, 0, 0, tile, dds, 2u << 20));
        assert(tile.level == 4 && tile.u0 == 0.0 && tile.v0 == 0.0 && tile.u1 == 1.0 && tile.v1 == 1.0);
        std::uint32_t height = 0, width = 0;
        std::memcpy(&height, dds.data() + 12, 4);
        std::memcpy(&width, dds.data() + 16, 4);
        assert(width <= 512 && height <= 512);
        std::printf("Orbiter root imagery: %zu DDS bytes\n", dds.size());
    }
    if (argc > 3)
    {
        kosmos::OrbiterElevation elevation(argv[3], {}, 6371000.0, -12000.0, 9000.0, 0.0);
        assert(elevation.ok());
        const double height = elevation.height_on_sphere(glm::dvec3(1.0, 0.0, 0.0), 6371000.0);
        assert(std::isfinite(height) && height >= 0.0 && height <= 9000.0);
        std::printf("Earth elevation archive opened: %.1f m at 0,0\n", height);
        const auto mod_path = std::filesystem::path(argv[3]).parent_path() / "Elev_mod.tree";
        if (std::filesystem::is_regular_file(mod_path))
        {
            kosmos::OrbiterTree mods, base;
            std::string error;
            assert(mods.open(mod_path.string(), error));
            assert(base.open(argv[3], error));
            bool checked = false;
            for (int column = 0; column < 2 && !checked; ++column)
            {
                const auto mod_node = mods.find(4, 0, column);
                if (!mods.has_data(mod_node) || !base.has_data(base.find(4, 0, column)))
                    continue;
                std::vector<std::uint8_t> bytes;
                assert(mods.read(mod_node, bytes, 2u << 20) && bytes.size() >= 100);
                std::int32_t header, type;
                double scale, offset;
                std::memcpy(&header, bytes.data() + 4, 4);
                std::memcpy(&type, bytes.data() + 8, 4);
                std::memcpy(&scale, bytes.data() + 28, 8);
                std::memcpy(&offset, bytes.data() + 36, 8);
                if ((type != 8 && type != -16) || header < 100 ||
                    static_cast<std::size_t>(header) + 259u * 259u * (type == 8 ? 1u : 2u) > bytes.size())
                    continue;
                for (int j = 2; j < 257 && !checked; ++j)
                    for (int i = 2; i < 257 && !checked; ++i)
                    {
                        const std::size_t index = static_cast<std::size_t>(j * 259 + i);
                        std::int16_t signed_value = 0;
                        if (type == -16)
                            std::memcpy(&signed_value, bytes.data() + header + 2 * index, 2);
                        const int value = type == 8 ? bytes[static_cast<std::size_t>(header) + index] : signed_value;
                        if (value == (type == 8 ? 255 : 32767))
                            continue;
                        constexpr double pi = 3.14159265358979323846;
                        const double lat = (-90.0 + (j - 1) * 180.0 / 256.0) * pi / 180.0;
                        const double lon = (-180.0 + (column + (i - 1) / 256.0) * 180.0) * pi / 180.0;
                        const glm::dvec3 direction(std::cos(lat) * std::cos(lon), std::sin(lat),
                                                   -std::cos(lat) * std::sin(lon));
                        const double expected = std::clamp(value * scale + offset, 0.0, 9000.0);
                        const double actual = elevation.height_on_sphere_lod(direction, 6371000.0, 1000000.0);
                        assert(std::fabs(actual - expected) < 0.001);
                        std::printf("Earth elevation modification: %.1f m at %.5f,%.5f degrees\n",
                                    actual, lat * 180.0 / pi, lon * 180.0 / pi);
                        checked = true;
                    }
            }
        }
    }
}
