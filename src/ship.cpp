#include "ship.h"

#include "json.hpp"
#include "part_kit.h"
#include "reentry_json.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <cstdio>
#include <fstream>

namespace kosmos
{
    namespace
    {
        constexpr double pi = 3.14159265358979323846;
        constexpr double g0 = 9.80665;

        sl::dvec3 vec3(const nlohmann::json &j, sl::dvec3 fallback = {0.0, 0.0, 0.0})
        {
            if (!j.is_array() || j.size() < 3)
                return fallback;
            return {j[0].get<double>(), j[1].get<double>(), j[2].get<double>()};
        }

        // Rotation whose nose (-Z) points along `nose` and whose roof (+Y) is as close to `roof` as possible.
        sl::dquat orientation_from(const sl::dvec3 &nose, const sl::dvec3 &roof)
        {
            const sl::dvec3 z = -glm::normalize(nose);
            sl::dvec3 x = glm::cross(roof, z);
            if (glm::length(x) < 1e-9)
                x = glm::cross(sl::dvec3(0.0, 0.0, 1.0), z);
            x = glm::normalize(x);
            const sl::dvec3 y = glm::cross(z, x);
            return glm::normalize(glm::quat_cast(sl::dmat3(x, y, z)));
        }

        double atmosphere_top(const Body &body)
        {
            return body.atmosphere_density > 0.0 ? body.atmosphere_scale_height * 10.0 : 0.0;
        }
    } // namespace

    const char *engine_type_name(EngineType type)
    {
        switch (type)
        {
        case EngineType::jet: return "jet";
        case EngineType::scramjet: return "scramjet";
        case EngineType::rocket: return "rocket";
        case EngineType::solid: return "solid";
        case EngineType::nuclear: return "nuclear";
        case EngineType::hyperjump: return "hyperjump";
        }
        return "rocket";
    }

    EngineType engine_type_from_name(const std::string &name, EngineType fallback)
    {
        for (EngineType t : {EngineType::jet, EngineType::scramjet, EngineType::rocket, EngineType::solid, EngineType::nuclear, EngineType::hyperjump})
            if (name == engine_type_name(t))
                return t;
        return fallback;
    }

    namespace
    {
        void read_module(const nlohmann::json &json, ShipSpec &spec)
        {
            if (json.contains("module") && json["module"].is_object())
            {
                const nlohmann::json &jm = json["module"];
                spec.module_addon = jm.value("addon", "");
                spec.module_max_force = jm.value("max_force", 0.0);
                spec.module_max_torque = jm.value("max_torque", 0.0);
            }
        }

        void read_cockpit(const nlohmann::json &jc, ShipSpec &spec)
        {
                spec.cockpit.present = true;
                spec.cockpit.eye_set = jc.contains("eye");
                spec.cockpit.eye = vec3(jc.value("eye", nlohmann::json()));
                spec.cockpit.yaw = jc.value("yaw", 0.0);
                spec.cockpit.pitch = jc.value("pitch", 0.0);
                spec.cockpit.fov = jc.value("fov", 0.0);
                spec.cockpit.mfds = jc.value("mfds", true);
                spec.cockpit.vc_model = jc.value("vc_model", "");
                if (jc.contains("vc_animations") && jc["vc_animations"].is_array())
                    for (const auto &ja : jc["vc_animations"])
                    {
                        VcAnimation a;
                        a.parts = ja.value("parts", std::vector<std::string>());
                        a.source = ja.value("source", "");
                        a.angle_deg = ja.value("angle", 0.0);
                        a.initial = ja.value("initial", 0.0);
                        if (ja.contains("move") && ja["move"].is_array() && ja["move"].size() == 3)
                        {
                            a.slide = true;
                            for (int i = 0; i < 3; ++i)
                                a.move[i] = ja["move"][static_cast<std::size_t>(i)].get<double>();
                        }
                        const std::vector<double> pv = ja.value("pivot", std::vector<double>{0, 0, 0}), av = ja.value("axis", std::vector<double>{1, 0, 0});
                        for (int i = 0; i < 3 && i < static_cast<int>(pv.size()); ++i)
                            a.pivot[i] = pv[i];
                        for (int i = 0; i < 3 && i < static_cast<int>(av.size()); ++i)
                            a.axis[i] = av[i];
                        spec.cockpit.vc_animations.push_back(a);
                    }
                spec.cockpit.mfd_left = jc.value("mfd_left", spec.cockpit.mfd_left);
                spec.cockpit.mfd_right = jc.value("mfd_right", spec.cockpit.mfd_right);
                spec.cockpit.mfd_filter = jc.value("mfd_filter", spec.cockpit.mfd_filter);
                spec.cockpit.mfd_hide = jc.value("mfd_hide", spec.cockpit.mfd_hide);
                spec.cockpit.hud_modes = jc.value("hud_modes", spec.cockpit.hud_modes);
                if (jc.contains("head_motion") && jc["head_motion"].is_object())
                {
                    const nlohmann::json &jh = jc["head_motion"];
                    HeadMotion &hm = spec.cockpit.head_motion;
                    hm.enabled = jh.value("enabled", hm.enabled);
                    hm.strength = std::clamp(jh.value("strength", hm.strength), 0.0, 4.0);
                    hm.frequency = std::clamp(jh.value("frequency", hm.frequency), 0.5, 10.0);
                    hm.damping = std::clamp(jh.value("damping", hm.damping), 0.05, 2.0);
                    hm.max_offset = std::clamp(jh.value("max", hm.max_offset), 0.0, 0.6);
                }
                for (const nlohmann::json &ji : jc.value("panel", nlohmann::json::array()))
                    spec.cockpit.panel.push_back({ji.value("label", ""), ji.value("property", ""), ji.value("scale", 1.0), ji.value("format", "%.1f")});
                if (jc.contains("layout"))
                {
                    const nlohmann::json &jl = jc["layout"];
                    spec.cockpit.panel_width = jl.value("width", spec.cockpit.panel_width);
                    spec.cockpit.panel_height = jl.value("height", spec.cockpit.panel_height);
                    spec.cockpit.panel_image = jl.value("image", "");
                    spec.cockpit.panel_name = jl.value("name", spec.cockpit.panel_name);
                    if (jl.contains("position"))
                        spec.cockpit.panel_pos = vec3(jl["position"]);
                    spec.cockpit.panel_size = jl.value("size", spec.cockpit.panel_size);
                    spec.cockpit.panel_tilt = jl.value("tilt", spec.cockpit.panel_tilt);
                }
                const auto read_controls = [](const nlohmann::json &list)
                {
                    std::vector<Cockpit::PanelControl> out;
                    for (const nlohmann::json &jk : list)
                    {
                        Cockpit::PanelControl pc;
                        pc.type = jk.value("type", "button");
                        pc.property = jk.value("property", "");
                        pc.label = jk.value("label", "");
                        pc.format = jk.value("format", pc.format);
                        pc.scale = jk.value("scale", 1.0f);
                        pc.max = jk.value("max", 1.0f);
                        pc.alarm = jk.value("alarm", false);
                        if (jk.contains("value"))
                            pc.value = jk["value"].is_string() ? jk["value"].get<std::string>() : jk["value"].dump();
                        pc.x = jk.value("x", 0.0f);
                        pc.y = jk.value("y", 0.0f);
                        pc.w = jk.value("w", 20.0f);
                        pc.h = jk.value("h", 20.0f);
                        pc.unit = jk.value("unit", 0);
                        pc.button = jk.value("button", 0);
                        pc.style = jk.value("style", "");
                        pc.cx = jk.value("cx", 0.0f);
                        pc.cy = jk.value("cy", 0.0f);
                        pc.r = jk.value("r", 0.0f);
                        pc.angle = jk.value("angle", 0.0f);
                        pc.a0 = jk.value("a0", pc.a0);
                        pc.a1 = jk.value("a1", pc.a1);
                        pc.min = jk.value("min", pc.min);
                        pc.offset = jk.value("offset", pc.offset);
                        pc.lo = jk.value("lo", pc.lo);
                        pc.hi = jk.value("hi", pc.hi);
                        pc.gate_property = jk.value("gate_property", "");
                        pc.gate_min = jk.value("gate_min", pc.gate_min);
                        pc.gate_max = jk.value("gate_max", pc.gate_max);
                        out.push_back(pc);
                    }
                    return out;
                };
                spec.cockpit.controls = read_controls(jc.value("controls", nlohmann::json::array()));
                spec.cockpit.vc_hide = jc.value("vc_hide", std::vector<std::string>());
                for (const nlohmann::json &ji : jc.value("vc_instruments", nlohmann::json::array()))
                    spec.cockpit.vc_instruments.push_back({ji.value("part", std::string()), ji.value("unit", 0)});
                for (const nlohmann::json &jh : jc.value("vc_hotspots", nlohmann::json::array()))
                {
                    if (!jh.contains("quad") || !jh.contains("control") || !jh["quad"].is_array() || jh["quad"].size() < 4)
                        continue;
                    Cockpit::VcHotspot hs;
                    for (int i = 0; i < 4; ++i)
                        for (int k = 0; k < 3; ++k)
                            hs.quad[i][k] = jh["quad"][i][k].get<double>();
                    hs.drag = jh.value("drag", "");
                    hs.drag_gain = jh.value("drag_gain", 1.0);
                    hs.drag_min = jh.value("drag_min", 0.0);
                    hs.drag_max = jh.value("drag_max", 1.0);
                    const std::vector<Cockpit::PanelControl> one = read_controls(nlohmann::json::array({jh["control"]}));
                    hs.control = one.front();
                    hs.follow = jh.value("follow", "");
                    spec.cockpit.vc_hotspots.push_back(hs);
                }
                for (const nlohmann::json &ji : jc.value("instruments", nlohmann::json::array()))
                {
                    Cockpit::PanelInstrument inst;
                    inst.mode = ji.value("mode", "");
                    inst.x = ji.value("x", 0.0f);
                    inst.y = ji.value("y", 0.0f);
                    inst.size = ji.value("size", 200.0f);
                    inst.frame = ji.value("frame", false);
                    inst.vc_only = ji.value("vc_only", false);
                    if (!inst.mode.empty())
                        spec.cockpit.instruments.push_back(inst);
                }
                for (const nlohmann::json &jp : jc.value("panels", nlohmann::json::array()))
                {
                    Cockpit::Panel2D extra;
                    extra.name = jp.value("name", "Panel " + std::to_string(spec.cockpit.extra_panels.size() + 2));
                    extra.width = jp.value("width", extra.width);
                    extra.height = jp.value("height", extra.height);
                    extra.image = jp.value("image", "");
                    for (const nlohmann::json &ji : jp.value("instruments", nlohmann::json::array()))
                    {
                        Cockpit::PanelInstrument inst;
                        inst.mode = ji.value("mode", "");
                        inst.x = ji.value("x", 0.0f);
                        inst.y = ji.value("y", 0.0f);
                        inst.size = ji.value("size", 200.0f);
                        inst.frame = ji.value("frame", false);
                        if (!inst.mode.empty())
                            extra.instruments.push_back(inst);
                    }
                    extra.controls = read_controls(jp.value("controls", nlohmann::json::array()));
                    if (!extra.instruments.empty() || !extra.controls.empty() || !extra.image.empty())
                        spec.cockpit.extra_panels.push_back(extra);
                }
        }

        void read_attachments(const nlohmann::json &json, ShipSpec &spec)
        {
            spec.transponder = json.value("transponder", 0.0);
            for (const nlohmann::json &jd : json.value("ports", nlohmann::json::array()))
            {
                PortSpec port;
                port.name = jd.value("name", port.name);
                port.position = vec3(jd.value("position", nlohmann::json()), port.position);
                port.direction = glm::normalize(vec3(jd.value("direction", nlohmann::json()), port.direction));
                sl::dvec3 up = vec3(jd.value("up", nlohmann::json()), port.up);
                up -= port.direction * glm::dot(up, port.direction);
                port.up = glm::length(up) > 1.0e-6 ? glm::normalize(up) : sl::dvec3(0.0, 1.0, 0.0);
                port.frequency = jd.value("frequency", 0.0);
                spec.ports.push_back(port);
            }
            spec.torque_accel = json.value("torque_accel", spec.torque_accel);
            spec.max_rate = json.value("max_rate", spec.max_rate);
            spec.rcs_thrust = json.value("rcs_thrust", 0.0);
            spec.drag_area = json.value("drag_area", spec.drag_area);
            read_thermal(json.value("thermal", nlohmann::json()), spec.thermal);
            read_hypersonic(json.value("hypersonic", nlohmann::json()), spec.hypersonic);
            read_parachutes(json.value("parachutes", nlohmann::json()), spec.parachutes);
            spec.hit_sphere = json.value("hit_sphere", spec.hit_sphere);
            spec.inertia = vec3(json.value("inertia", nlohmann::json()), spec.inertia);
            spec.damping = vec3(json.value("damping", nlohmann::json()), spec.damping);
            for (const nlohmann::json &jl : json.value("lights", nlohmann::json::array()))
            {
                LightSpec light = parse_light(jl);
                if (!jl.contains("always"))
                    light.always = true;
                spec.lights.push_back(std::move(light));
            }
            for (const nlohmann::json &jv : json.value("cameras", nlohmann::json::array()))
            {
                CameraSpec cam;
                cam.name = jv.value("name", "Interior");
                cam.position = vec3(jv.value("position", nlohmann::json()), cam.position);
                cam.yaw = jv.value("yaw", cam.yaw);
                cam.pitch = jv.value("pitch", cam.pitch);
                cam.fov = jv.value("fov", cam.fov);
                spec.cameras.push_back(std::move(cam));
            }
            for (const nlohmann::json &jf : json.value("surfaces", nlohmann::json::array()))
            {
                SurfaceSpec sf;
                sf.control = jf.value("control", "");
                sf.position = vec3(jf.value("position", nlohmann::json()), sf.position);
                sf.forward = glm::normalize(vec3(jf.value("forward", nlohmann::json()), sf.forward));
                sf.up = glm::normalize(vec3(jf.value("up", nlohmann::json()), sf.up));
                sf.area = jf.value("area", sf.area);
                sf.cl0 = jf.value("cl0", sf.cl0);
                sf.cl_alpha = jf.value("cl_alpha", sf.cl_alpha);
                sf.cd0 = jf.value("cd0", sf.cd0);
                sf.induced_drag = jf.value("induced_drag", sf.induced_drag);
                sf.stall = jf.value("stall_deg", sf.stall * 180.0 / pi) * pi / 180.0;
                sf.gain = jf.value("gain_deg", 0.0) * pi / 180.0;
                spec.surfaces.push_back(std::move(sf));
            }
            for (const nlohmann::json &jw : json.value("wheels", nlohmann::json::array()))
            {
                WheelSpec w;
                w.position = vec3(jw.value("position", nlohmann::json()), w.position);
                w.steer = jw.value("steer_deg", 0.0) * pi / 180.0;
                w.drive = jw.value("drive", false);
                w.rolling_resistance = jw.value("rolling_resistance", w.rolling_resistance);
                spec.wheels.push_back(w);
            }
        }

