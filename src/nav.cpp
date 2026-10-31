#include "nav.h"

#include "json.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <glm/gtc/quaternion.hpp>

namespace kosmos
{
    namespace
    {
        constexpr double pi = 3.14159265358979323846;
        constexpr double deg = 180.0 / pi;
        constexpr double tune_tolerance = 0.026;
        // NDBs sit at 190-535 kHz, so they are packed closer together than the VHF aids.
        constexpr double ndb_tolerance = 0.0015;

        double tolerance_for(const std::string &type)
        {
            return type == "ndb" ? ndb_tolerance : tune_tolerance;
        }
        constexpr int listed = 8;

        double wrap180(double a)
        {
            a = std::fmod(a + 180.0, 360.0);
            return (a < 0.0 ? a + 360.0 : a) - 180.0;
        }

        double wrap360(double a)
        {
            a = std::fmod(a, 360.0);
            return a < 0.0 ? a + 360.0 : a;
        }

        double default_range(const std::string &type)
        {
            return type == "vor" ? 2.0e6 : type == "ndb" ? 4.0e5 : 3.0e5;
        }

        // Port-to-port geometry as seen from `a`'s port: x right, y up, z along the port's outward direction.
        struct PortRelative
        {
            double x = 0, y = 0, z = 0, range = 0;
            double vx = 0, vy = 0, vz = 0, speed = 0;
            double yaw = 0, pitch = 0, roll = 0;
        };

        PortRelative relative_ports(const Ship &a, int pa, const Ship &b, int pb)
        {
            sl::dvec3 pos_a, dir_a, up_a, pos_b, dir_b, up_b;
            a.port_world(pa, pos_a, dir_a, up_a);
            b.port_world(pb, pos_b, dir_b, up_b);
            const sl::dvec3 right = glm::cross(dir_a, up_a);
            const sl::dvec3 d = pos_b - pos_a;
            const sl::dvec3 v = b.point_velocity(pos_b) - a.point_velocity(pos_a);
            PortRelative r;
            r.x = glm::dot(d, right);
            r.y = glm::dot(d, up_a);
            r.z = glm::dot(d, dir_a);
            r.range = glm::length(d);
            r.vx = glm::dot(v, right);
            r.vy = glm::dot(v, up_a);
            r.vz = glm::dot(v, dir_a);
            r.speed = glm::length(v);
            const sl::dvec3 db(glm::dot(dir_b, right), glm::dot(dir_b, up_a), glm::dot(dir_b, dir_a));
            const sl::dvec3 ub(glm::dot(up_b, right), glm::dot(up_b, up_a), glm::dot(up_b, dir_a));
            r.yaw = std::atan2(db.x, -db.z) * deg;
            r.pitch = std::atan2(db.y, -db.z) * deg;
            r.roll = std::atan2(ub.x, ub.y) * deg;
            return r;
        }
    } // namespace

    bool load_navaids(const std::string &file, std::vector<Transmitter> &out, std::string &error)
    {
        std::ifstream in(file);
        if (!in)
        {
            error = "cannot open " + file;
            return false;
        }
        try
        {
            nlohmann::json json;
            in >> json;
            for (const nlohmann::json &j : json.at("navaids"))
            {
                Transmitter t;
                t.type = j.at("type").get<std::string>();
                t.id = j.value("id", t.type + std::to_string(out.size()));
                t.name = j.value("name", t.id);
                t.ident = j.value("ident", "");
                t.body = j.value("body", "Earth");
                t.latitude = j.value("latitude", 0.0);
                t.longitude = j.value("longitude", 0.0);
                t.frequency = j.at("frequency").get<double>();
                t.range = j.value("range", default_range(t.type));
                t.heading = j.value("heading", 0.0);
                t.glide = j.value("glide", 3.0);
                out.push_back(std::move(t));
            }
        }
        catch (const std::exception &e)
        {
            error = file + ": " + e.what();
            return false;
        }
        return true;
    }

    sl::dvec3 surface_direction(const Universe &universe, int body, double latitude, double longitude)
    {
        const double la = latitude * pi / 180.0, lo = longitude * pi / 180.0;
        const double bx = std::cos(la) * std::cos(lo), by = std::sin(la), bz = -std::cos(la) * std::sin(lo);
        return universe.orientation(body) * sl::dvec3(bx, by, bz);
    }

