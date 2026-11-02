#include "equipment.h"

#include <cmath>

namespace cad
{
    namespace
    {
        std::vector<KindInfo> build()
        {
            std::vector<KindInfo> k;
            auto add = [&](KindInfo i) { k.push_back(std::move(i)); };
            auto sys = [&](const char *type, const char *label, V3 colour, Shape shape, V3 size, json defaults)
            {
                KindInfo i;
                i.key = std::string("sys:") + type;
                i.label = label;
                i.section = "systems";
                i.type = type;
                i.shape = shape;
                i.colour = colour;
                i.size = size;
                i.defaults = std::move(defaults);
                i.defaults["type"] = type;
                add(std::move(i));
            };
            {
                KindInfo i;
                i.key = "engine";
                i.label = "Engine";
                i.section = "engines";
                i.shape = Shape::CylZ;
                i.colour = {1.0, 0.55, 0.15};
                i.size = {1, 1, 1.6};
                i.has_direction = true;
                i.defaults = {{"thrust", 100000}, {"isp", 320}, {"type", "rocket"}};
                add(std::move(i));
            }
            {
                KindInfo i;
                i.key = "port";
                i.label = "Docking port";
                i.section = "ports";
                i.shape = Shape::CylZ;
                i.colour = {0.3, 0.9, 1.0};
                i.size = {1.2, 1.2, 0.3};
                i.has_direction = true;
                i.defaults = {{"up", {0, 1, 0}}};
                add(std::move(i));
            }
            {
                KindInfo i;
                i.key = "vessel";
                i.label = "Pressure vessel (cabin)";
                i.section = "vessels";
                i.shape = Shape::Box;
                i.colour = {0.4, 1.0, 0.5};
                i.size = {2.2, 1.8, 3};
                i.has_size = true;
                i.defaults = {{"crew", 2}};
                add(std::move(i));
            }
            {
                KindInfo i;
                i.key = "wheel";
                i.label = "Wheel";
                i.section = "wheels";
                i.shape = Shape::Diamond;
                i.colour = {0.8, 0.8, 0.8};
                i.size = {0.6, 0.6, 0.6};
                i.defaults = {{"rolling_resistance", 0.03}};
                add(std::move(i));
            }
            {
                KindInfo i;
                i.key = "gear";
                i.label = "Landing gear foot";
                i.section = "gear";
                i.shape = Shape::Diamond;
                i.colour = {0.8, 0.8, 0.5};
                i.size = {0.6, 0.6, 0.6};
                add(std::move(i));
            }
            sys("fission", "Fission reactor", {1.0, 0.4, 0.9}, Shape::CylY, {1.4, 1.4, 1.4}, {{"name", "Fission reactor"}, {"watts", 8000}, {"fuel_kg", 10}});
            sys("fusion", "Fusion reactor", {1.0, 0.5, 1.0}, Shape::CylY, {1.6, 1.6, 1.6}, {{"name", "Fusion reactor"}, {"watts", 40000}, {"fuel_kg", 10}});
            sys("rtg", "RTG", {0.9, 0.4, 0.6}, Shape::CylY, {0.5, 0.5, 0.5}, {{"name", "RTG"}, {"watts", 300}});
            sys("solar", "Solar array", {0.3, 0.5, 1.0}, Shape::Box, {4, 0.1, 2}, {{"name", "Solar"}, {"area_m2", 8}, {"efficiency", 0.28}, {"facing", {0, 1, 0}}});
            sys("battery", "Battery", {1.0, 0.9, 0.3}, Shape::Box, {0.8, 0.5, 0.8}, {{"name", "Batteries"}, {"capacity_wh", 6000}, {"max_w", 3000}});
            sys("fuel_cell", "Fuel cell", {0.9, 0.9, 0.4}, Shape::Box, {0.8, 0.8, 0.8}, {{"name", "Fuel cell"}, {"watts", 3000}});
            sys("tank", "Gas / water tank", {0.4, 0.8, 1.0}, Shape::CylY, {1, 1.5, 1}, {{"name", "Oxygen"}, {"gas", "oxygen"}, {"capacity_kg", 100}, {"kg", 100}});
            sys("regulator", "Cabin regulator", {0.6, 0.9, 0.6}, Shape::Box, {0.4, 0.4, 0.4}, {{"name", "Cabin regulator"}, {"vessel", "Cabin"}});
            sys("scrubber", "CO2 scrubber", {0.6, 0.9, 0.6}, Shape::Box, {0.5, 0.5, 0.5}, {{"name", "CO2 scrubber"}, {"vessel", "Cabin"}, {"kg_per_day", 3}, {"sorbent_kg", 100}});
            sys("isru", "ISRU ice miner", {0.5, 0.9, 0.9}, Shape::Box, {1.2, 1.2, 1.2}, {{"name", "Ice miner"}, {"site", true}, {"watts", 1200}, {"kg_per_day", 300}});
            sys("electrolyser", "Electrolyser", {0.5, 0.8, 0.9}, Shape::Box, {0.8, 0.8, 0.8}, {{"name", "Electrolyser"}, {"watts", 800}});
            sys("refinery", "Propellant refinery", {0.6, 0.8, 0.9}, Shape::Box, {1, 1, 1}, {{"name", "Propellant refinery"}, {"watts", 1500}, {"kg_per_day", 400}});
            sys("load", "Power load", {0.8, 0.8, 0.8}, Shape::Box, {0.4, 0.4, 0.4}, {{"name", "Avionics"}, {"watts", 400}, {"priority", 0}});
            sys("comms", "Comms", {0.8, 0.7, 1.0}, Shape::Box, {0.4, 0.4, 0.4}, {{"name", "Comms"}, {"watts", 50}});
            return k;
        }