        void read_ship_options(const nlohmann::json &json, ShipSpec &spec)
        {
            spec.camera_distance = json.value("camera_distance", spec.camera_distance);
            if (json.contains("camera_target"))
            {
                spec.has_camera_target = true;
                spec.camera_target = vec3(json["camera_target"], spec.camera_target);
            }
            spec.horizontal = json.value("horizontal", spec.ground());
            if (json.contains("ground") && json["ground"].is_object())
            {
                spec.ground_force = json["ground"].value("drive_force", spec.ground_force);
                spec.ground_max_speed = json["ground"].value("max_speed", spec.ground_max_speed);
            }
            if (spec.building() && json.contains("interior"))
            {
                std::string error;
                std::shared_ptr<Interior> parsed = Interior::parse(json["interior"], !spec.horizontal, spec.door.z, error);
                if (!parsed)
                    std::fprintf(stderr, "[kosmos] %s: %s\n", spec.source_file.c_str(), error.c_str());
                else
                {
                    if (!parsed->mesh_path.empty())
                        parsed->mesh_path = (std::filesystem::path(spec.source_file).parent_path() / parsed->mesh_path).string();
                    if (!json.contains("door"))
                        spec.door = parsed->door_ship();
                    spec.interior = parsed;
                }
            }
            spec.retractable_gear = json.value("retractable_gear", false);
            spec.rcs_modes = json.value("rcs_modes", false);
            spec.rcs_effects = json.value("rcs_effects", true);
            spec.rcs_propellant = json.value("rcs_propellant", 0.0);
            spec.rcs_isp = json.value("rcs_isp", 280.0);
            spec.hybrid = json.value("hybrid", false);
            spec.pod_turn_rate = json.value("pod_turn_rate", spec.pod_turn_rate);
            if (json.contains("animation_controls"))
                for (const auto &ja : json["animation_controls"])
                {
                    ShipSpec::AnimationControl a;
                    a.property = ja.value("property", "");
                    a.name = ja.value("name", a.property);
                    a.duration = std::max(0.1, ja.value("duration", a.duration));
                    a.initial = std::clamp(ja.value("initial", 0.0), 0.0, 1.0);
                    if (!a.property.empty())
                        spec.animation_controls.push_back(a);
                }
            if (json.contains("hardpoints"))
                for (const auto &jh : json["hardpoints"])
                {
                    ShipSpec::Hardpoint h;
                    h.equipment = jh.value("equipment", "");
                    h.name = jh.value("name", h.equipment);
                    h.count = std::max(0, jh.value("count", 1));
                    if (jh.contains("position"))
                        h.position = vec3(jh["position"]);
                    h.eject = glm::normalize(vec3(jh.value("eject", nlohmann::json()), h.eject));
                    h.eject_speed = jh.value("eject_speed", h.eject_speed);
                    if (!h.equipment.empty())
                        spec.hardpoints.push_back(h);
                }
            if (json.contains("weapons"))
                for (const auto &jw : json["weapons"])
                {
                    ShipSpec::WeaponMount w;
                    w.weapon = jw.value("weapon", "");
                    w.name = jw.value("name", w.weapon);
                    w.group = jw.value("group", w.group);
                    if (jw.contains("position"))
                        w.position = vec3(jw["position"]);
                    if (jw.contains("direction"))
                    {
                        const sl::dvec3 d = vec3(jw["direction"]);
                        if (glm::length(d) > 1.0e-6)
                            w.direction = glm::normalize(d);
                    }
                    if (!w.weapon.empty())
                        spec.weapons.push_back(w);
                }
            if (json.contains("shield"))
            {
                const auto &js = json["shield"];
                spec.shield.capacity = std::max(0.0, js.value("capacity", 0.0));
                spec.shield.recharge = std::max(0.0, js.value("recharge", spec.shield.recharge));
                spec.shield.delay = std::max(0.0, js.value("delay", spec.shield.delay));
                if (js.contains("colour") && js["colour"].size() >= 3)
                    for (int i = 0; i < 3; ++i)
                        spec.shield.colour[i] = std::clamp(js["colour"][static_cast<std::size_t>(i)].get<int>(), 0, 255);
            }
            if (json.contains("locker"))
                spec.locker = json["locker"].get<std::vector<std::string>>();
            spec.fuel_pod = json.value("fuel_pod", false);
            spec.gear_time = std::max(0.1, json.value("gear_time", spec.gear_time));
            spec.gimbal_range = std::max(0.0, json.value("gimbal_range", spec.gimbal_range));
        }

        StageSpec read_stage(const nlohmann::json &js)
        {
                StageSpec stage;
                stage.name = js.value("name", "Stage");
                stage.dry_mass = js.value("dry_mass", stage.dry_mass);
                stage.propellant = js.value("propellant", 0.0);
                stage.fuel_start = std::clamp(js.value("fuel_start", 1.0), 0.0, 1.0);
                stage.z = js.value("z", 0.0);
                stage.length = js.value("length", stage.length);
                stage.radius = js.value("radius", stage.radius);
                stage.separation_speed = js.value("separation_speed", stage.separation_speed);
                stage.separation_dir = vec3(js.value("separation_dir", nlohmann::json()), stage.separation_dir);
                for (const nlohmann::json &je : js.value("engines", nlohmann::json::array()))
                {
                    EngineSpec e;
                    e.position = vec3(je.value("position", nlohmann::json()), e.position);
                    e.direction = glm::normalize(vec3(je.value("direction", nlohmann::json()), e.direction));
                    e.thrust = je.value("thrust", 0.0);
                    e.isp = je.value("isp", e.isp);
                    e.isp_sl = je.value("isp_sl", e.isp_sl);
                    e.plume = je.value("plume", e.plume);
                    e.plume_width = je.value("plume_width", e.plume_width);
                    e.plume_glow = je.value("plume_glow", e.plume_glow);
                    e.plume_density = std::max(0.1, je.value("plume_density", e.plume_density));
                    e.plume_billow = std::clamp(je.value("plume_billow", e.plume_billow), 0.0, 1.0);
                    e.hover = je.value("group", std::string("main")) == "hover";
                    e.pod = je.value("group", std::string("main")) == "pod";
                    e.air_breathing = je.value("air_breathing", false);
                    if (je.value("group", std::string()) == "rcs")
                        for (const nlohmann::json &g : je.value("rcs_groups", nlohmann::json::array()))
                            e.rcs_groups.push_back(g.get<std::string>());
                    // Older specs only had air_breathing; that still means a jet.
                    e.type = engine_type_from_name(je.value("type", ""), e.air_breathing ? EngineType::jet : EngineType::rocket);
                    e.fuel = je.value("fuel", e.type == EngineType::solid ? std::string("solid") : e.type == EngineType::nuclear ? std::string("hydrogen") : e.fuel);
                    e.afterburner = je.value("afterburner", false);
                    e.range_ly = je.value("range_ly", e.range_ly);
                    if (e.type == EngineType::jet || (e.type == EngineType::scramjet && !je.contains("air_breathing")))
                        e.air_breathing = true;
                    stage.engines.push_back(e);
                }
                for (const nlohmann::json &jg : js.value("gear", nlohmann::json::array()))
                    stage.gear.push_back(vec3(jg));
                for (const nlohmann::json &jp : js.value("parts", nlohmann::json::array()))
                {
                    PartSpec part;
                    part.prop = jp.value("prop", "");
                    part.model = jp.value("model", "");
                    part.animations = jp.value("animations", "");
                    if (part.prop.empty() && part.model.empty())
                        for (const nlohmann::json &pt : jp.at("profile"))
                            part.profile.emplace_back(pt[0].get<float>(), pt[1].get<float>());
                    part.radius = jp.value("radius", 1.0f);
                    part.top_radius = jp.value("top_radius", part.radius);
                    part.length = jp.value("length", 1.0f);
                    part.span = jp.value("span", 1.0f);
                    part.count = jp.value("count", 1);
                    part.z = jp.value("z", 0.0f);
                    if (jp.contains("accent"))
                        for (int i = 0; i < 3; ++i)
                            part.accent[i] = jp["accent"][i].get<float>() / 255.0f;
                    if (jp.contains("colour"))
                        for (int i = 0; i < 3; ++i)
                            part.colour[i] = jp["colour"][i].get<float>() / 255.0f;
                    part.emissive = jp.value("emissive", 0.0f);
                    part.offset = vec3(jp.value("offset", nlohmann::json()));
                    if (jp.contains("scale"))
                        part.scale = jp["scale"].is_array() ? vec3(jp["scale"], part.scale) : sl::dvec3(jp["scale"].get<double>());
                    part.rotate = vec3(jp.value("rotate", nlohmann::json()), part.rotate);
                    stage.parts.push_back(std::move(part));
                }
            return stage;
        }

        void finish_ship_spec(ShipSpec &spec)
        {
            // Every ship has a cockpit camera: by default at the front centre of the hull, looking along the nose.
            if (!spec.cockpit.present)
            {
                spec.cockpit.present = true;
                spec.cockpit.mfds = false;
            }
            if (!spec.cockpit.eye_set)
            {
                const StageSpec *front = &spec.stages.front();
                for (const StageSpec &st : spec.stages)
                    if (st.z - st.length * 0.5 < front->z - front->length * 0.5)
                        front = &st;
                spec.cockpit.eye = sl::dvec3(0.0, front->radius * 0.3, front->z - front->length * 0.5);
            }
            // A light with no stage of its own belongs to the stage it sits on, and goes when that stage does.
            for (LightSpec &light : spec.lights)
            {
                if (light.stage >= 0)
                    continue;
                double best = 1.0e30;
                for (std::size_t s = 0; s < spec.stages.size(); ++s)
                {
                    const StageSpec &st = spec.stages[s];
                    const double gap = std::max({0.0, light.z - (st.z + st.length * 0.5), (st.z - st.length * 0.5) - light.z});
                    if (gap < best - 1.0e-9)
                    {
                        best = gap;
                        light.stage = static_cast<int>(s);
                    }
                }
            }
        }
    }

    bool load_ship_spec(const std::string &file, ShipSpec &spec, std::string &error)
    {
        std::ifstream in(file);
        if (!in)
        {
            error = "cannot open " + file;
            return false;
        }
        nlohmann::json json;
        try
        {
            in >> json;
            if (!expand_part_kits(json, std::filesystem::path(file).parent_path(), error))
            {
                error = file + ": " + error;
                return false;
            }
            spec = ShipSpec();
            spec.source_file = file;
            spec.name = json.value("name", "Ship");
            spec.kind = json.value("class", "ship");
            spec.pad_turn = json.value("pad_turn", 0.0);
            spec.orbital_only = json.value("orbital_only", spec.kind == "transfer" || spec.kind == "tug" || spec.kind == "station" || spec.kind == "probe");
            spec.crash_speed = json.value("crash_speed", 5.0);
            if (spec.building())
            {
                spec.orbital_only = false;
                spec.door = vec3(json.value("door", nlohmann::json()), spec.door);
                spec.door_radius = json.value("door_radius", 4.0);
                spec.grid = json.value("grid", "");
            }
            spec.script = json.value("script", "");
            // A building's cockpit is its interface: either key works there.
            const char *cockpit_key = json.contains("cockpit") || !spec.building() ? "cockpit" : "interface";
            read_module(json, spec);
            if (json.contains(cockpit_key))
                read_cockpit(json[cockpit_key], spec);
            read_attachments(json, spec);
            read_ship_options(json, spec);
            for (const nlohmann::json &js : json.at("stages"))
            {
                spec.stages.push_back(read_stage(js));
            }
            std::vector<std::string> stage_names;
            for (const StageSpec &st : spec.stages)
                stage_names.push_back(st.name);
            parse_systems(json, stage_names, spec.systems);
            if (spec.directory.empty())
                spec.directory = std::filesystem::path(file).parent_path().string();
            find_tagged_vessels(spec);
        }
        catch (const std::exception &e)
        {
            error = file + ": " + e.what();
            return false;
        }
        if (spec.stages.empty())
        {
            error = file + ": a ship needs at least one stage";
            return false;
        }
        finish_ship_spec(spec);
        return true;
    }