    void surface_lat_lon(const Universe &universe, int body, const sl::dvec3 &direction, double &latitude, double &longitude)
    {
        const sl::dvec3 r = glm::normalize(direction);
        const sl::dvec3 b = glm::inverse(universe.orientation(body)) * r;
        latitude = std::asin(std::clamp(b.y, -1.0, 1.0)) * deg;
        longitude = std::atan2(-b.z, b.x) * deg;
    }

    void NavSystem::init(sl::PropertyTree &props)
    {
        props_ = &props;
        for (int i = 0; i < receivers; ++i)
        {
            const std::string base = "/nav/nav[" + std::to_string(i) + "]/";
            props.set_double(base + "frequency", 0.0);
            props.set_double(base + "obs", 0.0);
            props.set_bool(base + "valid", false);
        }
        props.set_int("/nav/count", 0);
        props.set_int("/dock/receiver", 0);
        props.set_int("/dock/own-port", 0);
        props.set_bool("/dock/undock", false);
        props.set_string("/dock/transfer", "auto");
        props.set_bool("/dock/valid", false);
        props.set_bool("/dock/docked", false);
    }

    void NavSystem::add_beacon(const Transmitter &beacon)
    {
        for (Transmitter &t : beacons_)
            if (t.id == beacon.id)
            {
                t = beacon;
                return;
            }
        beacons_.push_back(beacon);
    }

    void NavSystem::clear_links()
    {
        links_.clear();
        locks_.clear();
    }

    bool NavSystem::docked(int ship) const
    {
        for (const Link &l : links_)
            if (l.a == ship || l.b == ship)
                return true;
        return false;
    }

    bool NavSystem::port_busy(int ship, int port) const
    {
        for (const Link &l : links_)
            if ((l.a == ship && l.pa == port) || (l.b == ship && l.pb == port))
                return true;
        return false;
    }

    bool NavSystem::locked(int a, int b) const
    {
        for (const Lock &k : locks_)
            if (k.until > time_ && ((k.a == a && k.b == b) || (k.a == b && k.b == a)))
                return true;
        return false;
    }

    // Pose the follower so its port meets the leader's port face to face with the same "up", moving with the leader.
    void NavSystem::place_follower(Ship &leader, int leader_port, Ship &follower, int follower_port)
    {
        sl::dvec3 pos, dir, up;
        leader.port_world(leader_port, pos, dir, up);
        const sl::dvec3 right = glm::cross(dir, up);
        const sl::dmat3 wanted(-right, up, dir);
        const PortSpec &fp = follower.spec().ports[static_cast<std::size_t>(follower_port)];
        const sl::dmat3 local(glm::cross(fp.direction, fp.up), fp.up, -fp.direction);
        const sl::dmat3 rotation = wanted * glm::transpose(local);

        const bool rails = leader.on_rails();
        if (follower.on_rails())
            follower.go_dynamic();
        sl::FlightBody &fb = follower.body();
        const sl::FlightBody &lb = leader.body();
        fb.orientation = glm::normalize(glm::quat_cast(rotation));
        fb.position = pos - rotation * fp.position;
        const sl::dvec3 omega = lb.rotation() * lb.angular_velocity;
        fb.velocity = leader.point_velocity(pos) + glm::cross(omega, fb.position - pos);
        fb.angular_velocity = glm::transpose(rotation) * omega;
        if (rails)
            follower.go_rails();
    }

