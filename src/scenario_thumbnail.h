#pragma once

#include <functional>
#include <string>

namespace kosmos
{
    bool save_scenario_thumbnail(const std::string &scenario_path,
                                 const std::function<bool(const std::string &)> &capture,
                                 std::string &image_path, std::string &error);
}