    Ship::Ship(Universe &universe, ShipSpec spec) : name(spec.name), universe_(universe), spec_(std::move(spec))
    {
        build_body();
        rcs_fuel_ = spec_.rcs_propellant;
        shield_ = spec_.shield.capacity;
        if (!spec_.systems.empty())
            systems_ = std::make_unique<Systems>(spec_.systems, life_support_);
    }

    void Ship::set_life_support(LifeSupportMode mode)
    {
        life_support_ = mode;
        if (systems_)
            systems_->set_mode(mode);
    }

    Ship::~Ship()
    {
        unpublish();
        for (const Debris &d : debris_)
            universe_.world.destroy_body(d.body);
        universe_.world.destroy_body(body_);
    }

    sl::FlightBody &Ship::body() { return *universe_.world.body(body_); }
    const sl::FlightBody &Ship::body() const { return *universe_.world.body(body_); }

    namespace
    {
        // Bits of the RCS control groups; see EngineSpec::rcs_groups.
        enum RcsGroup : unsigned
        {
            g_left = 1, g_right = 2, g_up = 4, g_down = 8, g_forward = 16, g_back = 32,
            g_pitch_up = 64, g_pitch_down = 128, g_yaw_left = 256, g_yaw_right = 512, g_bank_left = 1024, g_bank_right = 2048
        };

        unsigned rcs_group_bit(const std::string &name)
        {
            static const char *names[] = {"left", "right", "up", "down", "forward", "back", "pitch-up", "pitch-down", "yaw-left", "yaw-right", "bank-left", "bank-right"};
            for (unsigned i = 0; i < 12; ++i)
                if (name == names[i])
                    return 1u << i;
            return 0;
        }
    }

    void Ship::build_body()
    {
        body_ = universe_.world.create_body(name);
        sl::FlightBody &b = body();
        b.drag_area = spec_.drag_area;
        b.angular_damping = 0.0;
        for (std::size_t s = 0; s < spec_.stages.size(); ++s)
        {
            const StageSpec &stage = spec_.stages[s];
            b.tanks.push_back({stage.propellant, stage.propellant * stage.fuel_start});
            stage_first_thruster_.push_back(static_cast<int>(b.thrusters.size()));
            for (const EngineSpec &e : stage.engines)
            {
                sl::Thruster t;
                t.position = e.position;
                t.direction = e.direction;
                t.max_thrust = e.type == EngineType::hyperjump ? 0.0 : e.thrust;
                t.isp = e.isp;
                t.isp_sea_level = e.isp_sl;
                t.tank = static_cast<int>(s);
                t.air_breathing = e.air_breathing;
                b.thrusters.push_back(t);
                thruster_hover_.push_back(e.hover);
                unsigned mask = 0;
                for (const std::string &g : e.rcs_groups)
                    mask |= rcs_group_bit(g);
                thruster_rcs_.push_back(mask);
                explicit_rcs_ = explicit_rcs_ || mask != 0;
                thruster_pod_.push_back(e.pod ? (e.position.x < 0.0 ? 0 : 1) : -1);
            }
            for (const sl::dvec3 &g : stage.gear)
            {
                sl::GearContact contact;
                contact.position = g;
                contact.forward = {0.0, 0.0, -1.0};
                contact.rolling_resistance = 0.3;
                b.gear.push_back(contact);
            }
        }
        stage_first_thruster_.push_back(static_cast<int>(b.thrusters.size()));
        for (const WheelSpec &w : spec_.wheels)
        {
            sl::GearContact contact;
            contact.position = w.position;
            contact.forward = {0.0, 0.0, -1.0};
            contact.rolling_resistance = w.rolling_resistance;
            b.gear.push_back(contact);
        }
        for (const SurfaceSpec &sf : spec_.surfaces)
        {
            sl::LiftingSurface surface;
            surface.control_name = sf.control;
            surface.position = sf.position;
            surface.forward = sf.forward;
            surface.up = sf.up;
            surface.area = sf.area;
            surface.cl0 = sf.cl0;
            surface.cl_alpha = sf.cl_alpha;
            surface.cd0 = sf.cd0;
            surface.induced_drag = sf.induced_drag;
            surface.stall_angle = sf.stall;
            surface.control_gain = sf.gain;
            b.surfaces.push_back(surface);
        }
        refresh_mass_properties();
    }

    void Ship::set_surface_grip(double factor)
    {
        std::vector<sl::GearContact> &gear = body().gear;
        if (base_grip_.size() != gear.size())
        {
            base_grip_.clear();
            for (const sl::GearContact &g : gear)
                base_grip_.emplace_back(g.lateral_friction, g.brake_friction);
        }
        for (std::size_t i = 0; i < gear.size(); ++i)
        {
            gear[i].lateral_friction = base_grip_[i].first * factor;
            gear[i].brake_friction = base_grip_[i].second * factor;
        }
    }

    void Ship::refresh_mass_properties()
    {
        sl::FlightBody &b = body();
        double mass = 0.0, transverse = 0.0, roll = 0.0;
        for (std::size_t s = static_cast<std::size_t>(active_stage_); s < spec_.stages.size(); ++s)
        {
            const StageSpec &st = spec_.stages[s];
            mass += st.dry_mass;
            transverse += st.dry_mass * ((st.z - com_.z) * (st.z - com_.z) + st.length * st.length / 12.0 + st.radius * st.radius / 4.0);
            roll += st.dry_mass * st.radius * st.radius / 2.0;
        }
        mass = std::max(mass, 1.0);
        const double scale = (mass + attached_mass_ + payload_mass_) / mass;
        b.dry_mass = mass + attached_mass_ + payload_mass_;
        b.dry_inertia = {std::max(transverse, 1.0) * scale, std::max(transverse, 1.0) * scale, std::max(roll, 1.0) * scale};
        if (spec_.inertia.x > 0.0 && spec_.inertia.y > 0.0 && spec_.inertia.z > 0.0)
            b.dry_inertia = spec_.inertia * scale;
    }

    void Ship::set_payload_mass(double kilograms)
    {
        if (std::fabs(kilograms - payload_mass_) < 1.0e-6)
            return;
        payload_mass_ = kilograms;
        refresh_mass_properties();
    }

    double Ship::absorb(double amount)
    {
        if (spec_.shield.capacity <= 0.0 || amount <= 0.0)
            return amount;
        shield_quiet_ = 0.0;
        const double taken = std::min(shield_, amount);
        shield_ -= taken;
        return amount - taken;
    }

    void Ship::shield_tick(double dt)
    {
        if (spec_.shield.capacity <= 0.0)
            return;
        shield_quiet_ += dt;
        if (shield_quiet_ >= spec_.shield.delay)
            shield_ = std::min(spec_.shield.capacity, shield_ + spec_.shield.recharge * dt);
    }

    void Ship::damage(double amount)
    {
        if (crashed_ || amount <= 0.0)
            return;
        health_ = std::max(0.0, health_ - amount);
        if (health_ > 0.0)
            return;
        sl::FlightBody &b = body();
        for (sl::Thruster &t : b.thrusters)
            t.level = 0.0;
        // Something in the air is left to fall and crash by the usual rules; the ground and buildings are wrecked on the spot.
        if (spec_.building() || telemetry_.altitude < 50.0 || b.mode != sl::FlightBodyMode::dynamic)
        {
            crashed_ = true;
            pin_to_ground();
        }
    }

    void Ship::set_attached_mass(double kilograms)
    {
        if (std::fabs(kilograms - attached_mass_) < 1.0e-6)
            return;
        const double before = body().mass();
        attached_mass_ = kilograms;
        refresh_mass_properties();
        // The gear springs were sized for the ship alone; stiffen them for what it now carries so it does not sink.
        const double factor = before > 1.0 ? body().mass() / before : 1.0;
        if (gear_k_ > 0.0 && std::fabs(factor - 1.0) > 1.0e-9)
        {
            gear_k_ *= factor;
            for (std::size_t i = 0; i < body().gear.size(); ++i)
            {
                body().gear[i].stiffness *= factor;
                body().gear[i].damping *= factor;
            }
        }
    }

    void Ship::port_world(int index, sl::dvec3 &position, sl::dvec3 &direction, sl::dvec3 &up) const
    {
        const PortSpec &port = spec_.ports[static_cast<std::size_t>(index)];
        const sl::FlightBody &b = body();
        position = b.to_world(port.position - com_);
        const sl::dmat3 r = b.rotation();
        direction = r * port.direction;
        up = r * port.up;
    }

    sl::dvec3 Ship::point_velocity(const sl::dvec3 &world_point) const
    {
        const sl::FlightBody &b = body();
        const sl::dvec3 omega = b.rotation() * b.angular_velocity;
        return b.velocity + glm::cross(omega, world_point - b.position);
    }

    void Ship::place_on_surface(int body_id, const sl::dvec3 &direction, double heading_degrees, double ground_height)
    {
        const Body &planet = universe_.bodies[static_cast<std::size_t>(body_id)];
        const sl::FlightBody &pb = *universe_.world.body(body_id);
        sl::FlightBody &b = body();
        const sl::dvec3 up = glm::normalize(direction);
        const sl::dvec3 east = glm::normalize(glm::cross(pb.spin_axis, up));
        const sl::dvec3 north = glm::cross(up, east);
        const double heading = heading_degrees * pi / 180.0;
        const sl::dvec3 roof = north * std::cos(heading) + east * std::sin(heading);

        const bool level = spec_.aircraft() || spec_.horizontal;
        double lowest = 0.0;
        if (level)
        {
            for (const sl::GearContact &g : b.gear)
                lowest = std::max(lowest, -g.position.y);
        }
        else
            for (const StageSpec &st : spec_.stages)
            {
                lowest = std::max(lowest, st.z + st.length * 0.5);
                for (const sl::dvec3 &g : st.gear)
                    lowest = std::max(lowest, g.z);
            }
        // Gear that carries the whole ship compresses about 0.3 m, unless that is too soft to stop a tall, narrow
        // ship toppling: the springs' restoring torque must beat gravity's by a safe margin.
        const double surface_gravity = planet.gm / (planet.radius * planet.radius);
        const int gear_count = static_cast<int>(b.gear.size());
        double compression = 0.0;
        if (gear_count > 0)
        {
            const double mass = b.mass();
            double height = 0.0, spread = 0.0;
            for (const sl::GearContact &g : b.gear)
            {
                height += std::fabs(g.position.z) / gear_count;
                spread += (g.position.x * g.position.x + g.position.y * g.position.y) / gear_count;
            }
            gear_k_ = mass * surface_gravity / ((spec_.aircraft() || spec_.ground() ? 0.12 : 0.3) * gear_count);
            if (spread > 1e-6 && !level)
                gear_k_ = std::max(gear_k_, 6.0 * mass * surface_gravity * height / (gear_count * spread));
            compression = mass * surface_gravity / (gear_count * gear_k_);
            const double per_gear_mass = mass / gear_count;
            for (sl::GearContact &g : b.gear)
            {
                g.stiffness = gear_k_;
                g.damping = 1.2 * std::sqrt(gear_k_ * per_gear_mass);
            }
        }
        b.position = pb.position + up * (planet.radius + ground_height + lowest - compression);
        b.velocity = pb.velocity + glm::cross(pb.spin_axis * planet.spin, b.position - pb.position);
        b.orientation = orientation_from(level ? roof : up, level ? up : -roof); // rockets lie heads-down, so the roof ends up skyward as the ascent pitches over
        b.angular_velocity = sl::dvec3(0.0);
        crashed_ = false;
        health_ = 1.0;
        rcs_fuel_ = spec_.rcs_propellant;
        shield_ = spec_.shield.capacity;
        pinned_ = resting_ = false;
        update_telemetry();
    }

    void Ship::place_airborne(int body_id, const sl::dvec3 &direction, double heading_degrees, double ground_height, double altitude, double speed)
    {
        place_on_surface(body_id, direction, heading_degrees, ground_height);
        sl::FlightBody &b = body();
        const sl::dvec3 up = glm::normalize(direction);
        b.position += up * altitude;
        b.velocity += b.direction_to_world({0.0, 0.0, -1.0}) * speed;
    }

    void Ship::place_relative(int body_id, const sl::dvec3 &position, const sl::dvec3 &velocity, const sl::dquat &orientation)
    {
        const sl::FlightBody &pb = *universe_.world.body(body_id);
        sl::FlightBody &b = body();
        b.position = pb.position + position;
        b.velocity = pb.velocity + velocity;
        b.orientation = orientation;
        b.angular_velocity = sl::dvec3(0.0);
        crashed_ = false;
        health_ = 1.0;
        rcs_fuel_ = spec_.rcs_propellant;
        shield_ = spec_.shield.capacity;
        pinned_ = resting_ = false;
        update_telemetry();
    }

    void Ship::place_in_orbit(int body_id, double altitude, double inclination_degrees, double phase_degrees)
    {
        gear_position_ = 0.0;
        const Body &planet = universe_.bodies[static_cast<std::size_t>(body_id)];
        const sl::FlightBody &pb = *universe_.world.body(body_id);
        sl::FlightBody &b = body();
        const double r = planet.radius + altitude;
        const double inc = inclination_degrees * pi / 180.0;
        const double phase = phase_degrees * pi / 180.0;
        const sl::dvec3 up0(1.0, 0.0, 0.0);
        const sl::dvec3 prograde0(0.0, std::sin(inc), -std::cos(inc));
        const sl::dvec3 up = up0 * std::cos(phase) + prograde0 * std::sin(phase);
        const sl::dvec3 prograde = prograde0 * std::cos(phase) - up0 * std::sin(phase);
        b.position = pb.position + up * r;
        b.velocity = pb.velocity + prograde * std::sqrt(planet.gm / r);
        b.orientation = orientation_from(prograde, up);
        b.angular_velocity = sl::dvec3(0.0);
        crashed_ = false;
        health_ = 1.0;
        rcs_fuel_ = spec_.rcs_propellant;
        shield_ = spec_.shield.capacity;
        pinned_ = resting_ = false;
        update_telemetry();
    }

