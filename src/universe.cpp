#include "universe.h"

#include "json.hpp"
#include "vsop87.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
        const double file_epoch = json.value("epoch_jd", 2451545.0);
        epoch_jd = epoch_override > 0.0 ? epoch_override : file_epoch;
        const double elapsed_days = epoch_jd - file_epoch;
        bool vsop_loaded = false;
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
            if (const auto st = entry.find("style"); st != entry.end() && st->is_object())
            {
                body.style.type = archetype_from_name(st->value("type", ""));
                body.style.seed = st->value("seed", 1);
                body.style.ocean = st->value("ocean", 0.6f);
                body.style.ice = st->value("ice", 0.15f);
                body.style.relief = st->value("relief", 1.0f);
                body.style.bands = st->value("bands", 9);
                body.style.contrast = st->value("contrast", 0.5f);
                if (const auto tint = st->find("tint"); tint != st->end() && tint->is_array() && tint->size() >= 3)
                    for (int i = 0; i < 3; ++i)
                        body.style.tint[i] = (*tint)[static_cast<std::size_t>(i)].get<float>();
            }
            body.detail = entry.value("detail", "");
            body.detail_tile = entry.value("detail_tile", 128.0);
            body.detail_strength = entry.value("detail_strength", 0.9);
            body.texture = entry.value("texture", "");
            body.tiles = entry.value("tiles", "");
            body.mesh = entry.value("mesh", "");
            body.minor = entry.value("minor", false);
            body.tilt = entry.value("tilt", 0.0) * pi / 180.0;
            if (const auto r = entry.find("epoch_rates"); r != entry.end())
            {
                body.has_rates = true;
                body.raan_rate = r->value("raan", 0.0);
                body.argp_rate = r->value("argp", 0.0);
                body.mean_longitude_rate = r->value("mean_longitude", 0.0);
            }
            if (const auto r = entry.find("pole"); r != entry.end())
            {
                body.has_pole = true;
                body.pole_ra = r->value("ra", 0.0);
                body.pole_dec = r->value("dec", 90.0);
                const sl::dvec3 pole = equatorial_to_world(body.pole_ra, body.pole_dec);
                const sl::dvec3 axis = glm::cross(sl::dvec3(0.0, 1.0, 0.0), pole);
                if (glm::length(axis) > 1.0e-12)
                    body.axis_tilt = glm::angleAxis(std::acos(std::clamp(pole.y, -1.0, 1.0)), glm::normalize(axis));
            }
            if (const auto r = entry.find("meridian"); r != entry.end())
            {
                body.has_meridian = true;
                body.meridian_w0 = r->value("w0", 0.0);
                body.meridian_rate = r->value("rate", 0.0);
                body.meridian_node_ra = r->value("node_ra", 0.0);
            }
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
                bool fitted_to_epoch = false;
                // Planets with a VSOP87 file start from their true position at the epoch instead of the mean elements.
                const std::string code = parent.kind == BodyKind::star ? Vsop87::code_for(body.name) : std::string();
                if (!code.empty() && file.find('/') != std::string::npos)
                {
                    Vsop87 series;
                    if (series.load(file.substr(0, file.rfind('/')) + "/vsop87/VSOP87A." + code))
                    {
                        sl::dvec3 p, v;
                        series.state(epoch_jd, p, v);
                        sl::OrbitElements fitted;
                        if (sl::orbit_from_state(parent.gm, p, v, fitted))
                        {
                            body.orbit = fitted;
                            fitted_to_epoch = true;
                            vsop_loaded = true;
                        }
                    }
                }
                // Everything else carries its mean elements forward (or back) from the file's epoch to the start date.
                if (!fitted_to_epoch && elapsed_days != 0.0)
                {
                    const double to_rad = pi / 180.0;
                    if (body.has_rates)
                    {
                        body.orbit.raan += body.raan_rate * elapsed_days * to_rad;
                        body.orbit.arg_periapsis += body.argp_rate * elapsed_days * to_rad;
                        body.orbit.mean_anomaly += (body.mean_longitude_rate - body.raan_rate - body.argp_rate) * elapsed_days * to_rad;
                    }
                    else
                        body.orbit.mean_anomaly += body.orbit.mean_motion(parent.gm) * elapsed_days * 86400.0;
                    body.orbit.mean_anomaly = std::fmod(body.orbit.mean_anomaly, 2.0 * pi);
                }
                body.soi = body.orbit.semi_major_axis * std::pow(body.gm / parent.gm, 0.4);
            }
            if (entry.value("synchronous", false) && body.parent >= 0)
                body.spin = body.orbit.mean_motion(bodies[static_cast<std::size_t>(body.parent)].gm);
            // A spinning body started on another date has turned by that many rotations since the file's epoch.
            if (epoch_override > 0.0 && body.terrain.empty())
                body.rotation_offset = std::fmod(body.spin * elapsed_days * 86400.0, 2.0 * pi);

            const int id = world.create_body(body.name);
            if (id != static_cast<int>(bodies.size()))
            {
                error = "too many bodies";
                return false;
            }
            sl::FlightBody *fb = world.body(id);
            fb->gm = body.gm;
            fb->radius = body.radius;
            fb->spin_rate = body.spin;
            fb->spin_axis = body.axis_tilt * sl::dvec3(0.0, 1.0, 0.0);
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
        if (epoch_override > 0.0)
            orient_spinning_planets();
        return true;
    }

    // With a start date the prime meridian of each body that has one is put where it really was: W is measured from the
    // node of the body's equator on the J2000 equator (right ascension node_ra), eastward about the pole.
    void Universe::orient_spinning_planets()
    {
        const double days = epoch_jd - 2451545.0;
        for (std::size_t i = 0; i < bodies.size(); ++i)
        {
            Body &b = bodies[i];
            if (!b.has_meridian)
                continue;
            const sl::dvec3 pole = world.body(static_cast<int>(i))->spin_axis;
            const sl::dvec3 node = equatorial_to_world(b.meridian_node_ra, 0.0);
            const double w = std::fmod(b.meridian_w0 + b.meridian_rate * days, 360.0) * pi / 180.0;
            const sl::dvec3 target = glm::angleAxis(w, pole) * node;
            const sl::dvec3 lon0 = b.axis_tilt * sl::dvec3(1.0, 0.0, 0.0);
            b.rotation_offset = std::fmod(std::atan2(glm::dot(pole, glm::cross(lon0, target)), glm::dot(lon0, target)), 2.0 * pi);
        }
    }

    sl::dvec3 equatorial_to_world(double ra_degrees, double dec_degrees)
    {
        const double ra = ra_degrees * pi / 180.0, dec = dec_degrees * pi / 180.0;
        const double obliquity = 23.4392911 * pi / 180.0;
        const double x = std::cos(dec) * std::cos(ra), y = std::cos(dec) * std::sin(ra), z = std::sin(dec);
        const double ex = x, ey = y * std::cos(obliquity) + z * std::sin(obliquity), ez = -y * std::sin(obliquity) + z * std::cos(obliquity);
        return {ex, ez, -ey};
    }

    double julian_from_civil(int year, int month, int day, double hours)
    {
        if (month <= 2)
        {
            --year;
            month += 12;
        }
        const int a = static_cast<int>(std::floor(year / 100.0));
        const int b = 2 - a + static_cast<int>(std::floor(a / 4.0));
        return std::floor(365.25 * (year + 4716)) + std::floor(30.6001 * (month + 1)) + day + b - 1524.5 + hours / 24.0;
    }

    void civil_from_julian(double jd, int &year, int &month, int &day, double &hours)
    {
        const double j = jd + 0.5;
        const double z = std::floor(j);
        const double f = j - z;
        const double alpha = std::floor((z - 1867216.25) / 36524.25);
        const double a = z + 1.0 + alpha - std::floor(alpha / 4.0);
        const double b = a + 1524.0;
        const double c = std::floor((b - 122.1) / 365.25);
        const double d = std::floor(365.25 * c);
        const double e = std::floor((b - d) / 30.6001);
        day = static_cast<int>(b - d - std::floor(30.6001 * e));
        month = static_cast<int>(e < 14.0 ? e - 1.0 : e - 13.0);
        year = static_cast<int>(month > 2 ? c - 4716.0 : c - 4715.0);
        hours = f * 24.0;
    }

    bool parse_epoch(const std::string &text, double &julian_date)
    {
        const char *begin = text.c_str();
        while (*begin == ' ' || *begin == '\t')
            ++begin;
        if (!*begin)
            return false;
        char *end = nullptr;
        const double number = std::strtod(begin, &end);
        while (end && (*end == ' ' || *end == '\t'))
            ++end;
        if (end != begin && end && !*end)
        {
            julian_date = number + mjd_offset;
            return true;
        }
        int year = 0, month = 0, day = 0, used = 0;
        if (std::sscanf(begin, "%d-%d-%d%n", &year, &month, &day, &used) < 3 || month < 1 || month > 12 || day < 1 || day > 31)
            return false;
        const char *p = begin + used;
        double hours = 0.0;
        if (*p == ' ' || *p == 'T' || *p == 't')
        {
            while (*p == ' ' || *p == 'T' || *p == 't')
                ++p;
            int h = 0, m = 0, n = 0;
            double sec = 0.0;
            if (std::sscanf(p, "%d:%d%n", &h, &m, &n) == 2)
            {
                p += n;
                if (*p == ':' && std::sscanf(p + 1, "%lf%n", &sec, &n) == 1)
                    p += 1 + n;
                if (h < 0 || h > 24 || m < 0 || m > 59 || sec < 0.0 || sec >= 61.0)
                    return false;
                hours = h + m / 60.0 + sec / 3600.0;
            }
        }
        while (*p == ' ')
            ++p;
        double offset_hours = 0.0;
        if (*p == '+' || *p == '-')
        {
            const double sign = *p == '-' ? -1.0 : 1.0;
            int oh = 0, om = 0;
            const int got = std::sscanf(p + 1, "%d:%d", &oh, &om);
            if (got < 1)
                return false;
            offset_hours = sign * (oh + om / 60.0);
            p += std::strlen(p);
        }
        else
        {
            std::string tail(p);
            for (char &c : tail)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (!tail.empty() && tail != "Z" && tail != "UTC" && tail != "GMT")
                return false;
        }
        julian_date = julian_from_civil(year, month, day, hours - offset_hours);
        return true;
    }

    std::string format_utc(double jd)
    {
        const double rounded = std::round(jd * 86400.0) / 86400.0;
        int y, mo, d;
        double hours;
        civil_from_julian(rounded + 1.0e-9, y, mo, d, hours);
        const int seconds = static_cast<int>(hours * 3600.0);
        char buffer[48];
        std::snprintf(buffer, sizeof buffer, "%04d-%02d-%02d %02d:%02d:%02d GMT", y, mo, d, seconds / 3600, seconds / 60 % 60, seconds % 60);
        return buffer;
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

    sl::dquat Universe::orientation(int id) const
    {
        return bodies[static_cast<std::size_t>(id)].axis_tilt * glm::angleAxis(rotation_angle(id), sl::dvec3(0.0, 1.0, 0.0));
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
        tree.node("/universe/mjd")->tie_double([this] { return julian_date() - mjd_offset; });
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
            tree.node(base + "vx")->tie_double([this, id] { return world.body(id)->velocity.x; });
            tree.node(base + "vy")->tie_double([this, id] { return world.body(id)->velocity.y; });
            tree.node(base + "vz")->tie_double([this, id] { return world.body(id)->velocity.z; });
            tree.node(base + "parent")->set_string(bodies[i].parent >= 0 ? bodies[static_cast<std::size_t>(bodies[i].parent)].name : std::string());
        }
    }
} // namespace kosmos
