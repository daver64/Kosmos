#include "eva_solids.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>

namespace kosmos
{
    namespace
    {
        constexpr double pi = 3.14159265358979323846;
        // Anything this low is stepped over, and feet this far above a top clear it.
        constexpr double step_height = 0.45, clearance = 0.15;
        // Collision shapes are a little bigger than what they stand for, so you stop short of the surface.
        constexpr double margin = 0.3;

        struct Footprint
        {
            double min_x = 0.0, max_x = 0.0, min_z = 0.0, max_z = 0.0, top = 0.0;
            bool solid = false;
        };

        // The footprint of a base object's mesh in its own frame, kept so the mesh is built once per kind of object.
        const Footprint &footprint(const BaseObject &o)
        {
            static std::unordered_map<std::string, Footprint> cache;
            const std::string key = o.type + "|" + std::to_string(o.size) + "|" + std::to_string(o.width) + "|" + std::to_string(o.depth) + "|" + std::to_string(o.height) + "|" +
                                    std::to_string(o.length) + "|" + std::to_string(o.radius) + "|" + std::to_string(o.number) + "|" + std::to_string(o.seed);
            const auto found = cache.find(key);
            if (found != cache.end())
                return found->second;
            Footprint f;
            // Flat things you walk on, and models whose shape is unknown, are not solid.
            if (o.type != "road" && o.type != "landing_pad" && o.type != "launch_pad" && o.type != "model")
            {
                sl::MeshData mesh;
                if (base_object_mesh(o, mesh) && !mesh.vertices.empty())
                {
                    f.min_x = f.min_z = 1.0e9;
                    f.max_x = f.max_z = -1.0e9;
                    for (const sl::Vertex3D &v : mesh.vertices)
                    {
                        f.min_x = std::min<double>(f.min_x, v.position[0]);
                        f.max_x = std::max<double>(f.max_x, v.position[0]);
                        f.min_z = std::min<double>(f.min_z, v.position[2]);
                        f.max_z = std::max<double>(f.max_z, v.position[2]);
                        f.top = std::max<double>(f.top, v.position[1]);
                    }
                    f.solid = f.top > step_height;
                }
            }
            return cache.emplace(key, f).first->second;
        }

        sl::dvec3 horizontal(const sl::dvec3 &v, const sl::dvec3 &up)
        {
            return v - up * glm::dot(v, up);
        }

        sl::dvec3 normalised(const sl::dvec3 &v, const sl::dvec3 &fallback)
        {
            const double l = glm::length(v);
            return l > 1e-9 ? v / l : fallback;
        }

        // Pushes out of a vertical cylinder whose base is `base` (relative to the body centre).
        void cylinder(const sl::dvec3 &base, double radius, double top, const sl::dvec3 &up, const sl::dvec3 &from, double height, sl::dvec3 &to)
        {
            if (height > top - clearance)
                return;
            const sl::dvec3 offset = horizontal(to - base, up);
            const double reach = radius + margin + person_radius, d = glm::length(offset);
            if (d >= reach)
                return;
            const sl::dvec3 away = normalised(d > 1e-6 ? offset : horizontal(from - base, up), sl::dvec3(1.0, 0.0, 0.0));
            to += away * (reach - d);
        }
    } // namespace

