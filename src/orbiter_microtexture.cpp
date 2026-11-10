#include "orbiter_microtexture.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <utility>

namespace kosmos
{
    bool orbiter_microtexture_near_surface(double eye_distance, double radius, double max_height)
    {
        return !std::isfinite(eye_distance) || !std::isfinite(radius) || !std::isfinite(max_height) ||
               eye_distance - radius - std::max(max_height, 0.0) < 15000.0;
    }

    bool parse_orbiter_microtextures(std::istream &input,
                                    std::unordered_map<std::string, OrbiterMicrotextures> &bodies,
                                    std::string &error)
    {
        std::unordered_map<std::string, OrbiterMicrotextures> parsed;
        OrbiterMicrotextures current;
        std::string body;
        unsigned fields = 0;
        std::size_t line_number = 0;
        const auto fail = [&](const std::string &message)
        {
            error = "MicroTex.cfg line " + std::to_string(line_number) + ": " + message;
            return false;
        };
        const auto finish = [&]()
        {
            if (body.empty())
                return true;
            if (fields != 15)
                return fail("incomplete body " + body);
            if (!parsed.emplace(body, current).second)
                return fail("duplicate body " + body);
            return true;
        };
        std::string line;
        while (std::getline(input, line))
        {
            ++line_number;
            const auto comment = line.find("//");
            if (comment != std::string::npos)
                line.erase(comment);
            std::istringstream words(line);
            std::string keyword;
            if (!(words >> keyword))
                continue;
            if (keyword == "BODY")
            {
                if (!finish())
                    return false;
                current = {};
                fields = 0;
                std::string extra;
                if (!(words >> body) || words >> extra)
                    return fail("expected BODY followed by a body name");
            }
            else if (keyword == "NORMALS")
            {
                int normal = -1;
                std::string extra;
                if (body.empty() || (fields & 1) || !(words >> normal) || (normal != 0 && normal != 1) || words >> extra)
                    return fail("expected one NORMALS 0 or 1 in a body");
                current.normals = normal == 1;
                fields |= 1;
            }
            else if (keyword == "LEVEL")
            {
                int index = -1;
                std::string file, extra;
                double resolution = 0.0;
                if (body.empty() || !(words >> index >> file >> resolution) || index < 0 || index >= 3 ||
                    !std::isfinite(resolution) || resolution <= 0.0 || words >> extra)
                    return fail("expected LEVEL 0-2, a texture name and a positive resolution");
                const unsigned bit = 2u << index;
                if (fields & bit)
                    return fail("duplicate LEVEL " + std::to_string(index));
                current.levels[static_cast<std::size_t>(index)] = {std::move(file), resolution};
                fields |= bit;
            }
            else
                return fail("unknown keyword " + keyword);
        }
        if (input.bad())
            return fail("could not read configuration");
        if (!finish())
            return false;
        bodies = std::move(parsed);
        error.clear();
        return true;
    }

    OrbiterMicrotextureDiscovery discover_orbiter_microtextures(const std::filesystem::path &root,
                                                                const std::string &body,
                                                                OrbiterMicrotextureAssets &assets,
                                                                std::string &error)
    {
        namespace fs = std::filesystem;
        const fs::path config_path = root / "Config" / "MicroTex.cfg";
        std::error_code ec;
        if (!fs::exists(config_path, ec))
        {
            if (ec)
            {
                error = "Cannot inspect " + config_path.string() + ": " + ec.message();
                return OrbiterMicrotextureDiscovery::error;
            }
            error.clear();
            return OrbiterMicrotextureDiscovery::not_configured;
        }
        std::ifstream input(config_path);
        if (!input)
        {
            error = "Cannot open " + config_path.string();
            return OrbiterMicrotextureDiscovery::error;
        }
        std::unordered_map<std::string, OrbiterMicrotextures> bodies;
        if (!parse_orbiter_microtextures(input, bodies, error))
        {
            error = config_path.string() + ": " + error;
            return OrbiterMicrotextureDiscovery::error;
        }
        const auto found = bodies.find(body);
        if (found == bodies.end())
        {
            error.clear();
            return OrbiterMicrotextureDiscovery::not_configured;
        }
        OrbiterMicrotextureAssets resolved;
        resolved.config = found->second;
        for (std::size_t i = 0; i < resolved.paths.size(); ++i)
        {
            const std::string &name = resolved.config.levels[i].file;
            const fs::path file(name);
            if (name.empty() || name.find('\\') != std::string::npos || file.has_parent_path() || file == "." || file == "..")
            {
                error = config_path.string() + ": invalid texture filename " + name;
                return OrbiterMicrotextureDiscovery::error;
            }
            resolved.paths[i] = root / "Textures" / file;
            if (!fs::is_regular_file(resolved.paths[i], ec))
            {
                error = "Cannot access microtexture " + resolved.paths[i].string();
                if (ec)
                    error += ": " + ec.message();
                return OrbiterMicrotextureDiscovery::error;
            }
        }
        assets = std::move(resolved);
        error.clear();
        return OrbiterMicrotextureDiscovery::available;
    }

    bool orbiter_microtexture_range(int level, int column, int row, int levels_up, int micro_level,
                                    double radius, int texture_width, double pixels_per_metre,
                                    OrbiterMicrotextureRange &range, std::string &error)
    {
        if (level < 0 || level > 24 || levels_up < 0 || levels_up > level || micro_level < 0 || micro_level > 2 ||
            column < 0 || row < 0 || column >= (std::int64_t(2) << level) || row >= (std::int64_t(1) << level) ||
            !std::isfinite(radius) || radius <= 0.0 || texture_width <= 0 ||
            !std::isfinite(pixels_per_metre) || pixels_per_metre <= 0.0)
        {
            error = "invalid Orbiter microtexture tile or resolution";
            return false;
        }
        constexpr double pi = 3.14159265358979323846;
        const int owner_level = level - levels_up;
        const int owner_row = row >> levels_up;
        const double count = static_cast<double>(std::int64_t(1) << owner_level);
        const double south_latitude = pi * (0.5 - (owner_row + 1.0) / count);
        const double width = pi * std::cos(south_latitude) / count;
        const double height = pi / count;
        const double repeat_factor = radius * pixels_per_metre / texture_width;
        const int multiple = micro_level == 0 ? 16 : micro_level == 1 ? 8 : 2;
        const auto repeats = [&](double extent)
        {
            return (1.0 + std::floor(extent * repeat_factor / multiple)) * multiple;
        };
        const double u_repeat = repeats(width), v_repeat = repeats(height);
        if (!std::isfinite(u_repeat) || !std::isfinite(v_repeat))
        {
            error = "Orbiter microtexture repeat count is out of range";
            return false;
        }
        const std::int64_t span = std::int64_t(1) << levels_up;
        const double u = static_cast<double>(column % span) / span;
        const double v = static_cast<double>(row % span) / span;
        const double u_start = u_repeat * u, v_start = v_repeat * v;
        range = {u_start - std::floor(u_start), v_start - std::floor(v_start),
                 u_repeat / span, v_repeat / span};
        error.clear();
        return true;
    }
}