    void Ship::place_on_ellipse(int body_id, double periapsis, double apoapsis, double true_anomaly_degrees, int toward)
    {
        const Body &planet = universe_.bodies[static_cast<std::size_t>(body_id)];
        const sl::FlightBody &pb = *universe_.world.body(body_id);
        sl::FlightBody &b = body();
        const double rp = planet.radius + periapsis, ra = planet.radius + std::max(apoapsis, periapsis);
        const double a = 0.5 * (rp + ra), e = (ra - rp) / (ra + rp), mu = planet.gm;
        const double nu = true_anomaly_degrees * pi / 180.0;
        const double p = a * (1.0 - e * e);

        sl::dvec3 normal(0.0, 1.0, 0.0), apse(-1.0, 0.0, 0.0); // normal of the orbit plane, direction of periapsis
        if (toward >= 0 && toward != body_id)
        {
            const sl::FlightBody &tb = *universe_.world.body(toward);
            const sl::dvec3 m = tb.position - pb.position, vm = tb.velocity - pb.velocity;
            normal = glm::normalize(glm::cross(m, vm));
            // Time from the starting anomaly to apoapsis, by Kepler's equation; the target moves on meanwhile.
            const double E = 2.0 * std::atan2(std::sqrt(1.0 - e) * std::sin(0.5 * nu), std::sqrt(1.0 + e) * std::cos(0.5 * nu));
            const double M = E - e * std::sin(E);
            double M_now = std::fmod(M + 2.0 * pi, 2.0 * pi);
            const double n = std::sqrt(mu / (a * a * a));
            const double flight = (pi - M_now) / n;
            const double omega = glm::length(vm) / glm::length(m);
            const sl::dvec3 ahead = m * std::cos(omega * flight) + glm::cross(normal, m) * std::sin(omega * flight);
            apse = -glm::normalize(ahead);
        }
        else
            normal = glm::normalize(normal);
        const sl::dvec3 side = glm::cross(normal, apse); // 90 degrees ahead of periapsis
        const double r = p / (1.0 + e * std::cos(nu));
        const sl::dvec3 up = apse * std::cos(nu) + side * std::sin(nu);
        const sl::dvec3 prograde = side * std::cos(nu) - apse * std::sin(nu);
        const double h = std::sqrt(mu * p);
        const double vr = mu / h * e * std::sin(nu), vt = mu / h * (1.0 + e * std::cos(nu));
        b.position = pb.position + up * r;
        b.velocity = pb.velocity + up * vr + prograde * vt;
        b.orientation = orientation_from(prograde, up);
        b.angular_velocity = sl::dvec3(0.0);
        crashed_ = false;
        health_ = 1.0;
        rcs_fuel_ = spec_.rcs_propellant;
        shield_ = spec_.shield.capacity;
        pinned_ = resting_ = false;
        update_telemetry();
    }

    void Ship::place_on_escape(int body_id, double periapsis, double true_anomaly_degrees, int toward, double flight_days)
    {
        gear_position_ = 0.0;
        const Body &planet = universe_.bodies[static_cast<std::size_t>(body_id)];
        const sl::FlightBody &pb = *universe_.world.body(body_id);
        sl::FlightBody &b = body();
        const int star_id = planet.parent;
        if (star_id < 0 || toward < 0 || toward == body_id)
        {
            place_in_orbit(body_id, periapsis, 0.0, true_anomaly_degrees);
            return;
        }
        const sl::FlightBody &sb = *universe_.world.body(star_id);
        const sl::FlightBody &tb = *universe_.world.body(toward);
        const double mu_sun = universe_.bodies[static_cast<std::size_t>(star_id)].gm;
        const sl::dvec3 r_planet = pb.position - sb.position, v_planet = pb.velocity - sb.velocity;
        const double r1 = glm::length(r_planet), r2 = glm::length(tb.position - sb.position);
        sl::dvec3 normal = glm::normalize(glm::cross(r_planet, v_planet));
        const double signed_v_inf = std::sqrt(mu_sun / r1) * (std::sqrt(2.0 * r2 / (r1 + r2)) - 1.0);
        double v_inf = std::fabs(signed_v_inf);
        sl::dvec3 u = glm::normalize(v_planet) * (signed_v_inf < 0.0 ? -1.0 : 1.0);
        const double mu = planet.gm, rp = planet.radius + periapsis;
        sl::OrbitElements target_orbit, home_orbit;
        if (flight_days > 0.0 && universe_.world.orbit_of(toward, star_id, target_orbit) && universe_.world.orbit_of(body_id, star_id, home_orbit))
        {
            // Lambert's problem by universal variables: the conic from r1 to r2 in `tof`, the short way round about `normal`.
            const sl::dvec3 base_normal = normal;
            auto lambert = [&](const sl::dvec3 &from, const sl::dvec3 &to, double tof, sl::dvec3 &v_from) {
                const double a = glm::length(from), c = glm::length(to);
                double dtheta = std::acos(std::clamp(glm::dot(from, to) / (a * c), -1.0, 1.0));
                if (glm::dot(glm::cross(from, to), base_normal) < 0.0)
                    dtheta = 2.0 * pi - dtheta;
                const double A = std::sin(dtheta) * std::sqrt(a * c / (1.0 - std::cos(dtheta)));
                auto stumpff_c = [](double z) { return std::fabs(z) < 1e-8 ? 0.5 : (z > 0.0 ? (1.0 - std::cos(std::sqrt(z))) / z : (std::cosh(std::sqrt(-z)) - 1.0) / -z); };
                auto stumpff_s = [](double z) {
                    if (std::fabs(z) < 1e-8)
                        return 1.0 / 6.0;
                    return z > 0.0 ? (std::sqrt(z) - std::sin(std::sqrt(z))) / std::pow(z, 1.5) : (std::sinh(std::sqrt(-z)) - std::sqrt(-z)) / std::pow(-z, 1.5);
                };
                auto flight_time = [&](double z, double &y) {
                    const double C = stumpff_c(z), S = stumpff_s(z);
                    y = a + c + A * (z * S - 1.0) / std::sqrt(C);
                    return y < 0.0 ? -1.0 : (std::pow(y / C, 1.5) * S + A * std::sqrt(y)) / std::sqrt(mu_sun);
                };
                double lo = -4.0 * pi * pi, hi = 4.0 * pi * pi, y = 0.0;
                for (int i = 0; i < 200; ++i)
                {
                    const double mid = 0.5 * (lo + hi);
                    if (flight_time(mid, y) < tof)
                        lo = mid;
                    else
                        hi = mid;
                }
                flight_time(0.5 * (lo + hi), y);
                v_from = (to - (1.0 - y / a) * from) / (A * std::sqrt(y / mu_sun));
            };

            const double tof = flight_days * 86400.0;
            const sl::dvec3 r_arrive = sl::state_from_orbit(mu_sun, target_orbit, tof).position;
            const double soi_radius = planet.soi < 1.0e29 ? planet.soi : 0.0;
            sl::dvec3 v_depart;
            lambert(r_planet, r_arrive, tof, v_depart);
            sl::dvec3 excess = v_depart - v_planet;
            // The transfer must leave the planet's sphere of influence rather than its centre, so aim from the exit point and time, and repeat as the asymptote settles.
            for (int pass = 0; pass < 8 && soi_radius > 0.0 && glm::length(excess) > 1.0; ++pass)
            {
                const double speed_exit2 = glm::dot(excess, excess);
                const double vinf2 = speed_exit2 - 2.0 * mu / soi_radius;
                if (vinf2 <= 0.0)
                    break;
                const double vi = std::sqrt(vinf2), ecc = 1.0 + rp * vinf2 / mu, sma = mu / vinf2;
                const sl::dvec3 asym = excess / glm::length(excess);
                const sl::dvec3 across = base_normal - asym * glm::dot(asym, base_normal);
                if (glm::length(across) < 1e-6)
                    break;
                const sl::dvec3 nrm = glm::normalize(across);
                const double alpha = std::acos(-1.0 / ecc);
                const sl::dvec3 apse = asym * std::cos(alpha) - glm::cross(nrm, asym) * std::sin(alpha);
                const double nu_exit = std::acos(std::clamp(((rp * (1.0 + ecc)) / soi_radius - 1.0) / ecc, -1.0, 1.0));
                const double hyper = std::acosh((1.0 + soi_radius / sma) / ecc);
                const double t_exit = std::sqrt(sma * sma * sma / mu) * (ecc * std::sinh(hyper) - hyper);
                if (t_exit >= tof)
                    break;
                const sl::dvec3 exit_dir = apse * std::cos(nu_exit) + glm::cross(nrm, apse) * std::sin(nu_exit);
                const sl::OrbitState home = sl::state_from_orbit(mu_sun, home_orbit, t_exit);
                lambert(home.position + exit_dir * soi_radius, r_arrive, tof - t_exit, v_depart);
                const sl::dvec3 next = v_depart - home.velocity;
                if (!std::isfinite(next.x + next.y + next.z))
                    break;
                excess = next;
                (void)vi;
            }
            if (std::isfinite(excess.x + excess.y + excess.z) && glm::length(excess) > 1.0)
            {
                const double v2 = glm::dot(excess, excess) - (soi_radius > 0.0 ? 2.0 * mu / soi_radius : 0.0);
                v_inf = std::sqrt(std::max(v2, 1.0));
                u = glm::normalize(excess);
                // The hyperbola's plane holds the asymptote, tilted as little as it can from the planet's own orbit plane.
                const sl::dvec3 across = normal - u * glm::dot(u, normal);
                if (glm::length(across) > 1e-6)
                    normal = glm::normalize(across);
            }
        }

        const double e = 1.0 + rp * v_inf * v_inf / mu;
        // The outgoing asymptote lies at true anomaly acos(-1/e) from periapsis, so periapsis is the asymptote turned back by that angle.
        const double alpha = std::acos(-1.0 / e);
        const sl::dvec3 apse = u * std::cos(alpha) - glm::cross(normal, u) * std::sin(alpha);
        const sl::dvec3 side = glm::cross(normal, apse);
        const double nu = true_anomaly_degrees * pi / 180.0;
        const double p = rp * (1.0 + e);
        const double r = p / (1.0 + e * std::cos(nu));
        const sl::dvec3 up = apse * std::cos(nu) + side * std::sin(nu);
        const sl::dvec3 prograde = side * std::cos(nu) - apse * std::sin(nu);
        const double h = std::sqrt(mu * p);
        const double vr = mu / h * e * std::sin(nu), vt = mu / h * (1.0 + e * std::cos(nu));
        b.position = pb.position + up * r;
        b.velocity = pb.velocity + up * vr + prograde * vt;
        b.orientation = orientation_from(prograde, up);
        b.angular_velocity = sl::dvec3(0.0);
        crashed_ = false;
        health_ = 1.0;
        rcs_fuel_ = spec_.rcs_propellant;
        shield_ = spec_.shield.capacity;
        pinned_ = resting_ = false;
        update_telemetry();
    }

    sl::dvec3 Ship::relative_velocity() const
    {
        const int soi = universe_.soi_body(body().position);
        const Body &planet = universe_.bodies[static_cast<std::size_t>(soi)];
        const sl::FlightBody &pb = *universe_.world.body(soi);
        const sl::dvec3 r = body().position - pb.position;
        return body().velocity - pb.velocity - glm::cross(pb.spin_axis * planet.spin, r);
    }

    void Ship::local_frame(sl::dvec3 &up, sl::dvec3 &east, sl::dvec3 &north) const
    {
        const int soi = universe_.soi_body(body().position);
        up = glm::normalize(body().position - universe_.world.body(soi)->position);
        east = glm::cross(universe_.world.body(soi)->spin_axis, up);
        if (glm::length(east) < 1e-9)
            east = sl::dvec3(1.0, 0.0, 0.0);
        east = glm::normalize(east);
        north = glm::cross(up, east);
    }

    sl::dvec3 Ship::target_direction(const Controls &c, bool &valid) const
    {
        valid = true;
        sl::dvec3 up, east, north;
        local_frame(up, east, north);
        const sl::dvec3 v = relative_velocity();
        const sl::dvec3 r = body().position - universe_.world.body(telemetry_.soi)->position;
        const sl::dvec3 h = glm::cross(r, body().velocity - universe_.world.body(telemetry_.soi)->velocity);
        const std::string &mode = c.sas;
        if (mode == "prograde" || mode == "retrograde")
        {
            if (glm::length(v) < 2.0)
            {
                valid = false;
                return up;
            }
            return mode == "prograde" ? glm::normalize(v) : -glm::normalize(v);
        }
        if (mode == "radial-out")
            return up;
        if (mode == "radial-in")
            return -up;
        if (mode == "normal" || mode == "antinormal")
        {
            if (glm::length(h) < 1e-6)
            {
                valid = false;
                return up;
            }
            return mode == "normal" ? glm::normalize(h) : -glm::normalize(h);
        }
        if (mode == "attitude")
        {
            const double pitch = c.target_pitch * pi / 180.0, heading = c.target_heading * pi / 180.0;
            return up * std::sin(pitch) + (north * std::cos(heading) + east * std::sin(heading)) * std::cos(pitch);
        }
        valid = false;
        return up;
    }