    sl::dvec3 push_out_of_solids(const SolidScene &scene, const sl::dvec3 &from, const sl::dvec3 &to_in, double height)
    {
        if (!scene.universe)
            return to_in;
        const Universe &universe = *scene.universe;
        const sl::dvec3 centre = universe.position(scene.body);
        const double radius = universe.bodies[static_cast<std::size_t>(scene.body)].radius;
        sl::dvec3 to = to_in;
        const sl::dvec3 up = normalised(to, sl::dvec3(0.0, 1.0, 0.0));

        // Ships: the hull is a cylinder about the ship's own Z axis, one for each stage.
        for (Ship *ship : scene.ships)
        {
            const sl::FlightBody &b = ship->body();
            const sl::dvec3 origin = ship->origin() - centre;
            if (glm::length(origin - to) > 400.0)
                continue;
            const sl::dquat inverse = glm::inverse(b.orientation);
            const double heights[2] = {0.25, 1.7}; // feet and head
            const ShipSpec &spec = ship->spec();
            if (spec.interior)
            {
                // A building with an interior is its grid of walls; stepping into one means staying where you were.
                const Interior &room = *spec.interior;
                auto feet = [&](const sl::dvec3 &p) { return room.from_ship(inverse * (p - origin)) + sl::dvec3(0.0, 0.3, 0.0); };
                const double r = person_radius + margin;
                if (room.blocked(feet(to), r, 1.4) && !room.blocked(feet(from), r, 1.4))
                    return from;
                continue;
            }
            for (const double h : heights)
            {
                const sl::dvec3 sample = to + up * h;
                const sl::dvec3 local = inverse * (sample - origin);
                if (spec.aircraft())
                {
                    // An aircraft is a rectangular block around its fuselage, wings and gear.
                    sl::dvec3 lo(-1.0e9), hi(-1.0e9);
                    double z0 = 1.0e9, z1 = -1.0e9, half_x = 0.0, top = 0.0, bottom = 0.0;
                    for (const StageSpec &st : spec.stages)
                    {
                        z0 = std::min(z0, st.z - st.length * 0.5);
                        z1 = std::max(z1, st.z + st.length * 0.5);
                        half_x = std::max(half_x, st.radius);
                        top = std::max(top, st.radius);
                        bottom = std::min(bottom, -st.radius);
                        for (const sl::dvec3 &g : st.gear)
                            bottom = std::min(bottom, g.y);
                    }
                    for (const SurfaceSpec &sf : spec.surfaces)
                    {
                        half_x = std::max(half_x, std::fabs(sf.position.x));
                        top = std::max(top, sf.position.y);
                    }
                    for (const WheelSpec &w : spec.wheels)
                        bottom = std::min(bottom, w.position.y);
                    const double m = margin + person_radius;
                    const double pen[4] = {half_x + m - local.x, half_x + m + local.x, z1 + m - local.z, -(z0 - m) + local.z};
                    if (local.y > top + m || local.y < bottom - m || pen[0] <= 0 || pen[1] <= 0 || pen[2] <= 0 || pen[3] <= 0)
                        continue;
                    const sl::dvec3 normals[4] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};
                    int best = 0;
                    for (int i = 1; i < 4; ++i)
                        if (pen[i] < pen[best])
                            best = i;
                    // Standing on the roof or under the belly is not a way out sideways.
                    if (std::min(top + m - local.y, local.y - (bottom - m)) < pen[best] * 0.5)
                        return from;
                    to += normalised(horizontal(b.orientation * normals[best], up), sl::dvec3(1.0, 0.0, 0.0)) * pen[best];
                    continue;
                }
                for (const StageSpec &st : ship->spec().stages)
                {
                    const double z0 = st.z - st.length * 0.5, z1 = st.z + st.length * 0.5;
                    const double along = std::clamp(local.z, std::min(z0, z1), std::max(z0, z1));
                    const sl::dvec3 axis_point = b.orientation * sl::dvec3(0.0, 0.0, along) + origin;
                    const sl::dvec3 out = sample - axis_point;
                    double hull = st.radius, skin = margin;
                    bool profiled = false;
                    for (const PartSpec &part : st.parts)
                        profiled = profiled || part.profile.size() > 1;
                    if (spec.building() && profiled)
                    {
                        // Follow the lathe profile so a dome is as wide as it looks at this height.
                        hull = -1.0;
                        for (const PartSpec &part : st.parts)
                            for (std::size_t i = 1; i < part.profile.size(); ++i)
                            {
                                const double za = st.z + part.profile[i - 1].first, zb = st.z + part.profile[i].first;
                                if (za == zb || local.z < std::min(za, zb) || local.z > std::max(za, zb))
                                    continue;
                                const double t = (local.z - za) / (zb - za);
                                hull = std::max(hull, part.profile[i - 1].second + t * (part.profile[i].second - part.profile[i - 1].second));
                            }
                        if (hull < 0.0)
                            continue;
                        skin = 0.1;
                    }
                    const double d = glm::length(out), reach = hull + skin + person_radius;
                    if (d >= reach)
                        continue;
                    const sl::dvec3 flat = horizontal(out, up);
                    const double fraction = d > 1e-9 ? glm::length(flat) / d : 0.0;
                    // Directly above or below the hull there is no sideways way out, so stay where you were.
                    if (fraction < 0.3)
                        return from;
                    to += normalised(flat, sl::dvec3(1.0, 0.0, 0.0)) * std::min((reach - d) / fraction, 1.0);
                }
            }
        }