    void NavSystem::transfer_propellant(const std::vector<std::unique_ptr<Ship>> &ships, int active, double dt)
    {
        constexpr double rate = 100.0; // kg/s through each docking port
        const std::string mode = props_->get_string("/dock/transfer", "auto");
        auto tank_of = [&](int ship) -> sl::PropellantTank * {
            Ship &s = *ships[static_cast<std::size_t>(ship)];
            auto &tanks = s.body().tanks;
            const std::size_t stage = static_cast<std::size_t>(s.active_stage());
            return stage < tanks.size() ? &tanks[stage] : nullptr;
        };
        double partner_fuel = 0.0, partner_capacity = 0.0;
        for (const Link &l : links_)
        {
            if (l.a != active && l.b != active)
                continue;
            if (sl::PropellantTank *t = tank_of(l.a == active ? l.b : l.a))
            {
                partner_fuel += t->mass;
                partner_capacity += t->capacity;
            }
        }
        props_->set_double("/dock/partner-fuel", partner_fuel);
        props_->set_double("/dock/partner-capacity", partner_capacity);
        if (mode == "off")
            return;
        for (const Link &l : links_)
        {
            int from = -1, to = -1;
            if (mode == "auto")
            {
                const bool a_pod = ships[static_cast<std::size_t>(l.a)]->spec().fuel_pod, b_pod = ships[static_cast<std::size_t>(l.b)]->spec().fuel_pod;
                if (a_pod == b_pod)
                    continue;
                from = a_pod ? l.a : l.b;
                to = a_pod ? l.b : l.a;
            }
            else if (l.a == active || l.b == active)
            {
                const int other = l.a == active ? l.b : l.a;
                from = mode == "give" ? active : other;
                to = mode == "give" ? other : active;
            }
            if (from < 0)
                continue;
            sl::PropellantTank *src = tank_of(from), *dst = tank_of(to);
            if (!src || !dst)
                continue;
            const double moved = std::min({rate * dt, src->mass, dst->capacity - dst->mass});
            if (moved > 0.0)
            {
                src->mass -= moved;
                dst->mass += moved;
            }
        }
    }

    void NavSystem::couple(Universe &, const std::vector<std::unique_ptr<Ship>> &ships, int active, double dt)
    {
        time_ += dt;
        const int count = static_cast<int>(ships.size());
        links_.erase(std::remove_if(links_.begin(), links_.end(), [&](const Link &l) { return l.a >= count || l.b >= count; }), links_.end());
        locks_.erase(std::remove_if(locks_.begin(), locks_.end(), [&](const Lock &k) { return k.until <= time_; }), locks_.end());

        // Capture: two free ports, close, slow and roughly lined up.
        for (int a = 0; a < count; ++a)
            for (int b = a + 1; b < count; ++b)
            {
                Ship &sa = *ships[static_cast<std::size_t>(a)], &sb = *ships[static_cast<std::size_t>(b)];
                if (sa.on_rails() || sb.on_rails() || locked(a, b))
                    continue;
                for (int pa = 0; pa < static_cast<int>(sa.spec().ports.size()); ++pa)
                    for (int pb = 0; pb < static_cast<int>(sb.spec().ports.size()); ++pb)
                    {
                        if (!sa.port_attached(pa) || !sb.port_attached(pb) || port_busy(a, pa) || port_busy(b, pb))
                            continue;
                        const PortRelative r = relative_ports(sa, pa, sb, pb);
                        if (r.range < 1.0 && r.speed < 0.8 && std::fabs(r.yaw) < 15.0 && std::fabs(r.pitch) < 15.0 && std::fabs(r.roll) < 25.0)
                        {
                            links_.push_back({a, pa, b, pb});
                            props_->set_string("/dock/message", sa.name + " docked with " + sb.name);
                        }
                    }
            }

        // Each connected group follows its leader: the active ship if it is in the group, otherwise the lowest index.
        std::vector<char> seen(static_cast<std::size_t>(count), 0);
        std::vector<int> order;
        if (active >= 0 && active < count)
            order.push_back(active);
        for (int i = 0; i < count; ++i)
            order.push_back(i);
        for (int root : order)
        {
            if (seen[static_cast<std::size_t>(root)])
                continue;
            std::vector<int> group{root};
            seen[static_cast<std::size_t>(root)] = 1;
            for (std::size_t head = 0; head < group.size(); ++head)
            {
                const int cur = group[head];
                for (const Link &l : links_)
                {
                    const bool is_a = l.a == cur;
                    if (!is_a && l.b != cur)
                        continue;
                    const int other = is_a ? l.b : l.a;
                    if (seen[static_cast<std::size_t>(other)])
                        continue;
                    seen[static_cast<std::size_t>(other)] = 1;
                    group.push_back(other);
                    place_follower(*ships[static_cast<std::size_t>(cur)], is_a ? l.pa : l.pb, *ships[static_cast<std::size_t>(other)], is_a ? l.pb : l.pa);
                }
            }
            double carried = 0.0;
            for (std::size_t i = 1; i < group.size(); ++i)
                carried += ships[static_cast<std::size_t>(group[i])]->body().mass();
            ships[static_cast<std::size_t>(root)]->set_attached_mass(carried);
            for (std::size_t i = 1; i < group.size(); ++i)
                ships[static_cast<std::size_t>(group[i])]->set_attached_mass(0.0);
        }
        if (active >= 0 && active < count)
            transfer_propellant(ships, active, dt);
    }

