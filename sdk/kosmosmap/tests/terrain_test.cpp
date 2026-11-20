#include "terrain.h"
#include "render.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <zlib.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    int failures = 0;
    void check(bool result, const char *description)
    {
        std::printf("%s: %s\n", result ? "ok" : "FAIL", description);
        failures += !result;
    }

    template <class T>
    void put(std::vector<std::uint8_t> &data, std::size_t offset, T value)
    {
        std::memcpy(data.data() + offset, &value, sizeof(value));
    }

    std::vector<std::uint8_t> dds(std::uint16_t colour)
    {
        std::vector<std::uint8_t> bytes(136, 0);
        std::memcpy(bytes.data(), "DDS ", 4);
        put<std::uint32_t>(bytes, 12, 4);
        put<std::uint32_t>(bytes, 16, 4);
        put<std::uint32_t>(bytes, 80, 4);
        std::memcpy(bytes.data() + 84, "DXT1", 4);
        put<std::uint16_t>(bytes, 128, colour);
        return bytes;
    }

    void archive(const fs::path &path, const std::vector<std::vector<std::uint8_t>> &tiles, bool children)
    {
        struct Node { std::int64_t pos; std::uint32_t size, child[4], pad; };
        std::vector<Node> nodes(tiles.size());
        std::vector<std::uint8_t> compressed;
        for (std::size_t i = 0; i < tiles.size(); ++i)
        {
            nodes[i].pos = static_cast<std::int64_t>(compressed.size());
            nodes[i].size = static_cast<std::uint32_t>(tiles[i].size());
            for (auto &child : nodes[i].child)
                child = kmap::Tree::none;
            uLongf length = compressBound(tiles[i].size());
            const std::size_t start = compressed.size();
            compressed.resize(start + length);
            if (compress(compressed.data() + start, &length, tiles[i].data(), tiles[i].size()) != Z_OK)
                throw std::runtime_error("compress failed");
            compressed.resize(start + length);
        }
        if (children)
        {
            nodes[0].child[0] = 1;
            nodes[0].child[3] = 2;
        }
        std::vector<std::uint8_t> head(48, 0xff);
        std::memcpy(head.data(), "TX\x01\x00", 4);
        put<std::uint32_t>(head, 4, 48);
        put<std::uint32_t>(head, 12, 48 + static_cast<std::uint32_t>(nodes.size() * sizeof(Node)));
        put<std::int64_t>(head, 16, static_cast<std::int64_t>(compressed.size()));
        put<std::uint32_t>(head, 24, static_cast<std::uint32_t>(nodes.size()));
        put<std::uint32_t>(head, 40, 0);
        fs::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char *>(head.data()), head.size());
        out.write(reinterpret_cast<const char *>(nodes.data()), nodes.size() * sizeof(Node));
        out.write(reinterpret_cast<const char *>(compressed.data()), compressed.size());
        if (!out)
            throw std::runtime_error("cannot write fixture archive");
    }
}

