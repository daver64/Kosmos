#pragma once

#include "sl.h"

#include <map>
#include <string>
#include <vector>

namespace kosmos
{
    /** Where the saved settings live (empty when no config directory can be found). */
    std::string settings_path();
    /** --set path=value: numbers and true/false get their own types, anything else is a string. */
    void apply_sets(sl::PropertyTree &props, const std::vector<std::string> &sets);
    /** Settings live in the property tree under /sim/settings so scripts and the F4 browser see them too. */
    void settings_defaults(sl::PropertyTree &props);
    void settings_load(sl::PropertyTree &props);
    void settings_save(sl::PropertyTree &props);

    struct PropertyBrowser
    {
        char filter[96] = "";
        char path[160] = "/sim/settings/labels";
        char value[160] = "";
        std::string status;
        std::map<const sl::PropertyNode *, std::string> edit; // text being typed for a string node
    };

    /** The F4 property browser window. */
    void draw_property_browser(sl::PropertyTree &props, bool &open, PropertyBrowser &state);
} // namespace kosmos