    bool NavSystem::undock(const std::vector<std::unique_ptr<Ship>> &ships, int ship, int port)
    {
        bool any = false;
        for (std::size_t i = 0; i < links_.size();)
        {
            const Link l = links_[i];
            const bool mine = (l.a == ship && (port < 0 || l.pa == port)) || (l.b == ship && (port < 0 || l.pb == port));
            if (!mine)
            {
                ++i;
                continue;
            }
            any = true;
            links_.erase(links_.begin() + static_cast<std::ptrdiff_t>(i));
            Ship &a = *ships[static_cast<std::size_t>(l.a)], &b = *ships[static_cast<std::size_t>(l.b)];
            a.set_attached_mass(0.0);
            b.set_attached_mass(0.0);
            a.go_dynamic();
            b.go_dynamic();
            sl::dvec3 pos, dir, up;
            a.port_world(l.pa, pos, dir, up);
            const double ma = a.body().mass(), mb = b.body().mass();
            a.body().velocity -= dir * 0.5 * mb / (ma + mb);
            b.body().velocity += dir * 0.5 * ma / (ma + mb);
            locks_.push_back({l.a, l.b, time_ + 4.0});
            props_->set_string("/dock/message", a.name + " undocked from " + b.name);
        }
        return any;
    }

    std::vector<NavSystem::Contact> NavSystem::gather(const Universe &universe, const std::vector<std::unique_ptr<Ship>> &ships, int active) const
    {
        std::vector<Contact> list;
        const Ship &me = *ships[static_cast<std::size_t>(active)];
        const sl::dvec3 here = me.body().position;
        for (const Transmitter &t : beacons_)
        {
            const int body = universe.find(t.body);
            if (body < 0)
                continue;
            const sl::FlightBody &pb = *universe.world.body(body);
            const sl::dvec3 dir = surface_direction(universe, body, t.latitude, t.longitude);
            Contact c;
            c.frame.up = dir;
            c.frame.east = glm::normalize(glm::cross(universe.pole(body), dir));
            c.frame.north = glm::cross(dir, c.frame.east);
            const double spin = universe.bodies[static_cast<std::size_t>(body)].spin;
            c.frame.position = pb.position + dir * universe.bodies[static_cast<std::size_t>(body)].radius;
            c.frame.velocity = pb.velocity + glm::cross(universe.pole(body) * spin, c.frame.position - pb.position);
            const sl::dvec3 r = here - c.frame.position;
            c.distance = glm::length(r);
            if (c.distance > t.range || c.distance < 1.0e-3 || glm::dot(r / c.distance, c.frame.up) < 0.0)
                continue;
            c.type = t.type;
            c.name = t.name;
            c.ident = t.ident;
            c.frequency = t.frequency;
            c.beacon = &t;
            list.push_back(std::move(c));
        }
        for (int s = 0; s < static_cast<int>(ships.size()); ++s)
        {
            if (s == active)
                continue;
            const Ship &other = *ships[static_cast<std::size_t>(s)];
            Contact x;
            x.type = "xpdr";
            x.name = other.name;
            x.frequency = other.spec().transponder;
            x.ship = s;
            x.frame.position = other.body().position;
            x.frame.velocity = other.body().velocity;
            x.distance = glm::length(x.frame.position - here);
            if (x.frequency > 0.0 && x.distance < 1.0e7)
                list.push_back(x);
            for (int p = 0; p < static_cast<int>(other.spec().ports.size()); ++p)
            {
                if (!other.port_attached(p))
                    continue;
                sl::dvec3 pos, dir, up;
                other.port_world(p, pos, dir, up);
                Contact c;
                c.type = "ids";
                c.name = other.name + ": " + other.spec().ports[static_cast<std::size_t>(p)].name;
                c.frequency = other.spec().ports[static_cast<std::size_t>(p)].frequency;
                c.ship = s;
                c.port = p;
                c.frame.position = pos;
                c.frame.velocity = other.point_velocity(pos);
                c.distance = glm::length(pos - here);
                if (c.frequency > 0.0 && c.distance < 3.0e5)
                    list.push_back(std::move(c));
            }
        }
        std::sort(list.begin(), list.end(), [](const Contact &a, const Contact &b) { return a.distance < b.distance; });
        return list;
    }

