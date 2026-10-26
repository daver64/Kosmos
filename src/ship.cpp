#include "ship.h"

#include "json.hpp"
#include "part_kit.h"

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
            spec.script = json.value("script", "");
            if (json.contains("cockpit"))
            {
                const nlohmann::json &jc = json["cockpit"];
                spec.cockpit.present = true;
                spec.cockpit.eye_set = jc.contains("eye");
                spec.cockpit.eye = vec3(jc.value("eye", nlohmann::json()));
                spec.cockpit.yaw = jc.value("yaw", 0.0);
                spec.cockpit.pitch = jc.value("pitch", 0.0);
                spec.cockpit.fov = jc.value("fov", 0.0);
                spec.cockpit.mfds = jc.value("mfds", true);
                spec.cockpit.mfd_left = jc.value("mfd_left", spec.cockpit.mfd_left);
                spec.cockpit.mfd_right = jc.value("mfd_right", spec.cockpit.mfd_right);
                for (const nlohmann::json &ji : jc.value("panel", nlohmann::json::array()))
                    spec.cockpit.panel.push_back({ji.value("label", ""), ji.value("property", ""), ji.value("scale", 1.0), ji.value("format", "%.1f")});
                if (jc.contains("layout"))
                {
                    const nlohmann::json &jl = jc["layout"];
                    spec.cockpit.panel_width = jl.value("width", spec.cockpit.panel_width);
                    spec.cockpit.panel_height = jl.value("height", spec.cockpit.panel_height);
                    spec.cockpit.panel_image = jl.value("image", "");
                    if (jl.contains("position"))
                        spec.cockpit.panel_pos = vec3(jl["position"]);
                    spec.cockpit.panel_size = jl.value("size", spec.cockpit.panel_size);
                    spec.cockpit.panel_tilt = jl.value("tilt", spec.cockpit.panel_tilt);
                }
                for (const nlohmann::json &ji : jc.value("instruments", nlohmann::json::array()))
                {
                    Cockpit::PanelInstrument pi;
                    pi.mode = ji.value("mode", "");
                    pi.x = ji.value("x", 0.0f);
                    pi.y = ji.value("y", 0.0f);
                    pi.size = ji.value("size", 200.0f);
                    pi.frame = ji.value("frame", false);
                    if (!pi.mode.empty())
                        spec.cockpit.instruments.push_back(pi);
                }
            }
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
                w.rolling_resistance = jw.value("rolling_resistance", w.rolling_resistance);
                spec.wheels.push_back(w);
            }
            spec.camera_distance = json.value("camera_distance", spec.camera_distance);
            spec.retractable_gear = json.value("retractable_gear", false);
            spec.gear_time = std::max(0.1, json.value("gear_time", spec.gear_time));
            for (const nlohmann::json &js : json.at("stages"))
            {
                StageSpec stage;
                stage.name = js.value("name", "Stage");
                stage.dry_mass = js.value("dry_mass", stage.dry_mass);
                stage.propellant = js.value("propellant", 0.0);
                stage.z = js.value("z", 0.0);
                stage.length = js.value("length", stage.length);
                stage.radius = js.value("radius", stage.radius);
                stage.separation_speed = js.value("separation_speed", stage.separation_speed);
                for (const nlohmann::json &je : js.value("engines", nlohmann::json::array()))
                {
                    EngineSpec e;
                    e.position = vec3(je.value("position", nlohmann::json()), e.position);
                    e.direction = glm::normalize(vec3(je.value("direction", nlohmann::json()), e.direction));
                    e.thrust = je.value("thrust", 0.0);
                    e.isp = je.value("isp", e.isp);
                    e.plume = je.value("plume", e.plume);
                    e.air_breathing = je.value("air_breathing", false);
                    // Older specs only had air_breathing; that still means a jet.
                    e.type = engine_type_from_name(je.value("type", ""), e.air_breathing ? EngineType::jet : EngineType::rocket);
                    e.fuel = je.value("fuel", e.type == EngineType::solid ? std::string("solid") : e.type == EngineType::nuclear ? std::string("hydrogen") : e.fuel);
                    e.afterburner = je.value("afterburner", false);
                    e.range_ly = je.value("range_ly", e.range_ly);
                    if (e.type == EngineType::jet || e.type == EngineType::scramjet)
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
                spec.stages.push_back(std::move(stage));
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
        return true;
    }

    Ship::Ship(Universe &universe, ShipSpec spec) : name(spec.name), universe_(universe), spec_(std::move(spec))
    {
        build_body();
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

    void Ship::build_body()
    {
        body_ = universe_.world.create_body(name);
        sl::FlightBody &b = body();
        b.drag_area = spec_.drag_area;
        b.angular_damping = 0.0;
        for (std::size_t s = 0; s < spec_.stages.size(); ++s)
        {
            const StageSpec &stage = spec_.stages[s];
            b.tanks.push_back({stage.propellant, stage.propellant});
            stage_first_thruster_.push_back(static_cast<int>(b.thrusters.size()));
            for (const EngineSpec &e : stage.engines)
            {
                sl::Thruster t;
                t.position = e.position;
                t.direction = e.direction;
                t.max_thrust = e.type == EngineType::hyperjump ? 0.0 : e.thrust;
                t.isp = e.isp;
                t.tank = static_cast<int>(s);
                t.air_breathing = e.air_breathing;
                b.thrusters.push_back(t);
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

    void Ship::refresh_mass_properties()
    {
        sl::FlightBody &b = body();
        double mass = 0.0, transverse = 0.0, roll = 0.0;
        for (std::size_t s = static_cast<std::size_t>(active_stage_); s < spec_.stages.size(); ++s)
        {
            const StageSpec &st = spec_.stages[s];
            mass += st.dry_mass;
            transverse += st.dry_mass * (st.z * st.z + st.length * st.length / 12.0 + st.radius * st.radius / 4.0);
            roll += st.dry_mass * st.radius * st.radius / 2.0;
        }
        mass = std::max(mass, 1.0);
        const double scale = (mass + attached_mass_) / mass;
        b.dry_mass = mass + attached_mass_;
        b.dry_inertia = {std::max(transverse, 1.0) * scale, std::max(transverse, 1.0) * scale, std::max(roll, 1.0) * scale};
        if (spec_.inertia.x > 0.0 && spec_.inertia.y > 0.0 && spec_.inertia.z > 0.0)
            b.dry_inertia = spec_.inertia * scale;
    }

    void Ship::set_attached_mass(double kilograms)
    {
        if (std::fabs(kilograms - attached_mass_) < 1.0e-6)
            return;
        attached_mass_ = kilograms;
        refresh_mass_properties();
    }

    void Ship::port_world(int index, sl::dvec3 &position, sl::dvec3 &direction, sl::dvec3 &up) const
    {
        const PortSpec &port = spec_.ports[static_cast<std::size_t>(index)];
        const sl::FlightBody &b = body();
        position = b.to_world(port.position);
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

        double lowest = 0.0;
        if (spec_.aircraft())
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
            gear_k_ = mass * surface_gravity / ((spec_.aircraft() ? 0.12 : 0.3) * gear_count);
            if (spread > 1e-6 && !spec_.aircraft())
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
        b.orientation = orientation_from(spec_.aircraft() ? roof : up, spec_.aircraft() ? up : -roof); // rockets lie heads-down, so the roof ends up skyward as the ascent pitches over
        b.angular_velocity = sl::dvec3(0.0);
        crashed_ = false;
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
        update_telemetry();
    }

    void Ship::place_in_orbit(int body_id, double altitude, double inclination_degrees, double phase_degrees)
    {
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

    void Ship::apply_attitude(const Controls &c, double)
    {
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
        for (int axis = 0; axis < 3; ++axis)
        {
            if (std::fabs(manual[axis]) > 0.02)
                command[axis] = std::clamp(manual[axis], -1.0, 1.0);
            else if (active)
                command[axis] = std::clamp((desired[axis] - w[axis]) * 2.0, -1.0, 1.0);
        }
        rcs_torque_ = command;
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
        universe_.world.set_control(body_, "elevator", command(c.pitch, w.x, spec_.damping.x));
        universe_.world.set_control(body_, "rudder", command(c.yaw, w.y, spec_.damping.y));
        universe_.world.set_control(body_, "aileron", command(c.roll, w.z, spec_.damping.z));
        universe_.world.set_control(body_, "flaps", std::clamp(c.flaps, 0.0, 1.0));

        // Nose-wheel steering follows the yaw input; the wheels roll along their own heading.
        const std::size_t first = b.gear.size() - spec_.wheels.size();
        for (std::size_t i = 0; i < spec_.wheels.size(); ++i)
        {
            const double angle = spec_.wheels[i].steer * std::clamp(c.yaw, -1.0, 1.0);
            b.gear[first + i].forward = {-std::sin(angle), 0.0, -std::cos(angle)};
        }
    }

    void Ship::apply_controls(const Controls &c, double dt)
    {
        rcs_torque_ = rcs_push_ = sl::dvec3(0.0);
        if (crashed_)
            return;
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

        if (c.stage)
            stage_once();
        const double throttle = std::clamp(c.throttle, 0.0, 1.0);
        for (std::size_t i = 0; i < b.thrusters.size(); ++i)
            b.thrusters[i].level = 0.0;
        for (int i = stage_first_thruster_[static_cast<std::size_t>(active_stage_)];
             i < stage_first_thruster_[static_cast<std::size_t>(active_stage_) + 1]; ++i)
            b.thrusters[static_cast<std::size_t>(i)].level = throttle;
        if (spec_.aircraft())
        {
            // A parked aircraft holds its brakes; once rolling, only the pedal brakes.
            const bool parked = throttle < 0.02 && telemetry_.landed && telemetry_.surface_speed < 2.0;
            universe_.world.set_brakes(body_, std::max(c.brake, parked ? 1.0 : 0.0));
        }
        else
            universe_.world.set_brakes(body_, throttle < 0.02 ? 1.0 : 0.0);

        if (spec_.aircraft())
        {
            apply_flight_controls(c);
            return;
        }

        if (b.mode == sl::FlightBodyMode::dynamic)
        {
            apply_attitude(c, dt);
            rcs_push_ = sl::dvec3(0.0);
            if (spec_.rcs_thrust > 0.0)
            {
                const sl::dvec3 push(c.rcs_x, c.rcs_y, -c.rcs_z);
                rcs_push_ = glm::length(push) > 0.0 ? glm::normalize(push) : push;
                if (glm::length(push) > 0.0)
                    universe_.world.apply_force(body_, b.direction_to_world(glm::normalize(push) * spec_.rcs_thrust));
            }
        }
    }

    bool Ship::stage_once()
    {
        if (active_stage_ >= stage_count() - 1)
            return false;
        drop_stage(active_stage_);
        ++active_stage_;
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
            const sl::dvec3 tail = b.direction_to_world({0.0, 0.0, 1.0});
            d.dry_mass = std::max(st.dry_mass + leftover, 1.0);
            const double transverse = d.dry_mass * (st.length * st.length / 12.0 + st.radius * st.radius / 4.0);
            d.dry_inertia = {transverse, transverse, d.dry_mass * st.radius * st.radius / 2.0};
            d.position = b.position + b.direction_to_world({0.0, 0.0, st.z});
            d.velocity = b.velocity + tail * st.separation_speed;
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

    void Ship::go_dynamic()
    {
        if (on_rails())
            universe_.world.release_rails(body_);
    }

    bool Ship::on_rails() const { return body().mode == sl::FlightBodyMode::rails; }

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
        if (c.throttle > 0.0 && telemetry_.thrust > 0.0)
            return "engines are firing";
        const Body &planet = universe_.bodies[static_cast<std::size_t>(telemetry_.soi)];
        const double top = atmosphere_top(planet);
        if (telemetry_.altitude < top + 1000.0)
            return "inside the atmosphere";
        if (telemetry_.bound ? telemetry_.periapsis <= top : (telemetry_.vertical_speed <= 0.0 && telemetry_.periapsis <= top))
            return "orbit dips into the atmosphere";
        return {};
    }

    void Ship::update_telemetry()
    {
        Telemetry &t = telemetry_;
        sl::FlightBody &b = body();
        t.soi = universe_.soi_body(b.position);
        const Body &planet = universe_.bodies[static_cast<std::size_t>(t.soi)];
        const sl::FlightBody &pb = *universe_.world.body(t.soi);

        double lowest = 0.0;
        if (spec_.aircraft())
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
            const sl::dvec3 b = glm::inverse(universe_.orientation(t.soi)) * r;
            t.latitude = std::asin(std::clamp(b.y / glm::length(b), -1.0, 1.0)) * 180.0 / pi;
            t.longitude = std::atan2(-b.z, b.x) * 180.0 / pi;
            t.body_radius = planet.radius;
        }

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
        t.total_fuel = 0.0;
        for (const sl::PropellantTank &tank : b.tanks)
            t.total_fuel += tank.mass;
    }

    void Ship::after_step(double dt)
    {
        update_telemetry();
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
                b.mode = sl::FlightBodyMode::kinematic;
                b.velocity = universe_.world.body(telemetry_.soi)->velocity;
                b.angular_velocity = sl::dvec3(0.0);
                for (sl::Thruster &t : b.thrusters)
                    t.level = 0.0;
            }
        }

        if (!crashed_ && b.mode == sl::FlightBodyMode::dynamic && !telemetry_.landed)
        {
            double tail = 0.0;
            for (std::size_t s = static_cast<std::size_t>(active_stage_); s < spec_.stages.size(); ++s)
                tail = std::max(tail, spec_.stages[s].z + spec_.stages[s].length * 0.5);
            double gear_z = 0.0;
            for (const sl::GearContact &g : b.gear)
                gear_z = std::max(gear_z, g.position.z);
            if (telemetry_.altitude + std::max(tail, gear_z) < 0.4 * std::max(tail, gear_z))
            {
                crashed_ = true;
                b.mode = sl::FlightBodyMode::kinematic;
                b.velocity = universe_.world.body(telemetry_.soi)->velocity;
                b.angular_velocity = sl::dvec3(0.0);
                for (sl::Thruster &t : b.thrusters)
                    t.level = 0.0;
            }
        }
        else if (crashed_ && b.mode == sl::FlightBodyMode::kinematic)
            b.velocity = universe_.world.body(telemetry_.soi)->velocity;

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
        tree.node(base + "/gear-position")->tie_double([this] { return gear_position_; });
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
        num("sideslip", [this] { return telemetry_.sideslip; });
        num("dynamic-pressure", [this] { return telemetry_.dynamic_pressure; });
        num("flight-path", [this] { return telemetry_.flight_path; });
        num("mass", [this] { return telemetry_.mass; });
        num("thrust", [this] { return telemetry_.thrust; });
        num("twr", [this] { return telemetry_.twr; });
        num("max-thrust", [this] { return telemetry_.max_thrust; });
        num("acceleration-g", [this] { return telemetry_.acceleration_g; });
        num("stage-fuel", [this] { return telemetry_.stage_fuel; });
        num("total-fuel", [this] { return telemetry_.total_fuel; });
        num("orbit/apoapsis", [this] { return telemetry_.apoapsis; });
        num("orbit/periapsis", [this] { return telemetry_.periapsis; });
        num("orbit/eccentricity", [this] { return telemetry_.eccentricity; });
        num("orbit/inclination", [this] { return telemetry_.inclination; });
        num("latitude", [this] { return telemetry_.latitude; });
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
