#pragma once

#include <array>
#include <filesystem>
#include <istream>
#include <string>
#include <unordered_map>

namespace kosmos
{
    struct OrbiterMicrotextureLevel
    {
        std::string file;
        double pixels_per_metre = 0.0;
    };

    struct OrbiterMicrotextures
    {
        bool normals = false;
        std::array<OrbiterMicrotextureLevel, 3> levels;
    };

    // On failure the output is left unchanged and error identifies the invalid line or block.
    bool parse_orbiter_microtextures(std::istream &input,
                                    std::unordered_map<std::string, OrbiterMicrotextures> &bodies,
                                    std::string &error);

    struct OrbiterMicrotextureAssets
    {
        OrbiterMicrotextures config;
        std::array<std::filesystem::path, 3> paths;
    };

    enum class OrbiterMicrotextureDiscovery { available, not_configured, error };

    // root has Orbiter's Config/MicroTex.cfg and Textures/ layout. On failure, assets is unchanged.
    OrbiterMicrotextureDiscovery discover_orbiter_microtextures(const std::filesystem::path &root,
                                                                const std::string &body,
                                                                OrbiterMicrotextureAssets &assets,
                                                                std::string &error);

    // Conservative distance to the terrain's bounding sphere; unknown bounds keep detail enabled.
    bool orbiter_microtexture_near_surface(double eye_distance, double radius, double max_height);

    struct OrbiterMicrotextureRange
    {
        double u_offset = 0.0, v_offset = 0.0;
        double u_scale = 0.0, v_scale = 0.0;
    };

    // Orbiter's repeat counts belong to the colour tile supplying the imagery, not the geometry tile.
    bool orbiter_microtexture_range(int level, int column, int row, int levels_up, int micro_level,
                                    double radius, int texture_width, double pixels_per_metre,
                                    OrbiterMicrotextureRange &range, std::string &error);
}