int main()
{
    GError *error = nullptr;
    gchar *tmp = g_dir_make_tmp("kosmosmap-test-XXXXXX", &error);
    if (!tmp)
    {
        std::fprintf(stderr, "Cannot create test directory: %s\n", error->message);
        g_error_free(error);
        return 1;
    }
    const fs::path root(tmp);
    g_free(tmp);
    try
    {
        const auto installed = root / "import/orbiter/Earth/Archive";
        const auto bundled = root / "data/orbiter-defaults/earth/Earth/Archive";
        archive(bundled / "Surf.tree", {dds(0x07e0)}, false);
        archive(installed / "Surf.tree", {dds(0x001f), dds(0xf800), {1, 2, 3}}, true);
        std::vector<std::uint8_t> elevation(100, 0);
        std::memcpy(elevation.data(), "ELE", 3);
        put<std::int32_t>(elevation, 4, 100);
        put<double>(elevation, 28, 1.0);
        put<double>(elevation, 36, 42.0);
        archive(installed / "Elev.tree", {elevation}, false);

        kmap::Ground ground;
        ground.load(root.string(), "Earth", 6371000.0);
        check(ground.has_surface && ground.has_elevation, "installed surface and elevation discovered");
        kmap::Elevation::Cursor elevation_cursor;
        check(ground.elevation.height(kmap::pi / 4, -3 * kmap::pi / 4, 1.0, ground.radius, elevation_cursor) == 42.0,
              "elevation samples installed archive");
        kmap::Surface::Cursor cursor;
        float rgb[3] = {};
        check(ground.surface.colour(kmap::pi / 4, -3 * kmap::pi / 4, 1000000.0, ground.radius, cursor, rgb) &&
              rgb[2] > 0.99f && rgb[0] < 0.01f, "zoomed out uses installed parent tile");
        check(ground.surface.colour(kmap::pi / 4, -3 * kmap::pi / 4, 1.0, ground.radius, cursor, rgb) &&
              rgb[0] > 0.99f && rgb[2] < 0.01f, "zoomed in uses detailed child tile");
        check(ground.surface.colour(-kmap::pi / 4, -kmap::pi / 4, 1.0, ground.radius, cursor, rgb) &&
              rgb[2] > 0.99f, "corrupt child falls back to parent tile");
        check(!ground.surface.colour(0.0, kmap::pi / 2, 1.0, ground.radius, cursor, rgb), "missing hemisphere reports no tile");

        check(kmap::Tree::find_layer(root.string(), "earth", "Surf") == (installed / "Surf.tree").string(),
              "body names are matched case-insensitively");
        const auto fallback_root = root / "fallback";
        archive(fallback_root / "data/orbiter-defaults/earth/Earth/Archive/Surf.tree", {dds(0x07e0)}, false);
        kmap::Ground fallback;
        fallback.load(fallback_root.string(), "Earth", 6371000.0);
        kmap::Surface::Cursor fallback_cursor;
        check(fallback.surface.colour(kmap::pi / 4, -3 * kmap::pi / 4, 1.0, fallback.radius, fallback_cursor, rgb) &&
              rgb[1] > 0.99f, "bundled archive used when installed archive is absent");

        kmap::View view;
        view.w = view.h = 3;
        view.lat = 45;
        view.lon = -135;
        view.ppd = 100;
        view.imagery = true;
        std::atomic<int> generation{1};
        const auto surface_pixels = kmap::render(fallback, view, generation, 1);
        check(fallback.has_imagery() && surface_pixels.size() == 9 && (surface_pixels[4] & 0x00ffffff) == 0x0000ff00,
              "surface-only imagery renders without a global picture");

        const auto image_path = fallback_root / "data/textures/planets/earth.png";
        fs::create_directories(image_path.parent_path());
        GdkPixbuf *image = gdk_pixbuf_new(GDK_COLORSPACE_RGB, false, 8, 2, 1);
        gdk_pixbuf_fill(image, 0x00ff00ff);
        if (!gdk_pixbuf_save(image, image_path.string().c_str(), "png", &error, nullptr))
            throw std::runtime_error(error ? error->message : "cannot write image");
        g_object_unref(image);
        kmap::Ground picture_ground;
        picture_ground.load(fallback_root.string(), "Earth", 6371000.0);
        view.lat = 0;
        view.lon = 90;
        const auto pixels = kmap::render(picture_ground, view, generation, 1);
        check(pixels.size() == 9 && (pixels[4] & 0x00ffffff) == 0x0000ff00, "missing tile falls back to global picture");
    }
    catch (const std::exception &e)
    {
        std::fprintf(stderr, "Test fixture error: %s\n", e.what());
        ++failures;
    }
    std::error_code ec;
    fs::remove_all(root, ec);
    if (ec)
    {
        std::fprintf(stderr, "Cannot remove test directory: %s\n", ec.message().c_str());
        ++failures;
    }
    return failures ? 1 : 0;
}
