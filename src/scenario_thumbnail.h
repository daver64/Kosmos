#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace kosmos
{
    bool write_thumbnail_png(const std::string &path, const std::vector<std::uint8_t> &pixels,
                             int width, int height, std::string &error);
    bool save_scenario_thumbnail(const std::string &scenario_path,
                                 const std::function<bool(const std::string &)> &capture,
                                 std::string &image_path, std::string &error);
}