        V3 read_v3(const json &j, const V3 &fallback)
        {
            if (!j.is_array() || j.size() < 3 || !j[0].is_number())
                return fallback;
            return {j[0].get<double>(), j[1].get<double>(), j[2].get<double>()};
        }

        json to_json(const V3 &v) { return json::array({v.x, v.y, v.z}); }
    }

    const std::vector<KindInfo> &catalogue()
    {
        static const std::vector<KindInfo> k = build();
        return k;
    }

    const KindInfo *find_kind(const std::string &key)
    {
        for (const KindInfo &k : catalogue())
            if (k.key == key)
                return &k;
        return nullptr;
    }

    KindInfo kind_for_system(const std::string &type)
    {
        if (const KindInfo *k = find_kind("sys:" + type))
            return *k;
        KindInfo i;
        i.key = "sys:" + type;
        i.label = type;
        i.section = "systems";
        i.type = type;
        i.colour = {0.8, 0.8, 0.8};
        i.size = {0.5, 0.5, 0.5};
        i.defaults = {{"type", type}};
        return i;
    }

    V3 rotation_for_direction(const V3 &dir)
    {
        const V3 d = normalized(dir);
        if (length(d) < 0.5)
            return {};
        // Local -Z must become d. Yaw about Y then pitch about X (applied X first, then Y).
        const double pitch = std::asin(std::clamp(d.y, -1.0, 1.0));
        // After rotating by Ry(yaw): (0,0,-1) -> (-sin yaw, 0, -cos yaw), so yaw = atan2(-d.x, -d.z)
        const double yaw = std::atan2(-d.x, -d.z);
        // The pitch must lift -Z toward +Y: Rx(a) maps (0,0,-1) to (0, sin a, -cos a)
        return {pitch * 180 / kPi, yaw * 180 / kPi, 0};
    }

    V3 marker_direction(const Marker &m)
    {
        V3 d = normalized(Xform{{}, m.rot, {1, 1, 1}}.apply_dir({0, 0, -1}));
        auto snap = [](double v) { return std::fabs(v) < 1e-9 ? 0.0 : v; };
        return {snap(d.x), snap(d.y), snap(d.z)};
    }

    Marker new_marker(const std::string &key, const V3 &at)
    {
        const KindInfo *info = find_kind(key);
        Marker m;
        m.kind = key;
        m.pos = at;
        if (info)
        {
            m.size = info->size;
            m.props = info->defaults;
            m.name = info->defaults.value("name", info->label);
            m.props.erase("name");
            if (info->has_direction)
                m.rot = rotation_for_direction({0, 0, info->key == "port" ? -1.0 : 1.0});
        }
        m.placed = true;
        return m;
    }

    Marker marker_from_entry(const std::string &key, const json &entry, int stage, const std::string &source)
    {
        KindInfo info = find_kind(key) ? *find_kind(key) : kind_for_system(key.rfind("sys:", 0) == 0 ? key.substr(4) : key);
        Marker m;
        m.kind = info.key;
        m.stage = stage;
        m.source = source;
        m.size = info.size;
        if (entry.is_array()) // a gear foot is a bare [x, y, z]
        {
            m.pos = read_v3(entry, {});
            return m;
        }
        m.props = entry;
        m.name = entry.value("name", "");
        m.props.erase("name");
        if (entry.contains("position"))
        {
            m.pos = read_v3(entry["position"], {});
            m.props.erase("position");
        }
        else if (info.section == "systems")
        {
            // Systems have no place in ship.json; KosmosCAD keeps one under cad_position so the view can show it.
            if (entry.contains("cad_position"))
            {
                m.pos = read_v3(entry["cad_position"], {});
                m.props.erase("cad_position");
            }
            else
                m.placed = false;
            if (entry.contains("cad_size"))
            {
                m.size = read_v3(entry["cad_size"], m.size);
                m.props.erase("cad_size");
            }
        }
        if (info.has_direction && entry.contains("direction"))
        {
            m.rot = rotation_for_direction(read_v3(entry["direction"], {0, 0, -1}));
            m.props.erase("direction");
        }
        if (info.has_size && entry.contains("size"))
        {
            m.size = read_v3(entry["size"], m.size);
            m.props.erase("size");
        }
        return m;
    }

    json entry_from_marker(const Marker &m)
    {
        const KindInfo *info = find_kind(m.kind);
        const bool is_system = m.kind.rfind("sys:", 0) == 0;
        if (m.kind == "gear")
            return to_json(m.pos);
        json e = json::object();
        if (is_system && m.props.contains("type"))
            e["type"] = m.props["type"];
        if (!m.name.empty())
            e["name"] = m.name;
        for (auto it = m.props.begin(); it != m.props.end(); ++it)
            if (!e.contains(it.key()))
                e[it.key()] = it.value();
        if (is_system)
        {
            if (m.placed)
            {
                e["cad_position"] = to_json(m.pos);
                e["cad_size"] = to_json(m.size);
            }
            return e;
        }
        e["position"] = to_json(m.pos);
        if (info && info->has_direction)
            e["direction"] = to_json(marker_direction(m));
        if (info && info->has_size)
            e["size"] = to_json(m.size);
        return e;
    }
}
