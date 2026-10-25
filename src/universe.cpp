#include "universe.h"

#include "json.hpp"
#include "vsop87.h"

#include <cmath>
#include <fstream>
#include <iterator>
#include <random>

namespace kosmos
{
    namespace
    {
        constexpr double au = 1.495978707e11;
        constexpr double pi = 3.14159265358979323846;

        double deg(const nlohmann::json &j, const char *key)
        {
            return j.value(key, 0.0) * pi / 180.0;
        }
    } // namespace

    bool Universe::load(const std::string &file, std::string &error)
    {
        std::ifstream in(file);
        if (!in)
        {
            error = "cannot open " + file;
            return false;
        }
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return load_text(text, file, error);
    }

    int Universe::home_id() const
    {
        int id = find(home);
        if (id < 0)
            for (std::size_t i = 0; i < bodies.size() && id < 0; ++i)
                if (!bodies[i].terrain.empty())
                    id = static_cast<int>(i);
        return id;
    }

    int Universe::arrival_body(double &altitude) const
    {
        constexpr double giant_gm = 8.0e15; // about twenty Earth masses
        const int home_body = home_id();
        auto rocky = [&](const Body &b) { return b.kind == BodyKind::planet && b.gm < giant_gm; };
        auto altitude_for = [](const Body &b, double wanted) { return std::min(wanted, b.radius * 0.5); };
        if (home_body >= 0 && rocky(bodies[static_cast<std::size_t>(home_body)]) && bodies[static_cast<std::size_t>(home_body)].atmosphere_density > 0.5)
        {
            altitude = altitude_for(bodies[static_cast<std::size_t>(home_body)], 200.0e3);
            return home_body;
        }
        int best = -1;
        double best_score = 1.0e30;
        for (std::size_t i = 0; i < bodies.size(); ++i)
            if (rocky(bodies[i]) && bodies[i].atmosphere_density > 0.005)
            {
                const double score = std::fabs(std::log(bodies[i].atmosphere_density / 1.225));
                if (score < best_score)
                    best = static_cast<int>(i), best_score = score;
            }
        for (std::size_t i = 0; i < bodies.size() && best < 0; ++i)
            if (rocky(bodies[i]))
                best = static_cast<int>(i);
        if (best >= 0)
        {
            altitude = altitude_for(bodies[static_cast<std::size_t>(best)], 200.0e3);
            return best;
        }
        for (std::size_t i = 0; i < bodies.size(); ++i)
            if (bodies[i].kind == BodyKind::moon && bodies[i].parent >= 0 && bodies[static_cast<std::size_t>(bodies[i].parent)].gm >= giant_gm)
            {
                altitude = altitude_for(bodies[i], 100.0e3);
                return static_cast<int>(i);
            }
        for (std::size_t i = 0; i < bodies.size(); ++i)
            if (bodies[i].kind == BodyKind::planet)
            {
                altitude = bodies[i].radius * 0.5;
                return static_cast<int>(i);
            }
        return -1;
    }

