// KosmosCAD equipment catalogue: the ship.json entries that have a place in space (engines, ports, vessels, gear) or are listed as
// systems (reactors, tanks, ISRU...), and how each is drawn as a marker in the views.
#pragma once

#include "scene.h"

#include <string>
#include <vector>

namespace cad
{
    enum class Shape { Box, CylZ, CylY, Diamond };

    struct KindInfo
    {
        std::string key;      // marker kind
        std::string label;    // shown in menus
        std::string section;  // "engines" (stage), "gear" (stage), or a root array: "ports", "vessels", "wheels", "systems"
        std::string type;     // for systems: the "type" value
        Shape shape = Shape::Box;
        V3 colour{0.9, 0.9, 0.2};
        V3 size{1, 1, 1};
        bool has_direction = false;   // position + direction (engines, ports)
        bool has_size = false;        // the entry carries a "size" (vessels)
        json defaults = json::object();
    };

    const std::vector<KindInfo> &catalogue();
    const KindInfo *find_kind(const std::string &key);
    /** The kind for any ship.json system type, including ones the catalogue does not list (generic). */
    KindInfo kind_for_system(const std::string &type);

    /** A new marker of this kind with its default properties. */
    Marker new_marker(const std::string &key, const V3 &at);

    /** ship.json entry -> marker (the entry is kept whole in `props`; position/direction/size are lifted out). */
    Marker marker_from_entry(const std::string &key, const json &entry, int stage, const std::string &source);
    /** Marker -> the ship.json entry (props + position, direction, size, name). */
    json entry_from_marker(const Marker &m);
    /** Direction (unit) of a marker whose kind has one: local -Z through its rotation. */
    V3 marker_direction(const Marker &m);
    /** Euler degrees that point local -Z along `dir`. */
    V3 rotation_for_direction(const V3 &dir);
}