    void NavSystem::publish_receiver(int index, const Contact *c, const Universe &, const std::vector<std::unique_ptr<Ship>> &, Ship &me)
    {
        const std::string base = "/nav/nav[" + std::to_string(index) + "]/";
        sl::PropertyTree &p = *props_;
        p.set_bool(base + "valid", c != nullptr);
        for (const char *key : {"ils", "vtol"})
            p.set_bool(base + key + std::string("/valid"), false);
        if (!c)
        {
            p.set_string(base + "type", "");
            p.set_string(base + "name", "");
            p.set_string(base + "ident", "");
            return;
        }
        p.set_string(base + "type", c->type);
        p.set_string(base + "name", c->name);
        p.set_string(base + "ident", c->ident.empty() ? c->name : c->ident);
        p.set_double(base + "distance", c->distance);

        sl::dvec3 up, east, north;
        me.local_frame(up, east, north);
        const sl::dvec3 here = me.body().position;
        const sl::dvec3 d = c->frame.position - here;
        const sl::dvec3 flat = d - up * glm::dot(d, up);
        p.set_double(base + "bearing", wrap360(std::atan2(glm::dot(flat, east), glm::dot(flat, north)) * deg));
        p.set_double(base + "elevation", std::atan2(glm::dot(d, up), glm::length(flat)) * deg);
        // For an ADF needle: where the station is relative to the nose, positive to the right.
        p.set_double(base + "relative-bearing", wrap180(wrap360(std::atan2(glm::dot(flat, east), glm::dot(flat, north)) * deg) - me.telemetry().heading));
        const sl::dvec3 vrel = me.body().velocity - c->frame.velocity;
        p.set_double(base + "closing-speed", c->distance > 0.0 ? -glm::dot(vrel, d) / c->distance : 0.0);

        if (!c->beacon)
        {
            p.set_double(base + "radial", wrap360(std::atan2(-glm::dot(flat, east), -glm::dot(flat, north)) * deg));
            return;
        }
        // Ship relative to the beacon in the beacon's own east/north/up frame.
        const sl::dvec3 r = here - c->frame.position;
        const double rE = glm::dot(r, c->frame.east), rN = glm::dot(r, c->frame.north), rU = glm::dot(r, c->frame.up);
        const double az = std::atan2(rE, rN) * deg;
        const double horizontal = std::hypot(rE, rN);
        const double elevation = std::atan2(rU, horizontal) * deg;
        p.set_double(base + "radial", wrap360(az));
        const double heading = c->beacon->heading;
        if (c->type == "ils")
        {
            p.set_bool(base + "ils/valid", true);
            p.set_double(base + "ils/course", heading);
            p.set_double(base + "ils/localizer", wrap180(az - (heading + 180.0)));
            p.set_double(base + "ils/glideslope", elevation - c->beacon->glide);
            p.set_double(base + "ils/glide-angle", c->beacon->glide);
        }
        else if (c->type == "vtol")
        {
            const double h = me.telemetry().heading / deg;
            const double sh = std::sin(h), ch = std::cos(h);
            const double vE = glm::dot(vrel, c->frame.east), vN = glm::dot(vrel, c->frame.north), vU = glm::dot(vrel, c->frame.up);
            // The pad's offset from the ship and the ship's velocity over the pad, in the ship's heading frame.
            p.set_bool(base + "vtol/valid", true);
            p.set_double(base + "vtol/x", -rE * ch + rN * sh);
            p.set_double(base + "vtol/y", -rE * sh - rN * ch);
            p.set_double(base + "vtol/vx", vE * ch - vN * sh);
            p.set_double(base + "vtol/vy", vE * sh + vN * ch);
            p.set_double(base + "vtol/height", rU);
            p.set_double(base + "vtol/vertical-speed", vU);
            p.set_double(base + "vtol/pad-heading", wrap180(heading - me.telemetry().heading));
        }
    }

