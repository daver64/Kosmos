#include "ship.h"
#include "ship_internal.h"

#include "json.hpp"
#include "part_kit.h"
#include "reentry_json.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace kosmos
{
    using namespace ship_detail;

    namespace
    {
        sl::dvec3 vec3(const nlohmann::json &j, sl::dvec3 fallback = {0.0, 0.0, 0.0})
        {
            if (!j.is_array() || j.size() < 3)
                return fallback;
            return {j[0].get<double>(), j[1].get<double>(), j[2].get<double>()};
        }
    }

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
                spec.cockpit.vc_mfd_flip = jc.value("vc_mfd_flip", false);
                if (jc.contains("vc_animations") && jc["vc_animations"].is_array())
                    for (const auto &ja : jc["vc_animations"])
                    {
                        VcAnimation a;
                        a.parts = ja.value("parts", std::vector<std::string>());
                        a.source = ja.value("source", "");
                        a.angle_deg = ja.value("angle", 0.0);
                        a.initial = ja.value("initial", 0.0);
                        a.min = std::min(ja.value("min", 0.0), 0.0);
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
                for (const nlohmann::json &jg : jc.value("vc_gauges", nlohmann::json::array()))
                    spec.cockpit.vc_gauges.push_back({jg.value("part", std::string()), jg.value("image", std::string()), read_controls(jg.value("controls", nlohmann::json::array()))});
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
                    hs.outline_active = jh.value("outline_active", false);
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
                port.stage = jd.value("stage", -1);
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
            if (json.contains("portal")) spec.portal_radius = json["portal"].value("radius", 180.0);
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
            spec.reverse_throttle = json.value("reverse_throttle", false);
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
            if (json.contains("instruments"))
                for (const auto &ji : json["instruments"])
                {
                    ShipSpec::Instrument in;
                    in.type = ji.value("type", in.type);
                    if (in.type != "visual" && in.type != "ir" && in.type != "uv" && in.type != "radar" && in.type != "spectrometer")
                        continue;
                    in.name = ji.value("name", in.type);
                    in.id = ji.value("id", in.name);
                    in.load = ji.value("load", "");
                    if (ji.contains("position"))
                        in.position = vec3(ji["position"]);
                    if (ji.contains("direction"))
                        in.direction = vec3(ji["direction"]);
                    if (glm::dot(in.direction, in.direction) < 1e-9)
                        in.direction = sl::dvec3(0.0, 0.0, 1.0);
                    in.direction = glm::normalize(in.direction);
                    in.fov = std::clamp(ji.value("fov", in.fov), 0.2, 120.0);
                    in.slew = std::max(0.0, ji.value("slew", in.slew));
                    in.max_slew = std::clamp(ji.value("max_slew", in.max_slew), 0.0, 180.0);
                    in.sun_limit = std::clamp(ji.value("sun_limit", in.sun_limit), 0.0, 90.0);
                    in.resolution = std::clamp(ji.value("resolution", in.resolution), 64, 2048);
                    in.frames = std::max(0, ji.value("frames", in.frames));
                    spec.instruments.push_back(in);
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
            spec.life_pod = json.value("life_pod", false);
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
                stage.with_next = js.value("with_next", false);
                stage.radial_pair = js.value("radial_pair", false);
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
                if (stage.radial_pair)
                {
                    if (stage.parts.size() != 2)
                        throw std::runtime_error("radial_pair requires exactly two parts");
                    const sl::dvec3 a(stage.parts[0].offset.x, stage.parts[0].offset.y, 0.0);
                    const sl::dvec3 b(stage.parts[1].offset.x, stage.parts[1].offset.y, 0.0);
                    if (glm::length(a) < 1.0e-6 || glm::length(a + b) > 1.0e-6)
                        throw std::runtime_error("radial_pair parts require opposite nonzero X/Y offsets");
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
            for (PortSpec &port : spec.ports)
            {
                if (port.stage >= 0)
                    continue;
                double best = 1.0e30;
                for (std::size_t s = 0; s < spec.stages.size(); ++s)
                {
                    const StageSpec &st = spec.stages[s];
                    const double gap = std::max({0.0, port.position.z - (st.z + st.length * 0.5), (st.z - st.length * 0.5) - port.position.z});
                    if (gap < best - 1.0e-9)
                    {
                        best = gap;
                        port.stage = static_cast<int>(s);
                    }
                }
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
            spec.engine_sound = json.value("engine_sound", "");
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
            for (SystemComponent &component : spec.systems.components)
                if (!component.deployment_control.empty())
                {
                    for (std::size_t i = 0; i < spec.animation_controls.size(); ++i)
                        if (spec.animation_controls[i].property == component.deployment_control)
                        {
                            component.deployment_index = static_cast<int>(i);
                            break;
                        }
                    if (component.deployment_index < 0)
                        throw std::runtime_error("unknown solar deployment control: " + component.deployment_control);
                }
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
} // namespace kosmos