    void Ship::apply_attitude(const Controls &c, double dt)
    {
        if (rcs_dry())
            return;
        sl::FlightBody &b = body();
        const sl::dvec3 w = b.angular_velocity;
        sl::dvec3 desired(0.0);
        bool active = c.sas != "off";
        if (active)
        {
            bool valid = false;
            const sl::dvec3 target = target_direction(c, valid);
            if (valid)
            {
                const sl::dvec3 nose = b.direction_to_world({0.0, 0.0, -1.0});
                const sl::dvec3 axis = glm::cross(nose, target);
                const double s = glm::length(axis);
                const double angle = std::atan2(s, glm::dot(nose, target));
                if (s > 1e-9)
                    desired = b.direction_to_local(axis / s) * std::min(angle * 0.35, spec_.max_rate);
                desired.z = 0.0;
            }
        }
        const sl::dvec3 manual(c.pitch, c.yaw, c.roll);
        sl::dvec3 command(0.0);
        // The torque is held for the whole step, so the gain must stay under one step's worth or a slow frame makes the ship oscillate.
        const double gain = dt > 0.0 && spec_.torque_accel > 0.0 ? std::min(2.0, 0.8 / (spec_.torque_accel * dt)) : 2.0;
        for (int axis = 0; axis < 3; ++axis)
        {
            if (std::fabs(manual[axis]) > 0.02)
                command[axis] = std::clamp(manual[axis], -1.0, 1.0);
            else if (active)
                command[axis] = std::clamp((desired[axis] - w[axis]) * gain, -1.0, 1.0);
        }
        rcs_torque_ = command;
        if (explicit_rcs_)
            return;
        if (glm::length(command) > 0.0)
            universe_.world.apply_torque(body_, b.direction_to_world(command * spec_.torque_accel * b.inertia()));
    }

    void Ship::apply_flight_controls(const Controls &c)
    {
        sl::FlightBody &b = body();
        const sl::dvec3 w = b.angular_velocity;
        const bool damp = c.sas != "off";
        const double q = std::min(1.0, telemetry_.dynamic_pressure / 1500.0);
        auto command = [&](double stick, double rate, double gain)
        {
            return std::clamp(stick - (damp ? gain * rate * q : 0.0), -1.0, 1.0);
        };
        const double af = c.af_mode == "off" ? 0.0 : 1.0, af_lateral = c.af_mode == "on" ? 1.0 : 0.0;
        universe_.world.set_control(body_, "elevator", af * command(c.pitch + c.trim, w.x, spec_.damping.x));
        universe_.world.set_control(body_, "rudder", af_lateral * command(c.yaw, w.y, spec_.damping.y));
        universe_.world.set_control(body_, "aileron", af_lateral * command(c.roll, w.z, spec_.damping.z));
        universe_.world.set_control(body_, "flaps", std::clamp(c.flaps, 0.0, 1.0));

        // Nose-wheel steering follows the yaw input; the wheels roll along their own heading.
        const std::size_t first = b.gear.size() - spec_.wheels.size();
        for (std::size_t i = 0; i < spec_.wheels.size(); ++i)
        {
            const double angle = spec_.wheels[i].steer * std::clamp(c.yaw, -1.0, 1.0);
            b.gear[first + i].forward = {-std::sin(angle), 0.0, -std::cos(angle)};
        }
    }

    void Ship::apply_ground_controls(const Controls &c)
    {
        sl::FlightBody &b = body();
        const sl::FlightBody *planet = telemetry_.soi >= 0 ? universe_.world.body(telemetry_.soi) : nullptr;
        double forward_speed = 0.0;
        if (planet)
        {
            const sl::dvec3 ground_velocity = planet->velocity + glm::cross(planet->spin_axis * planet->spin_rate, b.position - planet->position);
            forward_speed = glm::dot(b.velocity - ground_velocity, b.direction_to_world({0.0, 0.0, -1.0}));
        }
        const double throttle = std::clamp(c.throttle, -1.0, 1.0);
        int driven = 0;
        for (const WheelSpec &w : spec_.wheels)
            driven += w.drive ? 1 : 0;
        // The drive fades as the vehicle nears its top speed in the direction it is being driven.
        const double taper = std::clamp(1.0 - forward_speed * (throttle < 0.0 ? -1.0 : 1.0) / std::max(spec_.ground_max_speed, 0.1), 0.0, 1.0);
        const double per_wheel = driven > 0 ? throttle * spec_.ground_force * taper / driven : 0.0;
        // Steering is eased off with speed so the vehicle does not roll over in a turn.
        const double lock = std::clamp(1.0 - 0.6 * std::fabs(forward_speed) / std::max(spec_.ground_max_speed, 0.1), 0.4, 1.0);
        const std::size_t first = b.gear.size() - spec_.wheels.size();
        for (std::size_t i = 0; i < spec_.wheels.size(); ++i)
        {
            const double angle = spec_.wheels[i].steer * lock * std::clamp(c.yaw, -1.0, 1.0);
            b.gear[first + i].forward = {-std::sin(angle), 0.0, -std::cos(angle)};
            b.gear[first + i].drive_force = spec_.wheels[i].drive ? per_wheel : 0.0;
        }
        for (sl::Thruster &t : b.thrusters)
            t.level = 0.0;
        // Parked on its brakes when idle and nearly still; otherwise only the pedal brakes.
        const bool parked = std::fabs(throttle) < 0.02 && std::fabs(forward_speed) < 1.0;
        universe_.world.set_brakes(body_, std::max(c.brake, parked ? 1.0 : 0.0));
    }

    void Ship::apply_controls(const Controls &input, double dt)
    {
        Controls c = input;
        rcs_torque_ = rcs_push_ = sl::dvec3(0.0);
        if (crashed_ || health_ <= 0.0)
            return;
        apply_rcs_mode(c);
        animate_mechanisms(c, dt);
        if (c.parachute > 0)
            deploy_parachute();
        else if (c.parachute < 0)
            cut_parachutes();
        if (spec_.ground())
        {
            apply_ground_controls(c);
            return;
        }
        if (c.stage)
            stage_once();
        sl::FlightBody &b = body();
        set_thruster_levels(c);
        if (flies_as_aircraft())
        {
            apply_flight_controls(c);
            hold_hover_attitude(c);
            return;
        }
        if (b.mode == sl::FlightBodyMode::dynamic)
        {
            apply_attitude(c, dt);
            apply_rcs(c);
            use_rcs_propellant(dt);
        }
    }

    double Ship::engine_value(const std::string &role, const std::string &what) const
    {
        const sl::FlightBody &b = body();
        if (b.thrusters.empty() || stage_first_thruster_.size() < static_cast<std::size_t>(active_stage_) + 2)
            return 0.0;
        double sum = 0.0, level = 0.0;
        int count = 0;
        for (int i = stage_first_thruster_[static_cast<std::size_t>(active_stage_)]; i < stage_first_thruster_[static_cast<std::size_t>(active_stage_) + 1]; ++i)
        {
            const sl::Thruster &t = b.thrusters[static_cast<std::size_t>(i)];
            const std::size_t k = static_cast<std::size_t>(i);
            if (thruster_rcs_[k])
                continue;
            std::string mine;
            if (thruster_pod_[k] >= 0)
                mine = t.position.x < 0.0 ? "pod-left" : "pod-right";
            else if (thruster_hover_[k])
                mine = t.position.z < 0.0 ? "hover-front" : "hover-aft";
            else
                mine = t.position.x < 0.0 ? "main-left" : "main-right";
            if (mine != role)
                continue;
            const bool fed = t.tank < 0 || b.tanks[static_cast<std::size_t>(t.tank)].mass > 0.0;
            const double lvl = fed ? t.level : 0.0;
            level += lvl;
            ++count;
            sum += (what == "thrust" || what == "accel") ? lvl * t.max_thrust : t.isp > 0.0 ? lvl * t.max_thrust / (t.isp * g0) : 0.0;
        }
        if (what == "accel")
            return b.mass() > 0.0 ? sum / b.mass() : 0.0;
        return what == "level" ? (count ? level / count : 0.0) : sum;
    }

    bool Ship::rcs_dry() const
    {
        return spec_.rcs_propellant > 0.0 && rcs_fuel_ <= 0.0;
    }

    void Ship::use_rcs_propellant(double dt)
    {
        rcs_flow_ = 0.0;
        if (spec_.rcs_propellant <= 0.0 || dt <= 0.0)
            return;
        const double activity = std::min(1.0, 0.3 * (std::fabs(rcs_torque_.x) + std::fabs(rcs_torque_.y) + std::fabs(rcs_torque_.z))) + glm::length(rcs_push_);
        rcs_flow_ = activity * std::max(spec_.rcs_thrust, 1000.0) / (spec_.rcs_isp * g0);
        rcs_fuel_ = std::max(0.0, rcs_fuel_ - rcs_flow_ * dt);
    }

    void Ship::apply_rcs_mode(Controls &c) const
    {
        if (spec_.rcs_modes)
        {
            if (c.rcs_mode == "off")
                c.pitch = c.yaw = c.roll = c.rcs_x = c.rcs_y = c.rcs_z = 0.0;
            else if (c.rcs_mode == "lin")
            {
                // The rotation keys push instead: W/S forward and back, A/D left and right, Q/E up and down.
                c.rcs_z = std::clamp(c.rcs_z - c.pitch, -1.0, 1.0);
                c.rcs_x = std::clamp(c.rcs_x - c.yaw, -1.0, 1.0);
                c.rcs_y = std::clamp(c.rcs_y + c.roll, -1.0, 1.0);
                c.pitch = c.yaw = c.roll = 0.0;
            }
        }
    }

    void Ship::animate_mechanisms(const Controls &c, double dt)
    {
        for (int side = 0; side < 2; ++side)
        {
            const double target = std::clamp(side == 0 ? c.pod_tilt_left : c.pod_tilt_right, 0.0, 1.0) * 3.14159265358979;
            const double step = spec_.pod_turn_rate * dt;
            pod_angle_[side] += std::clamp(target - pod_angle_[side], -step, step);
        }
        if (anim_pos_.size() != spec_.animation_controls.size())
        {
            anim_pos_.clear();
            for (const ShipSpec::AnimationControl &a : spec_.animation_controls)
                anim_pos_.push_back(a.initial);
        }
        for (std::size_t i = 0; i < anim_pos_.size(); ++i)
        {
            const double target = i < c.anim.size() ? std::clamp(c.anim[i], 0.0, 1.0) : anim_pos_[i];
            const double step = dt / spec_.animation_controls[i].duration;
            anim_pos_[i] = std::clamp(anim_pos_[i] + std::clamp(target - anim_pos_[i], -step, step), 0.0, 1.0);
        }
        sl::FlightBody &b = body();
        if (spec_.retractable_gear)
        {
            const double target = c.gear > 0.5 ? 1.0 : 0.0;
            const double step = dt / spec_.gear_time;
            gear_position_ = std::clamp(gear_position_ + std::clamp(target - gear_position_, -step, step), 0.0, 1.0);
            const bool locked = gear_position_ >= 0.999;
            for (sl::GearContact &g : b.gear)
                g.enabled = locked;
        }
    }

    void Ship::set_thruster_levels(const Controls &c)
    {
        sl::FlightBody &b = body();
        const double throttle = std::clamp(c.throttle, 0.0, 1.0);
        for (std::size_t i = 0; i < b.thrusters.size(); ++i)
            b.thrusters[i].level = 0.0;
        for (int i = stage_first_thruster_[static_cast<std::size_t>(active_stage_)];
             i < stage_first_thruster_[static_cast<std::size_t>(active_stage_) + 1]; ++i)
        {
            const int pod = thruster_pod_[static_cast<std::size_t>(i)];
            sl::Thruster &t = b.thrusters[static_cast<std::size_t>(i)];
            if (pod >= 0)
            {
                const double a = pod_angle_[pod];
                t.direction = {0.0, std::sin(a), std::cos(a)};
                t.level = std::clamp(pod == 0 ? c.pod_left : c.pod_right, 0.0, 1.0);
            }
            else
            {
                const bool main_engine = !thruster_rcs_[static_cast<std::size_t>(i)] && !thruster_hover_[static_cast<std::size_t>(i)];
                t.level = thruster_rcs_[static_cast<std::size_t>(i)] ? 0.0 : thruster_hover_[static_cast<std::size_t>(i)] ? std::clamp(c.hover, 0.0, 1.0) : throttle;
                const auto &engines = spec_.stages[static_cast<std::size_t>(active_stage_)].engines;
                const std::size_t local = static_cast<std::size_t>(i - stage_first_thruster_[static_cast<std::size_t>(active_stage_)]);
                if (main_engine && local < engines.size())
                {
                    // The gimbal swings the thrust line about the ship's X (pitch) and Y (yaw) axes.
                    const double range = glm::radians(spec_.gimbal_range);
                    const sl::dquat swing = glm::angleAxis(std::clamp(c.gimbal_yaw, -1.0, 1.0) * range, sl::dvec3(0.0, 1.0, 0.0)) * glm::angleAxis(std::clamp(c.gimbal_pitch, -1.0, 1.0) * range, sl::dvec3(1.0, 0.0, 0.0));
                    t.direction = glm::normalize(swing * engines[local].direction);
                }
            }
        }
        if (flies_as_aircraft())
        {
            // A parked aircraft holds its brakes; once rolling, only the pedal brakes.
            const bool parked = throttle < 0.02 && telemetry_.landed && telemetry_.surface_speed < 2.0;
            universe_.world.set_brakes(body_, std::max(c.brake, parked ? 1.0 : 0.0));
        }
        else
            universe_.world.set_brakes(body_, throttle < 0.02 ? 1.0 : 0.0);
    }