    void NavSystem::publish_docking(const Contact *c, const std::vector<std::unique_ptr<Ship>> &ships, int active)
    {
        sl::PropertyTree &p = *props_;
        Ship &me = *ships[static_cast<std::size_t>(active)];
        const int ports = static_cast<int>(me.spec().ports.size());
        const int own = ports > 0 ? std::clamp(p.get_int("/dock/own-port"), 0, ports - 1) : 0;
        p.set_int("/dock/own-port", own);
        p.set_int("/dock/port-count", ports);
        p.set_string("/dock/own-port-name", ports > 0 ? me.spec().ports[static_cast<std::size_t>(own)].name : "");
        p.set_bool("/dock/own-port-busy", ports > 0 && port_busy(active, own));
        p.set_bool("/dock/docked", docked(active));
        std::string partner;
        for (const Link &l : links_)
            if (l.a == active || l.b == active)
                partner += (partner.empty() ? "" : ", ") + ships[static_cast<std::size_t>(l.a == active ? l.b : l.a)]->name;
        p.set_string("/dock/partner", partner);

        const bool ok = ports > 0 && c && c->type == "ids" && c->ship >= 0 && c->ship < static_cast<int>(ships.size());
        p.set_bool("/dock/valid", ok);
        if (!ok)
            return;
        const PortRelative r = relative_ports(me, own, *ships[static_cast<std::size_t>(c->ship)], c->port);
        p.set_string("/dock/target", c->name);
        p.set_bool("/dock/target-busy", port_busy(c->ship, c->port));
        p.set_double("/dock/range", r.range);
        p.set_double("/dock/x", r.x);
        p.set_double("/dock/y", r.y);
        p.set_double("/dock/z", r.z);
        p.set_double("/dock/vx", r.vx);
        p.set_double("/dock/vy", r.vy);
        p.set_double("/dock/vz", r.vz);
        p.set_double("/dock/closing-speed", -r.vz);
        p.set_double("/dock/yaw", r.yaw);
        p.set_double("/dock/pitch", r.pitch);
        p.set_double("/dock/roll", r.roll);

        // World-frame vectors for the head-up display: the target port relative to our port, its axis, and our velocity relative to it.
        const Ship &other = *ships[static_cast<std::size_t>(c->ship)];
        sl::dvec3 pos_a, dir_a, up_a, pos_b, dir_b, up_b;
        me.port_world(own, pos_a, dir_a, up_a);
        other.port_world(c->port, pos_b, dir_b, up_b);
        const sl::dvec3 rel_v = me.point_velocity(pos_a) - other.point_velocity(pos_b);
        const auto set_vec = [&p](const std::string &path, const sl::dvec3 &v) {
            p.set_double(path + "x", v.x);
            p.set_double(path + "y", v.y);
            p.set_double(path + "z", v.z);
        };
        set_vec("/dock/hud/position-", pos_b - me.origin());
        set_vec("/dock/hud/velocity-", rel_v);
        set_vec("/dock/hud/direction-", dir_b);
        set_vec("/dock/hud/up-", up_b);
    }

    void NavSystem::update(Universe &universe, const std::vector<std::unique_ptr<Ship>> &ships, int active)
    {
        sl::PropertyTree &p = *props_;
        if (p.get_bool("/dock/undock"))
        {
            p.set_bool("/dock/undock", false);
            if (active >= 0 && active < static_cast<int>(ships.size()))
                undock(ships, active, -1);
        }
        if (active < 0 || active >= static_cast<int>(ships.size()))
        {
            p.set_int("/nav/count", 0);
            return;
        }
        Ship &me = *ships[static_cast<std::size_t>(active)];
        const std::vector<Contact> contacts = gather(universe, ships, active);

        const Contact *selected[receivers] = {nullptr, nullptr};
        for (int i = 0; i < receivers; ++i)
        {
            const double f = p.get_double("/nav/nav[" + std::to_string(i) + "]/frequency");
            if (f > 0.0)
                for (const Contact &c : contacts)
                    if (std::fabs(c.frequency - f) < tolerance_for(c.type))
                    {
                        selected[i] = &c;
                        break;
                    }
            publish_receiver(i, selected[i], universe, ships, me);
        }

        const int shown = std::min(static_cast<int>(contacts.size()), listed);
        p.set_int("/nav/count", shown);
        for (int k = 0; k < shown; ++k)
        {
            const std::string base = "/nav/item[" + std::to_string(k) + "]/";
            const Contact &c = contacts[static_cast<std::size_t>(k)];
            p.set_string(base + "type", c.type);
            p.set_string(base + "name", c.name);
            p.set_string(base + "ident", c.ident.empty() ? c.name : c.ident);
            p.set_double(base + "frequency", c.frequency);
            p.set_double(base + "distance", c.distance);
        }

        const int receiver = std::clamp(p.get_int("/dock/receiver"), 0, receivers - 1);
        publish_docking(selected[receiver], ships, active);
    }
} // namespace kosmos
