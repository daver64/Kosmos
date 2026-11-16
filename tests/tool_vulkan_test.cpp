#include "../sdk/tool_vulkan.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    bool pixel_is(const std::vector<unsigned char> &pixels, int width, int x, int y, int r, int g, int b, int tolerance = 2)
    {
        uint32_t pixel = 0;
        std::memcpy(&pixel, pixels.data() + (static_cast<std::size_t>(y) * width + x) * 4, sizeof pixel);
        return (pixel >> 24) == 255 &&
               std::abs(static_cast<int>((pixel >> 16) & 255) - r) <= tolerance &&
               std::abs(static_cast<int>((pixel >> 8) & 255) - g) <= tolerance &&
               std::abs(static_cast<int>(pixel & 255) - b) <= tolerance;
    }

    void quad(std::vector<toolvk::Vertex> &vertices, float extent, float depth, float r, float g, float b)
    {
        vertices.insert(vertices.end(), {
            {-extent, -extent, depth, r, g, b}, {extent, -extent, depth, r, g, b}, {extent, extent, depth, r, g, b},
            {-extent, -extent, depth, r, g, b}, {extent, extent, depth, r, g, b}, {-extent, extent, depth, r, g, b}});
    }
}

int main(int argc, char **argv)
{
    toolvk::Renderer renderer;
    std::string error;
    const bool ready = renderer.init(error);
    if (argc == 2 && std::strcmp(argv[1], "--expect-unavailable") == 0)
    {
        if (ready || error.empty())
        {
            std::fprintf(stderr, "Expected an explicit Vulkan initialization failure\n");
            return 1;
        }
        std::printf("Vulkan unavailable: %s\n", error.c_str());
        return 0;
    }
    if (argc != 1)
    {
        std::fprintf(stderr, "Usage: tool_vulkan_test [--expect-unavailable]\n");
        return 1;
    }
    if (!ready)
    {
        std::fprintf(stderr, "Vulkan initialization failed: %s\n", error.c_str());
        return 1;
    }

    std::vector<toolvk::Vertex> vertices;
    quad(vertices, 1.0f, 0.8f, 1, 0, 0);
    quad(vertices, 0.5f, 0.2f, 0, 1, 0);
    quad(vertices, 0.5f, 0.9f, 0, 0, 1);
    vertices.push_back({-0.8f, 0, 0.5f, 1, 1, 1});
    vertices.push_back({0.8f, 0, 0.5f, 1, 1, 1});
    const std::vector<toolvk::Draw> draws = {{0, 18, false, true}, {18, 2, true, false}};
    const float clear[] = {0.2f, 0.3f, 0.4f, 1.0f};
    std::vector<unsigned char> pixels;
    if (!renderer.draw(vertices, draws, 64, 64, clear, pixels, error))
    {
        std::fprintf(stderr, "Vulkan draw failed: %s\n", error.c_str());
        return 1;
    }
    if (pixels.size() != 64u * 64u * 4u ||
        !pixel_is(pixels, 64, 8, 8, 255, 0, 0) ||
        !pixel_is(pixels, 64, 32, 24, 0, 255, 0))
    {
        std::fprintf(stderr, "Vulkan triangle colors, depth ordering or output size are wrong\n");
        return 1;
    }
    bool line_visible = false;
    for (int y = 30; y <= 33; ++y)
        line_visible = line_visible || pixel_is(pixels, 64, 32, y, 255, 255, 255, 140);
    if (!line_visible)
    {
        std::fprintf(stderr, "Vulkan line is missing\n");
        return 1;
    }
    if (!renderer.draw({}, {}, 31, 19, clear, pixels, error) ||
        pixels.size() != 31u * 19u * 4u ||
        !pixel_is(pixels, 31, 15, 9, 51, 77, 102))
    {
        std::fprintf(stderr, "Vulkan resized empty scene failed: %s\n", error.c_str());
        return 1;
    }
    if (renderer.draw(vertices, {{18, 3, true, false}}, 31, 19, clear, pixels, error) || error.empty())
    {
        std::fprintf(stderr, "Vulkan renderer accepted an out-of-range draw\n");
        return 1;
    }
    std::puts("Vulkan renderer checks passed");
    return 0;
}