    // On hover thrust there is no airflow over the surfaces, so the RCS thrusters hold attitude.
    void Ship::hold_hover_attitude(const Controls &c)
    {
        sl::FlightBody &b = body();
        if (c.hover > 0.02 && b.mode == sl::FlightBodyMode::dynamic && c.sas != "off")
        {
            sl::dvec3 up, east, north;
            local_frame(up, east, north);
            const sl::dvec3 ub = b.direction_to_local(up);
            const sl::dvec3 w = b.angular_velocity;
            const sl::dvec3 desired(std::clamp(ub.z * 1.5, -0.6, 0.6), 0.0, std::clamp(-ub.x * 1.5, -0.6, 0.6));
            sl::dvec3 command(0.0);
            command.x = std::clamp((desired.x - w.x) * 3.0, -1.0, 1.0);
            command.z = std::clamp((desired.z - w.z) * 3.0, -1.0, 1.0);
            command.y = std::clamp(-w.y * 3.0, -1.0, 1.0);
            rcs_torque_ = command;
            universe_.world.apply_torque(body_, b.direction_to_world(command * spec_.torque_accel * b.inertia()));
        }
    }

    void Ship::apply_rcs(const Controls &c)
    {
        if (rcs_dry())
            return;
        sl::FlightBody &b = body();
        rcs_push_ = sl::dvec3(0.0);
        if (explicit_rcs_)
        {
            // The thrusters themselves make the force and torque; each fires in proportion to its strongest group.
            const double lin[6] = {-c.rcs_x, c.rcs_x, c.rcs_y, -c.rcs_y, c.rcs_z, -c.rcs_z};
            const double rot[6] = {rcs_torque_.x, -rcs_torque_.x, rcs_torque_.y, -rcs_torque_.y, rcs_torque_.z, -rcs_torque_.z};
            for (int i = stage_first_thruster_[static_cast<std::size_t>(active_stage_)];
                 i < stage_first_thruster_[static_cast<std::size_t>(active_stage_) + 1]; ++i)
            {
                const unsigned mask = thruster_rcs_[static_cast<std::size_t>(i)];
                double level = 0.0;
                for (int k = 0; k < 6; ++k)
                {
                    if (mask & (1u << k))
                        level = std::max(level, lin[k]);
                    if (mask & (1u << (k + 6)))
                        level = std::max(level, rot[k]);
                }
                b.thrusters[static_cast<std::size_t>(i)].level = std::clamp(level, 0.0, 1.0);
            }
            rcs_torque_ = sl::dvec3(0.0);
        }
        else if (spec_.rcs_thrust > 0.0)
        {
            const sl::dvec3 push(c.rcs_x, c.rcs_y, -c.rcs_z);
            rcs_push_ = glm::length(push) > 0.0 ? glm::normalize(push) : push;
            if (glm::length(push) > 0.0)
                universe_.world.apply_force(body_, b.direction_to_world(glm::normalize(push) * spec_.rcs_thrust));
        }
    }

    bool Ship::stage_once()
    {
        if (active_stage_ >= stage_count() - 1)
            return false;
        drop_stage(active_stage_);
        ++active_stage_;
        recentre();
        refresh_mass_properties();
        return true;
    }

    void Ship::drop_stage(int index)
    {
        sl::FlightBody &b = body();
        const StageSpec &st = spec_.stages[static_cast<std::size_t>(index)];
        const double leftover = b.tanks[static_cast<std::size_t>(index)].mass;
        const double total = b.mass();

        const int id = universe_.world.create_body(name + " " + st.name);
        if (id >= 0)
        {
            sl::FlightBody &d = *universe_.world.body(id);
            const sl::dvec3 tail = b.direction_to_world(glm::normalize(st.separation_dir));
            d.dry_mass = std::max(st.dry_mass + leftover, 1.0);
            const double transverse = d.dry_mass * (st.length * st.length / 12.0 + st.radius * st.radius / 4.0);
            d.dry_inertia = {transverse, transverse, d.dry_mass * st.radius * st.radius / 2.0};
            const sl::dvec3 arm = b.direction_to_world({0.0, 0.0, st.z - com_.z});
            d.position = b.position + arm;
            d.velocity = b.velocity + glm::cross(b.rotation() * b.angular_velocity, arm) + tail * st.separation_speed;
            d.orientation = b.orientation;
            d.angular_velocity = b.angular_velocity;
            d.drag_area = 3.14159 * st.radius * st.radius;
            Debris debris;
            debris.body = id;
            debris.stage = index;
            debris.centre_z = st.z;
            debris_.push_back(debris);
            b.velocity -= tail * st.separation_speed * d.dry_mass / std::max(total - d.dry_mass, 1.0);
        }

        for (int i = stage_first_thruster_[static_cast<std::size_t>(index)]; i < stage_first_thruster_[static_cast<std::size_t>(index) + 1]; ++i)
        {
            b.thrusters[static_cast<std::size_t>(i)].max_thrust = 0.0;
            b.thrusters[static_cast<std::size_t>(i)].level = 0.0;
        }
        b.tanks[static_cast<std::size_t>(index)] = {0.0, 0.0};
        if (systems_)
            systems_->drop_stage(index);
        // Gear belongs to stages in order, and stages leave in order, so the leading contacts are this stage's.
        b.gear.erase(b.gear.begin(), b.gear.begin() + static_cast<std::ptrdiff_t>(std::min(st.gear.size(), b.gear.size())));
    }

    sl::dvec3 Ship::camera_focus() const
    {
        sl::dvec3 target = spec_.camera_target;
        if (active_stage_ <= 0 || active_stage_ >= static_cast<int>(spec_.stages.size()))
            return target;
        const auto middle = [&](std::size_t first)
        {
            double lo = 1.0e30, hi = -1.0e30;
            for (std::size_t s = first; s < spec_.stages.size(); ++s)
            {
                lo = std::min(lo, spec_.stages[s].z - spec_.stages[s].length * 0.5);
                hi = std::max(hi, spec_.stages[s].z + spec_.stages[s].length * 0.5);
            }
            return 0.5 * (lo + hi);
        };
        target.z += middle(static_cast<std::size_t>(active_stage_)) - middle(0);
        return target;
    }

    void Ship::recentre()
    {
        sl::FlightBody &b = body();
        double mass = 0.0, moment = 0.0;
        for (std::size_t s = static_cast<std::size_t>(active_stage_); s < spec_.stages.size(); ++s)
        {
            const double m = spec_.stages[s].dry_mass + (s < b.tanks.size() ? b.tanks[s].mass : 0.0);
            mass += m;
            moment += m * spec_.stages[s].z;
        }
        if (mass <= 0.0)
            return;
        const sl::dvec3 delta = sl::dvec3(0.0, 0.0, moment / mass) - com_;
        if (glm::length(delta) < 1.0e-6)
            return;
        const sl::dvec3 arm = b.direction_to_world(delta);
        b.velocity += glm::cross(b.rotation() * b.angular_velocity, arm);
        b.position += arm;
        for (sl::Thruster &t : b.thrusters)
            t.position -= delta;
        for (sl::GearContact &g : b.gear)
            g.position -= delta;
        for (sl::LiftingSurface &sf : b.surfaces)
            sf.position -= delta;
        com_ += delta;
    }

    void Ship::go_dynamic()
    {
        if (on_rails())
            universe_.world.release_rails(body_);
    }

    bool Ship::on_rails() const { return body().mode == sl::FlightBodyMode::rails; }

    bool Ship::flies_as_aircraft() const
    {
        if (!spec_.aircraft())
            return false;
        if (!spec_.hybrid)
            return true;
        const int soi = telemetry_.soi;
        if (soi < 0 || soi >= static_cast<int>(universe_.bodies.size()))
            return false;
        const Body &planet = universe_.bodies[static_cast<std::size_t>(soi)];
        return planet.atmosphere_density >= 0.1225 && telemetry_.altitude < atmosphere_top(planet);
    }

    bool Ship::go_rails()
    {
        if (on_rails() || crashed_)
            return on_rails();
        sl::FlightBody &b = body();
        b.angular_velocity = sl::dvec3(0.0);
        return universe_.world.set_rails(body_, universe_.soi_body(b.position));
    }

    bool Ship::can_rails(const Controls &c) const { return rails_blocker(c).empty(); }

    std::string Ship::rails_blocker(const Controls &c) const
    {
        if (crashed_)
            return "crashed";
        if (telemetry_.landed)
            return "landed";
        // A throttle left up with empty tanks does nothing, so it must not hold the clock back.
        if ((c.throttle > 0.0 || c.hover > 0.0) && telemetry_.thrust > 0.0)
            return "engines are firing";
        const Body &planet = universe_.bodies[static_cast<std::size_t>(telemetry_.soi)];
        const double top = atmosphere_top(planet);
        if (telemetry_.altitude < top + 1000.0)
            return "inside the atmosphere";
        if (telemetry_.bound ? telemetry_.periapsis <= top : (telemetry_.vertical_speed <= 0.0 && telemetry_.periapsis <= top))
            return "orbit dips into the atmosphere";
        return {};
    }

    bool Ship::deploy_parachute()
    {
        if (chutes_.size() != spec_.parachutes.size())
            chutes_.assign(spec_.parachutes.size(), ChuteRuntime());
        for (ChuteRuntime &c : chutes_)
            if (c.state == ChuteState::stowed)
            {
                c.state = ChuteState::reefed;
                c.time = 0.0;
                std::fprintf(stderr, "[kosmos] %s: parachute deployed\n", name.c_str());
                return true;
            }
        return false;
    }

    void Ship::cut_parachutes()
    {
        for (ChuteRuntime &c : chutes_)
            if (c.state != ChuteState::stowed && c.state != ChuteState::gone)
                c.state = ChuteState::gone;
    }

    void Ship::step_parachutes(double dt, const sl::dvec3 &air, double rho)
    {
        if (spec_.parachutes.empty())
            return;
        if (chutes_.size() != spec_.parachutes.size())
            chutes_.assign(spec_.parachutes.size(), ChuteRuntime());
        sl::FlightBody &b = body();
        const double speed = glm::length(air);
        const double q = 0.5 * rho * speed * speed;
        for (std::size_t i = 0; i < chutes_.size(); ++i)
        {
            const ParachuteSpec &spec = spec_.parachutes[i];
            ChuteRuntime &c = chutes_[i];
            if (c.state == ChuteState::stowed && spec.auto_altitude > 0.0 && telemetry_.altitude < spec.auto_altitude &&
                (spec.auto_speed <= 0.0 || speed < spec.auto_speed))
                deploy_parachute();
            if (c.state == ChuteState::stowed || c.state == ChuteState::gone)
                continue;
            if (q > spec.max_q * (c.state == ChuteState::reefed ? 1.5 : 1.0))
            {
                c.state = ChuteState::gone;
                std::fprintf(stderr, "[kosmos] %s: %s torn away at %.0f Pa\n", name.c_str(), spec.name.c_str(), q);
                continue;
            }
            if (spec.cut_on_landing && (telemetry_.landed || telemetry_.altitude < 1.0))
            {
                c.state = ChuteState::gone;
                continue;
            }
            c.time += dt;
            if (c.state == ChuteState::reefed && c.time >= spec.reef_time)
            {
                c.state = ChuteState::opening;
                c.fill = 0.0;
            }
            if (c.state == ChuteState::opening)
            {
                c.fill += dt / std::max(spec.inflation_time, 0.1);
                if (c.fill >= 1.0)
                    c.state = ChuteState::open;
            }
            const double area = chute_drag_area(spec, c);
            if (area <= 0.0 || speed < 0.5)
                continue;
            const sl::dvec3 pull = -air / speed * (q * area);
            universe_.world.apply_force_at(body_, pull, b.position + b.direction_to_world(spec.position - com_));
        }
    }

    void Ship::step_reentry(double dt)
    {
        const bool wanted = spec_.thermal.enabled || spec_.hypersonic.enabled || !spec_.parachutes.empty();
        if (!wanted || dt <= 0.0 || crashed_ || pinned_ || on_rails() || telemetry_.soi < 0)
            return;
        sl::FlightBody &b = body();
        if (b.mode != sl::FlightBodyMode::dynamic)
            return;
        const double rho = universe_.world.air_density_at(b.position);
        const sl::dvec3 air = b.velocity - universe_.world.air_velocity_at(b.position);
        const double speed = glm::length(air);
        const sl::dvec3 local = b.direction_to_local(air);

        if (spec_.hypersonic.enabled)
        {
            if (!aero_scaled_)
            {
                aero_scaled_ = true;
                base_drag_area_ = b.drag_area;
                base_surface_area_.clear();
                for (const sl::LiftingSurface &s : b.surfaces)
                    base_surface_area_.push_back(s.area);
            }
            const AeroResult aero = hypersonic_aero(spec_.hypersonic, local, rho, b.angular_velocity);
            b.drag_area = base_drag_area_ * (1.0 - aero.blend);
            for (std::size_t i = 0; i < b.surfaces.size() && i < base_surface_area_.size(); ++i)
                b.surfaces[i].area = base_surface_area_[i] * (1.0 - aero.blend);
            if (aero.blend > 0.0)
            {
                universe_.world.apply_force(body_, b.direction_to_world(aero.force));
                universe_.world.apply_torque(body_, b.direction_to_world(aero.torque));
            }
        }
        if (spec_.thermal.enabled)
        {
            const double windward = speed > 1.0 ? glm::dot(spec_.thermal.shield_axis, local / speed) : 1.0;
            const double rate = step_thermal(spec_.thermal, thermal_, rho, speed, windward, 288.0, dt);
            if (rate > 0.0)
                damage(rate * dt);
        }
        step_parachutes(dt, air, rho);
    }