        // Base objects, in each base's own frame.
        if (scene.bases)
        {
            const std::string &body_name = universe.bodies[static_cast<std::size_t>(scene.body)].name;
            for (const Base &base : *scene.bases)
            {
                if (base.body != body_name)
                    continue;
                double x = 0.0, z = 0.0;
                base_local_xz(universe, base, centre + to, x, z);
                if (std::fabs(x) > 1500.0 || std::fabs(z) > 1500.0)
                    continue;
                double push_x = 0.0, push_z = 0.0;
                for (const BaseObject &o : base.objects)
                {
                    for (int i = 0; i < std::max(1, o.count); ++i)
                    {
                        const double ox = o.x + o.step_x * i, oz = o.z + o.step_z * i;
                        const double k = o.scale > 0.0 ? o.scale : 1.0;
                        // Cheap rejection before the footprint is looked up.
                        if (std::fabs(x - ox) > 400.0 || std::fabs(z - oz) > 400.0)
                            continue;
                        const Footprint &f = footprint(o);
                        if (!f.solid || height > o.y + k * f.top + margin - clearance)
                            continue;
                        const double a = o.heading * pi / 180.0, c = std::cos(a), s = std::sin(a);
                        const double px = x + push_x - ox, pz = z + push_z - oz;
                        const double vx = (px * c + pz * s) / k, vz = (-px * s + pz * c) / k, pr = (person_radius + margin) / k;
                        if (vx <= f.min_x - pr || vx >= f.max_x + pr || vz <= f.min_z - pr || vz >= f.max_z + pr)
                            continue;
                        // Out through the nearest side.
                        const double out_x_min = vx - (f.min_x - pr), out_x_max = (f.max_x + pr) - vx;
                        const double out_z_min = vz - (f.min_z - pr), out_z_max = (f.max_z + pr) - vz;
                        const double least = std::min({out_x_min, out_x_max, out_z_min, out_z_max});
                        double dx = 0.0, dz = 0.0;
                        if (least == out_x_min)
                            dx = -out_x_min;
                        else if (least == out_x_max)
                            dx = out_x_max;
                        else if (least == out_z_min)
                            dz = -out_z_min;
                        else
                            dz = out_z_max;
                        push_x += k * (dx * c - dz * s);
                        push_z += k * (dx * s + dz * c);
                    }
                }
                if (push_x != 0.0 || push_z != 0.0)
                {
                    const sl::dvec3 a = base_point(base, radius, x, z), b = base_point(base, radius, x + push_x, z + push_z);
                    to += (b - a) * radius;
                }
            }
        }

        // Trees, rocks, houses and outposts scattered over the ground.
        if (scene.clutter)
        {
            std::vector<sl::ClutterObstacle> near;
            scene.clutter->obstacles(to, 12.0, near);
            for (const sl::ClutterObstacle &o : near)
            {
                if (o.height < step_height)
                    continue;
                if (o.height > 2.0 * o.radius)
                {
                    cylinder(o.position, o.radius, o.height, up, from, height, to);
                    continue;
                }
                // Squat objects such as rocks and bushes are a sphere around their middle.
                const double r = std::max(o.radius, o.height * 0.5) * 1.1 + margin;
                const sl::dvec3 centre_of = o.position + up * (o.height * 0.5);
                const double reach = r + person_radius;
                for (const double h : {0.25, 1.0})
                {
                    const sl::dvec3 offset = to + up * h - centre_of;
                    const double vertical = glm::dot(offset, up);
                    const sl::dvec3 flat = horizontal(offset, up);
                    const double d_h = glm::length(flat), need = reach * reach - vertical * vertical;
                    if (need <= 0.0)
                        continue;
                    const double want = std::sqrt(need);
                    if (d_h < want)
                        to += normalised(d_h > 1e-6 ? flat : horizontal(from - o.position, up), sl::dvec3(1.0, 0.0, 0.0)) * (want - d_h);
                }
            }
        }
        return to;
    }
} // namespace kosmos