    bool Universe::load_text(const std::string &text, const std::string &file, std::string &error)
    {
        nlohmann::json json;
        try
        {
            json = nlohmann::json::parse(text);
        }
        catch (const std::exception &e)
        {
            error = file + ": " + e.what();
            return false;
        }

        bodies.clear();
        name = json.value("name", "System");
        home = json.value("home", "Earth");
        nlohmann::json list = json.value("bodies", nlohmann::json::array());
        if (const auto belt = json.find("belt"); belt != json.end())
        {
            // A procedural belt of small anonymous rocks: orbit shapes drawn from the given ranges.
            std::mt19937 rng(belt->value("seed", 1));
            auto uniform = [&](double lo, double hi) { return std::uniform_real_distribution<double>(lo, hi)(rng); };
            for (int i = 0; i < belt->value("count", 0); ++i)
            {
                const double r = uniform(belt->value("radius_min", 3000.0), belt->value("radius_max", 40000.0));
                const double grey = uniform(95.0, 150.0);
                char label[32];
                std::snprintf(label, sizeof label, "%s-%03d", belt->value("name", "Belt").c_str(), i + 1);
                list.push_back({{"name", label}, {"kind", "asteroid"}, {"parent", belt->value("parent", "Sun")}, {"minor", true},
                                {"gm", 4.0e-9 * r * r * r}, {"radius", r}, {"spin", uniform(-3e-4, 3e-4)},
                                {"colour", {grey, grey * 0.95, grey * 0.9}},
                                {"orbit", {{"a_au", uniform(belt->value("a_min", 2.1), belt->value("a_max", 3.3))}, {"e", uniform(0.0, belt->value("e_max", 0.2))},
                                           {"i", uniform(0.0, belt->value("i_max", 15.0))}, {"raan", uniform(0.0, 360.0)},
                                           {"argp", uniform(0.0, 360.0)}, {"m0", uniform(0.0, 360.0)}}}});
            }
        }
        for (const nlohmann::json &entry : list)
        {
            Body body;
            body.name = entry.value("name", "");
            const std::string kind = entry.value("kind", "planet");
            body.kind = kind == "star" ? BodyKind::star : kind == "moon" ? BodyKind::moon : kind == "asteroid" ? BodyKind::asteroid
                        : kind == "comet" ? BodyKind::comet : BodyKind::planet;
            body.gm = entry.value("gm", 0.0);
            body.radius = entry.value("radius", 1000.0);
            body.spin = entry.value("spin", 0.0);
            body.terrain = entry.value("terrain", "");
            body.texture = entry.value("texture", "");
            body.minor = entry.value("minor", false);
            body.tilt = entry.value("tilt", 0.0) * pi / 180.0;
            if (const auto r = entry.find("rings"); r != entry.end())
            {
                body.ring_inner = r->value("inner", 0.0);
                body.ring_outer = r->value("outer", 0.0);
                if (r->contains("colour"))
                    for (int i = 0; i < 3; ++i)
                        body.ring_colour[i] = (*r)["colour"][i].get<float>() / 255.0f;
            }
            body.parent = -1;
            if (entry.contains("parent"))
            {
                body.parent = find(entry["parent"].get<std::string>());
                if (body.parent < 0)
                {
                    error = body.name + ": unknown parent " + entry["parent"].get<std::string>();
                    return false;
                }
            }
            if (const auto a = entry.find("atmosphere"); a != entry.end())
            {
                body.atmosphere_density = a->value("density", 0.0);
                body.atmosphere_scale_height = a->value("scale_height", 8500.0);
                if (a->contains("colour"))
                    for (int i = 0; i < 3; ++i)
                        body.atmosphere_colour[i] = (*a)["colour"][i].get<float>() / 255.0f;
                if (const auto c = a->find("clouds"); c != a->end())
                {
                    body.cloud_cover = c->value("cover", 0.0);
                    body.cloud_height = c->value("height", body.cloud_height);
                    if (c->contains("colour"))
                        for (int i = 0; i < 3; ++i)
                            body.cloud_colour[i] = (*c)["colour"][i].get<float>() / 255.0f;
                }
            }
            if (entry.contains("colour"))
                for (int i = 0; i < 3; ++i)
                    body.colour[i] = entry["colour"][i].get<float>() / 255.0f;

            if (body.parent >= 0)
            {
                const nlohmann::json &o = entry.at("orbit");
                body.orbit.semi_major_axis = o.contains("a_au") ? o["a_au"].get<double>() * au : o.value("a_km", 0.0) * 1000.0;
                body.orbit.eccentricity = o.value("e", 0.0);
                body.orbit.inclination = deg(o, "i");
                body.orbit.raan = deg(o, "raan");
                body.orbit.arg_periapsis = deg(o, "argp");
                body.orbit.mean_anomaly = deg(o, "m0");
                const Body &parent = bodies[static_cast<std::size_t>(body.parent)];
                // Planets with a VSOP87 file start from their true position at the epoch instead of the mean elements.
                const std::string code = parent.kind == BodyKind::star ? Vsop87::code_for(body.name) : std::string();
                if (!code.empty() && file.find('/') != std::string::npos)
                {
                    Vsop87 series;
                    if (series.load(file.substr(0, file.rfind('/')) + "/vsop87/VSOP87A." + code))
                    {
                        sl::dvec3 p, v;
                        series.state(json.value("epoch_jd", 2451545.0), p, v);
                        sl::OrbitElements fitted;
                        if (sl::orbit_from_state(parent.gm, p, v, fitted))
                            body.orbit = fitted;
                    }
                }
                body.soi = body.orbit.semi_major_axis * std::pow(body.gm / parent.gm, 0.4);
            }
            if (entry.value("synchronous", false) && body.parent >= 0)
                body.spin = body.orbit.mean_motion(bodies[static_cast<std::size_t>(body.parent)].gm);

            if (!body.terrain.empty())
                body.spin = 0.0; // terrain does not spin with its body yet

            const int id = world.create_body(body.name);
            if (id != static_cast<int>(bodies.size()))
            {
                error = "too many bodies";
                return false;
            }
            sl::FlightBody *fb = world.body(id);
            fb->gm = body.gm;
            fb->radius = body.radius;
            fb->spin_rate = body.terrain.empty() ? body.spin : 0.0;
            fb->atmosphere.surface_density = body.atmosphere_density;
            fb->atmosphere.scale_height = body.atmosphere_scale_height;
            if (body.parent < 0)
                fb->mode = sl::FlightBodyMode::kinematic;
            else
            {
                const sl::FlightBody *parent = world.body(body.parent);
                const sl::OrbitState rel = sl::state_from_orbit(parent->gm, body.orbit);
                fb->position = parent->position + rel.position;
                fb->velocity = parent->velocity + rel.velocity;
                fb->rails_orbit = body.orbit;
                fb->rails_parent = body.parent;
                fb->rails_epoch = 0.0;
                fb->mode = sl::FlightBodyMode::rails;
            }
            bodies.push_back(body);
        }
        if (bodies.empty())
        {
            error = file + ": no bodies";
            return false;
        }
        return true;
    }