    void Ship::update_ground_track()
    {
        const Telemetry &t = telemetry_;
        const double now = universe_.time();
        if (t.soi == track_.soi && std::fabs(now - track_.stamp) < 2.0)
            return;
        track_.stamp = now;
        track_.soi = t.soi;
        track_.count = 0;
        track_.impact = false;
        const Body &planet = universe_.bodies[static_cast<std::size_t>(t.soi)];
        const sl::FlightBody &pb = *universe_.world.body(t.soi);
        const sl::FlightBody &b = body();
        if (planet.gm <= 0.0)
            return;
        sl::dvec3 r = b.position - pb.position, v = b.velocity - pb.velocity;
        const double mu = planet.gm;
        const double rmag0 = glm::length(r);
        const double energy = 0.5 * glm::dot(v, v) - mu / rmag0;
        // One orbit when bound, otherwise a stretch of the escape trajectory.
        double span = energy < 0.0 ? 2.0 * pi * std::sqrt(std::pow(-mu / (2.0 * energy), 3.0) / mu) : 2.0 * 3600.0;
        span = std::min(span, 40.0 * 86400.0);
        const double step = std::min(span / 2000.0, 0.02 * std::sqrt(rmag0 * rmag0 * rmag0 / mu));
        const sl::dquat tilt_inv = glm::inverse(planet.axis_tilt);
        const double angle0 = universe_.rotation_angle(t.soi);
        const auto accel = [&](const sl::dvec3 &p) { return p * (-mu / std::pow(glm::length(p), 3.0)); };
        const auto record = [&](const sl::dvec3 &p, double dt, double &lat, double &lon)
        {
            const sl::dvec3 local = glm::inverse(glm::angleAxis(angle0 + planet.spin * dt, sl::dvec3(0.0, 1.0, 0.0))) * (tilt_inv * p);
            lat = std::asin(std::clamp(local.y / glm::length(local), -1.0, 1.0)) * 180.0 / pi;
            lon = std::atan2(-local.z, local.x) * 180.0 / pi;
        };
        record(r, 0.0, track_.lat[0], track_.lon[0]);
        track_.count = 1;
        const double sample = span / track_points;
        double elapsed = 0.0, next = sample;
        const long max_steps = 4000;
        for (long n = 0; n < max_steps && track_.count <= track_points; ++n)
        {
            const sl::dvec3 k1v = accel(r), k1r = v;
            const sl::dvec3 k2v = accel(r + k1r * (step * 0.5)), k2r = v + k1v * (step * 0.5);
            const sl::dvec3 k3v = accel(r + k2r * (step * 0.5)), k3r = v + k2v * (step * 0.5);
            const sl::dvec3 k4v = accel(r + k3r * step), k4r = v + k3v * step;
            r += (k1r + 2.0 * k2r + 2.0 * k3r + k4r) * (step / 6.0);
            v += (k1v + 2.0 * k2v + 2.0 * k3v + k4v) * (step / 6.0);
            elapsed += step;
            if (glm::length(r) <= planet.radius)
            {
                track_.impact = true;
                track_.impact_time = elapsed;
                record(r, elapsed, track_.impact_lat, track_.impact_lon);
                record(r, elapsed, track_.lat[track_.count], track_.lon[track_.count]);
                ++track_.count;
                break;
            }
            if (elapsed >= next)
            {
                record(r, elapsed, track_.lat[track_.count], track_.lon[track_.count]);
                ++track_.count;
                next += sample;
            }
        }
    }

    void Ship::update_telemetry()
    {
        Telemetry &t = telemetry_;
        sl::FlightBody &b = body();
        t.soi = universe_.soi_body(b.position);
        const Body &planet = universe_.bodies[static_cast<std::size_t>(t.soi)];
        const sl::FlightBody &pb = *universe_.world.body(t.soi);

        double lowest = 0.0;
        if (spec_.aircraft() || spec_.horizontal)
        {
            for (const sl::GearContact &g : b.gear)
                lowest = std::max(lowest, -g.position.y);
        }
        else
        {
            for (std::size_t s = static_cast<std::size_t>(active_stage_); s < spec_.stages.size(); ++s)
                lowest = std::max(lowest, spec_.stages[s].z + spec_.stages[s].length * 0.5);
            for (const sl::GearContact &g : b.gear)
                lowest = std::max(lowest, g.position.z);
        }
        const double origin_altitude = universe_.world.altitude_of(b.position);
        t.altitude = origin_altitude - lowest;
        t.landed = false;
        for (const sl::GearContact &g : b.gear)
            t.landed = t.landed || g.compression > 0.0;

        const sl::dvec3 r = b.position - pb.position;
        const sl::dvec3 vrel = relative_velocity();
        const sl::dvec3 up = glm::normalize(r);
        t.surface_speed = glm::length(vrel);
        {
            const sl::dvec3 air = b.velocity - universe_.world.air_velocity_at(b.position);
            const sl::dvec3 local = b.direction_to_local(air);
            const double rho = universe_.world.air_density_at(b.position);
            t.dynamic_pressure = 0.5 * rho * glm::dot(air, air);
            const double speed = glm::length(local);
            t.alpha = t.sideslip = 0.0;
            if (speed > 1.0)
            {
                t.alpha = std::atan2(local.y, -local.z) * 180.0 / pi * -1.0;
                t.sideslip = std::asin(std::clamp(local.x / speed, -1.0, 1.0)) * 180.0 / pi;
            }
        }
        t.orbital_speed = glm::length(b.velocity - pb.velocity);
        t.vertical_speed = glm::dot(vrel, up);
        t.flight_path = t.surface_speed > 1.0 ? std::asin(std::clamp(t.vertical_speed / t.surface_speed, -1.0, 1.0)) * 180.0 / pi : 0.0;

        {
            const sl::dvec3 local_pos = glm::inverse(universe_.orientation(t.soi)) * r;
            t.latitude = std::asin(std::clamp(local_pos.y / glm::length(local_pos), -1.0, 1.0)) * 180.0 / pi;
            t.longitude = std::atan2(-local_pos.z, local_pos.x) * 180.0 / pi;
            t.body_radius = planet.radius;
        }
        update_ground_track();

        sl::OrbitElements el;
        t.bound = false;
        t.apoapsis = t.periapsis = t.period = t.time_to_apoapsis = t.time_to_periapsis = 0.0;
        if (t.soi >= 0 && universe_.world.orbit_of(body_, t.soi, el))
        {
            t.eccentricity = el.eccentricity;
            t.inclination = el.inclination * 180.0 / pi;
            t.semi_major_axis = el.semi_major_axis;
            t.raan = el.raan * 180.0 / pi;
            t.arg_periapsis = el.arg_periapsis * 180.0 / pi;
            {
                // True anomaly from the eccentric (or hyperbolic) state vectors.
                const sl::dvec3 vv = b.velocity - pb.velocity;
                const sl::dvec3 e = (glm::dot(vv, vv) - planet.gm / glm::length(r)) / planet.gm * r - glm::dot(r, vv) / planet.gm * vv;
                double ta = 0.0;
                if (glm::length(e) > 1e-9)
                {
                    ta = std::acos(std::clamp(glm::dot(e, r) / (glm::length(e) * glm::length(r)), -1.0, 1.0));
                    if (glm::dot(r, vv) < 0.0)
                        ta = 2.0 * pi - ta;
                }
                t.true_anomaly = ta * 180.0 / pi;
            }
            t.bound = el.eccentricity < 1.0;
            t.periapsis = el.periapsis() - planet.radius;
            const double n = el.mean_motion(planet.gm);
            double m = std::fmod(el.mean_anomaly, 2.0 * pi);
            if (m < 0.0 && t.bound)
                m += 2.0 * pi;
            if (t.bound)
            {
                t.apoapsis = el.apoapsis() - planet.radius;
                t.period = el.period(planet.gm);
                t.time_to_apoapsis = (m < pi ? pi - m : 3.0 * pi - m) / n;
                t.time_to_periapsis = (2.0 * pi - m) / n;
            }
            else
                t.time_to_periapsis = m < 0.0 ? -m / n : 0.0;
        }

        const sl::dvec3 nose = b.direction_to_world({0.0, 0.0, -1.0});
        sl::dvec3 u, east, north;
        local_frame(u, east, north);
        t.pitch = std::asin(std::clamp(glm::dot(nose, u), -1.0, 1.0)) * 180.0 / pi;
        {
            const sl::dvec3 right = b.direction_to_world({1.0, 0.0, 0.0}), roof = b.direction_to_world({0.0, 1.0, 0.0});
            t.roll = std::atan2(-glm::dot(right, u), glm::dot(roof, u)) * 180.0 / pi;
        }
        const sl::dvec3 flat = nose - u * glm::dot(nose, u);
        t.heading = glm::length(flat) > 1e-6 ? std::fmod(std::atan2(glm::dot(flat, east), glm::dot(flat, north)) * 180.0 / pi + 360.0, 360.0) : 0.0;

        t.mass = b.mass();
        t.thrust = 0.0;
        for (const sl::Thruster &th : b.thrusters)
            if (th.level > 0.0 && th.tank >= 0 && b.tanks[static_cast<std::size_t>(th.tank)].mass > 0.0)
                t.thrust += th.level * th.max_thrust;
        const double g_local = planet.gm / std::max(glm::dot(r, r), 1.0);
        t.max_thrust = 0.0;
        if (!b.thrusters.empty())
            for (int i = stage_first_thruster_[static_cast<std::size_t>(active_stage_)]; i < stage_first_thruster_[static_cast<std::size_t>(active_stage_) + 1]; ++i)
                t.max_thrust += b.thrusters[static_cast<std::size_t>(i)].max_thrust;
        t.twr = b.mass() > 0.0 ? t.max_thrust / (b.mass() * g_local) : 0.0;
        t.acceleration_g = t.thrust / (b.mass() * g0);
        t.stage_fuel = b.tanks[static_cast<std::size_t>(active_stage_)].mass;
        fuel_flow_ = 0.0;
        for (const sl::Thruster &th : b.thrusters)
            if (th.level > 0.0 && th.tank == active_stage_ && th.isp > 0.0 && b.tanks[static_cast<std::size_t>(th.tank)].mass > 0.0)
                fuel_flow_ += th.level * th.max_thrust / (th.isp * g0);
        t.total_fuel = 0.0;
        for (const sl::PropellantTank &tank : b.tanks)
            t.total_fuel += tank.mass;
    }

    namespace
    {
        // The turn of a planet's ground about its pole since time zero, the same one the physics world applies to the terrain.
        sl::dquat ground_spin(const Universe &universe, int id)
        {
            const sl::FlightBody *planet = universe.world.body(id);
            return glm::angleAxis(planet->spin_phase + planet->spin_rate * universe.time(), glm::normalize(planet->spin_axis));
        }
    }

    void Ship::pin_to_ground()
    {
        sl::FlightBody &b = body();
        const int soi = telemetry_.soi;
        if (soi < 0)
            return;
        const sl::dquat inverse = glm::inverse(ground_spin(universe_, soi));
        pin_body_ = soi;
        pin_position_ = inverse * (b.position - universe_.position(soi));
        pin_attitude_ = inverse * b.orientation;
        pinned_ = true;
        b.mode = sl::FlightBodyMode::kinematic;
        b.angular_velocity = sl::dvec3(0.0);
        for (sl::Thruster &t : b.thrusters)
            t.level = 0.0;
        hold_pinned();
    }

    void Ship::hold_pinned()
    {
        if (!pinned_ || pin_body_ < 0)
            return;
        sl::FlightBody &b = body();
        const sl::dquat spin = ground_spin(universe_, pin_body_);
        const sl::FlightBody *planet = universe_.world.body(pin_body_);
        b.position = planet->position + spin * pin_position_;
        b.orientation = glm::normalize(spin * pin_attitude_);
        // Carried along with the ground: planet velocity plus its spin about the pole.
        b.velocity = planet->velocity + glm::cross(planet->spin_axis * planet->spin_rate, b.position - planet->position);
        b.angular_velocity = sl::dvec3(0.0);
    }

    void Ship::after_step(double dt)
    {
        update_telemetry();
        step_systems(dt);

        // A building is part of the ground: held there from the first step, with no flight.
        if (spec_.building())
        {
            if (!pinned_)
                pin_to_ground();
            else
                hold_pinned();
            return;
        }
        step_reentry(dt);
        resolve_landing();
        expire_debris(dt);
    }

    void Ship::step_systems(double dt)
    {
        sl::FlightBody &b = body();
        if (systems_ && dt > 0.0)
        {
            mission_time_ += dt;
            SystemsContext ctx;
            ctx.mission_time = mission_time_;
            int star = -1;
            for (std::size_t i = 0; i < universe_.bodies.size(); ++i)
                if (universe_.bodies[i].kind == BodyKind::star)
                {
                    star = static_cast<int>(i);
                    break;
                }
            if (star >= 0)
            {
                const sl::dvec3 to_star = universe_.position(star) - b.position;
                const double distance = std::max(glm::length(to_star), 1.0);
                const sl::dvec3 dir = to_star / distance;
                ctx.sun_dir = b.direction_to_local(dir);
                ctx.sun_flux = std::pow(149597870700.0 / distance, 2.0);
                // Eclipse: the ray to the star passes through any other body.
                for (std::size_t i = 0; i < universe_.bodies.size(); ++i)
                {
                    if (static_cast<int>(i) == star)
                        continue;
                    const sl::dvec3 to_body = universe_.position(static_cast<int>(i)) - b.position;
                    const double along = glm::dot(to_body, dir);
                    if (along <= 0.0 || along >= distance)
                        continue;
                    if (glm::length(to_body - dir * along) < universe_.bodies[i].radius)
                    {
                        ctx.sun_flux = 0.0;
                        break;
                    }
                }
            }
            systems_->step(dt, ctx);
            if (systems_->crew_lost() && !crashed_)
            {
                std::fprintf(stderr, "[kosmos] %s: crew lost\n", name.c_str());
                crashed_ = true;
                if (telemetry_.altitude < 50.0)
                    pin_to_ground();
                else
                {
                    b.mode = sl::FlightBodyMode::kinematic;
                    b.velocity = universe_.world.body(telemetry_.soi)->velocity;
                    b.angular_velocity = sl::dvec3(0.0);
                    for (sl::Thruster &t : b.thrusters)
                        t.level = 0.0;
                }
            }
        }
    }

    void Ship::resolve_landing()
    {
        sl::FlightBody &b = body();

        // Ships built for orbit have no landing gear: a gentle touchdown leaves them resting on the surface, a hard one wrecks them.
        double burning = 0.0;
        for (const sl::Thruster &t : b.thrusters)
            burning = std::max(burning, t.level);
        const bool touching = spec_.orbital_only && !crashed_ && burning < 0.05 && b.mode == sl::FlightBodyMode::dynamic && (telemetry_.landed || telemetry_.altitude <= 0.3);
        const bool hard_landing = touching && approach_speed_ > spec_.crash_speed;
        if (hard_landing)
            std::fprintf(stderr, "[kosmos] %s: hit the ground at %.1f m/s\n", name.c_str(), approach_speed_);
        else if (touching)
        {
            const double lift = -telemetry_.altitude;
            if (lift > 0.0)
                b.position += glm::normalize(b.position - universe_.position(telemetry_.soi)) * lift;
            pin_to_ground();
            resting_ = true;
        }
        else if (resting_ && !crashed_)
        {
            if (burning > 0.05)
            {
                pinned_ = false;
                b.mode = sl::FlightBodyMode::dynamic;
                resting_ = false;
            }
        }
        if (!touching || hard_landing)
            approach_speed_ = telemetry_.surface_speed;
        if (hard_landing || (!crashed_ && b.mode == sl::FlightBodyMode::dynamic && !telemetry_.landed))
        {
            double tail = 0.0;
            for (std::size_t s = static_cast<std::size_t>(active_stage_); s < spec_.stages.size(); ++s)
                tail = std::max(tail, spec_.stages[s].z + spec_.stages[s].length * 0.5);
            double gear_z = 0.0;
            for (const sl::GearContact &g : b.gear)
                gear_z = std::max(gear_z, g.position.z);
            if (spec_.aircraft() || spec_.horizontal)
            {
                tail = 0.0;
                for (const sl::GearContact &g : b.gear)
                    tail = std::max(tail, -g.position.y);
                gear_z = tail;
            }
            if (hard_landing || telemetry_.altitude + std::max(tail, gear_z) < 0.4 * std::max(tail, gear_z))
            {
                crashed_ = true;
                // The wreck rests on the surface rather than sunk into it.
                const double origin_alt = universe_.world.altitude_of(b.position);
                if (origin_alt < 1.5)
                    b.position += glm::normalize(b.position - universe_.position(telemetry_.soi)) * (1.5 - origin_alt);
                pin_to_ground();
            }
        }
        else if (pinned_)
            hold_pinned();
        else if (crashed_ && b.mode == sl::FlightBodyMode::kinematic)
            b.velocity = universe_.world.body(telemetry_.soi)->velocity;
    }

    void Ship::expire_debris(double dt)
    {
        for (std::size_t i = 0; i < debris_.size();)
        {
            debris_[i].age += dt;
            const sl::FlightBody *d = universe_.world.body(debris_[i].body);
            const bool fallen = universe_.world.altitude_of(d->position) < -20.0;
            if (fallen || debris_[i].age > 900.0)
            {
                universe_.world.destroy_body(debris_[i].body);
                debris_.erase(debris_.begin() + static_cast<std::ptrdiff_t>(i));
            }
            else
                ++i;
        }
    }

    void Ship::publish(sl::PropertyTree &tree, const std::string &base)
    {
        unpublish();
        tree_ = &tree;
        base_ = base;
        const auto num = [&](const char *path, std::function<double()> getter) { tree.node(base + "/" + path)->tie_double(std::move(getter)); };
        tree.node(base + "/name")->tie_string([this] { return name; });
        tree.node(base + "/class")->tie_string([this] { return spec().name; });
        tree.node(base + "/gear-position")->tie_double([this] { return gear_position_; });
        tree.node(base + "/shield")->tie_double([this] { return spec_.shield.capacity > 0.0 ? shield_ / spec_.shield.capacity : 0.0; });
        tree.node(base + "/health")->tie_double([this] { return health_; });
        tree.node(base + "/thermal/temp")->tie_double([this] { return thermal_.temp; });
        tree.node(base + "/thermal/flux")->tie_double([this] { return thermal_.flux; });
        tree.node(base + "/thermal/peak-flux")->tie_double([this] { return thermal_.peak_flux; });
        tree.node(base + "/thermal/peak-temp")->tie_double([this] { return thermal_.peak_temp; });
        tree.node(base + "/thermal/shield")->tie_double([this] { return std::max(thermal_.shield, 0.0); });
        tree.node(base + "/thermal/blackout")->tie_bool([this] { return thermal_.blackout; });
        tree.node(base + "/chutes/open")->tie_int([this] {
            int n = 0;
            for (const ChuteRuntime &c : chutes_)
                n += c.state != ChuteState::stowed && c.state != ChuteState::gone ? 1 : 0;
            return n;
        });
        tree.node(base + "/chutes/stowed")->tie_int([this] {
            int n = 0;
            for (const ChuteRuntime &c : chutes_)
                n += c.state == ChuteState::stowed ? 1 : 0;
            return chutes_.empty() ? static_cast<int>(spec_.parachutes.size()) : n;
        });
        for (std::size_t i = 0; i < spec_.animation_controls.size(); ++i)
            tree.node(base + "/anim/" + spec_.animation_controls[i].property)->tie_double([this, i] { return animation_position(i); });
        tree.node(base + "/pod-angle-left")->tie_double([this] { return pod_angle_[0] / 3.14159265358979; });
        tree.node(base + "/pod-angle-right")->tie_double([this] { return pod_angle_[1] / 3.14159265358979; });
        tree.node(base + "/stage")->tie_int([this] { return active_stage_; });
        tree.node(base + "/stage-count")->tie_int([this] { return stage_count(); });
        tree.node(base + "/crashed")->tie_bool([this] { return crashed_; });
        tree.node(base + "/landed")->tie_bool([this] { return telemetry_.landed; });
        tree.node(base + "/on-rails")->tie_bool([this] { return on_rails(); });
        tree.node(base + "/soi")->tie_string([this] { return universe_.bodies[static_cast<std::size_t>(telemetry_.soi)].name; });
        num("x", [this] { return body().position.x; });
        num("y", [this] { return body().position.y; });
        num("z", [this] { return body().position.z; });
        num("vx", [this] { return body().velocity.x; });
        num("vy", [this] { return body().velocity.y; });
        num("vz", [this] { return body().velocity.z; });
        num("altitude", [this] { return telemetry_.altitude; });
        num("speed", [this] { return telemetry_.surface_speed; });
        num("orbital-speed", [this] { return telemetry_.orbital_speed; });
        num("vertical-speed", [this] { return telemetry_.vertical_speed; });
        num("pitch", [this] { return telemetry_.pitch; });
        num("heading", [this] { return telemetry_.heading; });
        num("roll", [this] { return telemetry_.roll; });
        num("alpha", [this] { return telemetry_.alpha; });
        // Body rates in degrees per second: nose up, nose right and right bank are positive.
        num("rate-pitch", [this] { return body().angular_velocity.x * 180.0 / pi; });
        num("rate-yaw", [this] { return -body().angular_velocity.y * 180.0 / pi; });
        num("rate-roll", [this] { return -body().angular_velocity.z * 180.0 / pi; });
        num("sideslip", [this] { return telemetry_.sideslip; });
        num("dynamic-pressure", [this] { return telemetry_.dynamic_pressure; });
        num("flight-path", [this] { return telemetry_.flight_path; });
        num("mass", [this] { return telemetry_.mass; });
        num("thrust", [this] { return telemetry_.thrust; });
        num("twr", [this] { return telemetry_.twr; });
        num("max-thrust", [this] { return telemetry_.max_thrust; });
        num("acceleration-g", [this] { return telemetry_.acceleration_g; });
        num("stage-fuel", [this] { return telemetry_.stage_fuel; });
        num("stage-fuel-capacity", [this] { return active_stage_ < static_cast<int>(spec_.stages.size()) ? spec_.stages[static_cast<std::size_t>(active_stage_)].propellant : 0.0; });
        num("stage-fuel-percent", [this] {
            const double cap = active_stage_ < static_cast<int>(spec_.stages.size()) ? spec_.stages[static_cast<std::size_t>(active_stage_)].propellant : 0.0;
            return cap > 0.0 ? 100.0 * telemetry_.stage_fuel / cap : 0.0;
        });
        num("rcs-fuel-percent", [this] { return spec_.rcs_propellant > 0.0 ? 100.0 * rcs_fuel_ / spec_.rcs_propellant : 0.0; });
        num("fuel-flow", [this] { return fuel_flow_; });
        num("rcs-fuel", [this] { return rcs_fuel_; });
        num("rcs-fuel-capacity", [this] { return spec_.rcs_propellant; });
        for (const char *role : {"main-left", "main-right", "hover-front", "hover-aft", "pod-left", "pod-right"})
            for (const char *what : {"thrust", "level", "flow", "accel"})
                tree.node(base + "/engine/" + role + "/" + what)->tie_double([this, role = std::string(role), what = std::string(what)] { return engine_value(role, what); });
        num("rcs-flow", [this] { return rcs_flow_; });
        num("total-fuel", [this] { return telemetry_.total_fuel; });
        num("orbit/apoapsis", [this] { return telemetry_.apoapsis; });
        num("orbit/periapsis", [this] { return telemetry_.periapsis; });
        num("orbit/eccentricity", [this] { return telemetry_.eccentricity; });
        num("orbit/inclination", [this] { return telemetry_.inclination; });
        num("latitude", [this] { return telemetry_.latitude; });
        num("map/points", [this] { return static_cast<double>(track_.count); });
        num("map/impact", [this] { return track_.impact ? 1.0 : 0.0; });
        num("map/impact-latitude", [this] { return track_.impact_lat; });
        num("map/impact-longitude", [this] { return track_.impact_lon; });
        num("map/impact-time", [this] { return track_.impact_time; });
        num("map/body-spin", [this] { return universe_.bodies[static_cast<std::size_t>(telemetry_.soi)].spin; });
        for (int i = 0; i <= track_points; ++i)
        {
            const std::string at = "map/point[" + std::to_string(i) + "]/";
            num((at + "lat").c_str(), [this, i] { return track_.lat[i]; });
            num((at + "lon").c_str(), [this, i] { return track_.lon[i]; });
        }
        num("longitude", [this] { return telemetry_.longitude; });
        num("body-radius", [this] { return telemetry_.body_radius; });
        num("orbit/semi-major-axis", [this] { return telemetry_.semi_major_axis; });
        num("orbit/raan", [this] { return telemetry_.raan; });
        num("orbit/arg-periapsis", [this] { return telemetry_.arg_periapsis; });
        num("orbit/true-anomaly", [this] { return telemetry_.true_anomaly; });
        num("orbit/period", [this] { return telemetry_.period; });
        num("orbit/time-to-apoapsis", [this] { return telemetry_.time_to_apoapsis; });
        num("orbit/time-to-periapsis", [this] { return telemetry_.time_to_periapsis; });
        tree.node(base + "/orbit/bound")->tie_bool([this] { return telemetry_.bound; });
        num("position/x", [this] { return body().position.x; });
        num("position/y", [this] { return body().position.y; });
        num("position/z", [this] { return body().position.z; });
        num("velocity/x", [this] { return body().velocity.x; });
        num("velocity/y", [this] { return body().velocity.y; });
        num("velocity/z", [this] { return body().velocity.z; });
        if (systems_)
            systems_->publish(tree, base + "/systems");
    }

    void Ship::unpublish()
    {
        if (!tree_)
            return;
        // Tied nodes capture this ship, so drop the whole subtree. The base looks like /ships/ship[2].
        const std::size_t slash = base_.find_last_of('/');
        const std::string parent = base_.substr(0, slash);
        const std::string leaf = base_.substr(slash + 1);
        const std::size_t bracket = leaf.find('[');
        const std::string leaf_name = leaf.substr(0, bracket);
        const int index = bracket == std::string::npos ? 0 : std::stoi(leaf.substr(bracket + 1));
        if (sl::PropertyNode *p = tree_->find(parent.empty() ? "/" : parent))
            p->remove_child(leaf_name, index);
        tree_ = nullptr;
    }
} // namespace kosmos