    int Universe::find(const std::string &body_name) const
    {
        for (std::size_t i = 0; i < bodies.size(); ++i)
            if (bodies[i].name == body_name)
                return static_cast<int>(i);
        return -1;
    }

    int Universe::soi_body(const sl::dvec3 &position) const
    {
        int best = 0;
        for (std::size_t i = 1; i < bodies.size(); ++i)
            if (bodies[i].kind != BodyKind::star && glm::length(position - world.body(static_cast<int>(i))->position) < bodies[i].soi &&
                bodies[i].soi < bodies[static_cast<std::size_t>(best)].soi)
                best = static_cast<int>(i);
        return best;
    }

    double Universe::rotation_angle(int id) const
    {
        const Body &b = bodies[static_cast<std::size_t>(id)];
        return std::fmod(b.rotation_offset + b.spin * world.time(), 2.0 * pi);
    }

    double Universe::altitude(int id, const sl::dvec3 &position) const
    {
        return glm::length(position - world.body(id)->position) - bodies[static_cast<std::size_t>(id)].radius;
    }

    void Universe::publish(sl::PropertyTree &tree)
    {
        tree.node("/universe/time")->tie_double([this] { return world.time(); });
        tree.node("/universe/name")->set_string(name);
        tree.node("/universe/bodies/count")->set_int(static_cast<int>(bodies.size()));
        for (std::size_t i = 0; i < bodies.size(); ++i)
        {
            const std::string base = "/universe/bodies/body[" + std::to_string(i) + "]/";
            tree.node(base + "name")->set_string(bodies[i].name);
            tree.node(base + "radius")->set_double(bodies[i].radius);
            tree.node(base + "gm")->set_double(bodies[i].gm);
            tree.node(base + "soi")->set_double(bodies[i].soi < 1.0e29 ? bodies[i].soi : 0.0);
            const int id = static_cast<int>(i);
            tree.node(base + "x")->tie_double([this, id] { return world.body(id)->position.x; });
            tree.node(base + "y")->tie_double([this, id] { return world.body(id)->position.y; });
            tree.node(base + "z")->tie_double([this, id] { return world.body(id)->position.z; });
        }
    }
} // namespace kosmos
