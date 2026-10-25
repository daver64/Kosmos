#include "galaxy.h"

#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <random>

namespace kosmos
{
    namespace
    {
        constexpr double pi = 3.14159265358979323846;
        constexpr double earth_radius = 6.371e6, earth_gm = 3.986004418e14, sun_gm = 1.32712440018e20, sun_radius = 6.957e8;

        struct Class
        {
            char letter;
            double weight, mass_lo, mass_hi, temp_lo, temp_hi;
        };
        // Main-sequence classes with their rough share of stars, so the catalogue is mostly red dwarfs as the real sky is.
        const Class classes[] = {{'M', 0.7645, 0.08, 0.45, 2400, 3700}, {'K', 0.121, 0.45, 0.8, 3700, 5200}, {'G', 0.076, 0.8, 1.04, 5200, 6000},
                                 {'F', 0.03, 1.04, 1.4, 6000, 7500},    {'A', 0.006, 1.4, 2.1, 7500, 10000}, {'B', 0.0013, 2.1, 16.0, 10000, 30000},
                                 {'O', 0.00003, 16.0, 60.0, 30000, 50000}};

        struct Rng
        {
            std::mt19937_64 engine;
            explicit Rng(std::uint64_t seed) : engine(seed) {}
            double uniform(double lo = 0.0, double hi = 1.0) { return std::uniform_real_distribution<double>(lo, hi)(engine); }
            double log_uniform(double lo, double hi) { return std::exp(uniform(std::log(lo), std::log(hi))); }
            double normal(double mean, double sigma) { return std::normal_distribution<double>(mean, sigma)(engine); }
            int integer(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(engine); }
            bool chance(double p) { return uniform() < p; }
        };

        std::uint64_t mix(std::uint64_t seed, std::uint64_t index)
        {
            std::uint64_t z = seed + (index + 1) * 0x9E3779B97F4A7C15ull;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            return z ^ (z >> 31);
        }

        // Approximate blackbody colour (Tanner Helland's fit), 0-1.
        void temperature_colour(double kelvin, float *out)
        {
            const double t = std::clamp(kelvin, 1000.0, 40000.0) / 100.0;
            double r, g, b;
            r = t <= 66 ? 255 : 329.698727446 * std::pow(t - 60, -0.1332047592);
            g = t <= 66 ? 99.4708025861 * std::log(t) - 161.1195681661 : 288.1221695283 * std::pow(t - 60, -0.0755148492);
            b = t >= 66 ? 255 : t <= 19 ? 0 : 138.5177312231 * std::log(t - 10) - 305.0447927307;
            out[0] = static_cast<float>(std::clamp(r, 0.0, 255.0) / 255.0);
            out[1] = static_cast<float>(std::clamp(g, 0.0, 255.0) / 255.0);
            out[2] = static_cast<float>(std::clamp(b, 0.0, 255.0) / 255.0);
        }

        Star make_star(Rng &rng, const std::string &name)
        {
            double pick = rng.uniform(0.0, 0.99983), acc = 0.0;
            const Class *cls = &classes[0];
            for (const Class &c : classes)
            {
                acc += c.weight;
                if (pick <= acc)
                {
                    cls = &c;
                    break;
                }
            }
            Star s;
            s.name = name;
            s.spectral_class = cls->letter;
            s.mass = rng.log_uniform(cls->mass_lo, cls->mass_hi);
            s.luminosity = s.mass < 0.43 ? 0.23 * std::pow(s.mass, 2.3) : s.mass < 2.0 ? std::pow(s.mass, 4.0) : 1.4 * std::pow(s.mass, 3.5);
            s.radius = s.mass < 1.0 ? std::pow(s.mass, 0.8) : std::pow(s.mass, 0.57);
            s.temperature = std::clamp(5772.0 * std::pow(s.luminosity / (s.radius * s.radius), 0.25), cls->temp_lo, cls->temp_hi);
            temperature_colour(s.temperature, s.colour);
            return s;
        }

        std::string syllable_name(Rng &rng)
        {
            static const char *pre[] = {"Ar", "Be", "Cor", "Del", "El", "Fa", "Gal", "Hy", "Il", "Jo", "Ka", "Lu", "Mar", "Nor",
                                        "Or", "Pa", "Quil", "Ra", "Sel", "Tor", "Ul", "Vel", "Wyn", "Xan", "Yor", "Zeb"};
            static const char *mid[] = {"a", "e", "i", "o", "u", "ae", "ia", "an", "ar", "en", "or"};
            static const char *end[] = {"n", "s", "th", "x", "r", "ra", "na", "ris", "dor", "lon", "tis", "mus"};
            return std::string(pre[rng.integer(0, 25)]) + mid[rng.integer(0, 10)] + end[rng.integer(0, 11)];
        }

        enum class Kind
        {
            rocky,
            home,
            gas,
            ice
        };

        struct Plan
        {
            double a_au = 1.0;
            Kind kind = Kind::rocky;
        };

        struct Hz
        {
            double centre, frost;
        };

        Hz habitable_zone(const Star &s)
        {
            return {std::sqrt(s.luminosity), 4.2 * std::sqrt(s.luminosity)};
        }

        // The planet slots: spacing grows geometrically, giants sit beyond the frost line, and one slot is always home.
        std::vector<Plan> plan_planets(const Star &star, Rng &rng)
        {
            const Hz hz = habitable_zone(star);
            std::vector<Plan> plans;
            double a = hz.centre * rng.uniform(0.12, 0.3);
            const std::size_t target = static_cast<std::size_t>(star.spectral_class == 'M' ? rng.integer(1, 7) : rng.integer(3, 9));
            while (plans.size() < target)
            {
                plans.push_back({a, Kind::rocky});
                a *= rng.uniform(1.45, 2.1);
            }
            int home = -1;
            double best = 1.0e30;
            for (std::size_t i = 0; i < plans.size(); ++i)
            {
                const double ratio = std::fabs(std::log(plans[i].a_au / hz.centre));
                if (plans[i].a_au > 0.6 * hz.centre && plans[i].a_au < 1.6 * hz.centre && ratio < best)
                {
                    best = ratio;
                    home = static_cast<int>(i);
                }
            }
            if (home < 0)
            {
                const Plan extra{hz.centre * rng.uniform(0.85, 1.2), Kind::home};
                const auto at = std::find_if(plans.begin(), plans.end(), [&](const Plan &p) { return p.a_au > extra.a_au; });
                plans.insert(at, extra);
            }
            else
                plans[static_cast<std::size_t>(home)].kind = Kind::home;
            for (Plan &p : plans)
                if (p.kind == Kind::rocky && p.a_au > hz.frost * 0.9)
                    p.kind = rng.chance(p.a_au > hz.frost * 3.0 ? 0.55 : 0.75) ? Kind::gas : Kind::ice;
            return plans;
        }

        const char *letters = "bcdefghijklmnop";
    } // namespace

    Galaxy::Galaxy(std::uint64_t seed, int local_stars, int distant_stars) : seed_(seed)
    {
        Star sol;
        sol.name = "Sol";
        sol.real = true;
        sol.spectral_class = 'G';
        temperature_colour(5772.0, sol.colour);
        stars_.push_back(sol);

        struct Neighbour
        {
            const char *name;
            double x, y, z;
            char cls;
            double mass, radius, lum, temp;
        };
        // Approximate positions: right distances, plausible directions.
        const Neighbour neighbours[] = {{"Alpha Centauri", -1.64, -1.37, -3.84, 'G', 1.10, 1.22, 1.52, 5790},
                                        {"Proxima Centauri", -1.55, -1.18, -3.77, 'M', 0.12, 0.15, 0.0017, 3042},
                                        {"Barnard's Star", -0.06, -5.94, 0.49, 'M', 0.144, 0.196, 0.0035, 3134},
                                        {"Wolf 359", -7.43, 2.07, 0.72, 'M', 0.11, 0.16, 0.0014, 2800},
                                        {"Sirius", -1.61, 8.07, -2.47, 'A', 2.06, 1.71, 25.4, 9940},
                                        {"Epsilon Eridani", 6.22, 8.36, -1.73, 'K', 0.82, 0.74, 0.34, 5084},
                                        {"Tau Ceti", 10.27, 5.00, -3.27, 'G', 0.78, 0.79, 0.52, 5344},
                                        {"Vega", 17.0, 11.0, 14.0, 'A', 2.1, 2.36, 40.0, 9602},
                                        {"Lalande 21185", -3.43, 7.56, -0.42, 'M', 0.39, 0.39, 0.0194, 3601},
                                        {"Procyon", -9.29, 2.58, -6.2, 'F', 1.50, 2.05, 6.9, 6530},
                                        {"TRAPPIST-1", 8.24, -30.62, 25.51, 'M', 0.0898, 0.1192, 0.000553, 2566},
                                        {"Betelgeuse", -509.25, -85.73, -183.34, 'M', 16.5, 764.0, 126000.0, 3600},
                                        {"Rigel", -679.27, -366.17, -379.63, 'B', 21.0, 78.9, 120000.0, 12100}};
        for (const Neighbour &n : neighbours)
        {
            Star s;
            s.name = n.name;
            s.x = n.x, s.y = n.y, s.z = n.z;
            s.spectral_class = n.cls;
            s.mass = n.mass, s.radius = n.radius, s.luminosity = n.lum, s.temperature = n.temp;
            s.real = true;
            temperature_colour(n.temp, s.colour);
            stars_.push_back(s);
        }

        Rng rng(mix(seed_, 0xC0FFEE));
        char label[64];
        for (int i = 0; i < local_stars; ++i)
        {
            std::snprintf(label, sizeof label, "%s %d", syllable_name(rng).c_str(), static_cast<int>(stars_.size()));
            Star s = make_star(rng, label);
            // Uniform in a flattened bubble about 60 ly across the disc, never on top of Sol.
            const double r = std::max(3.0, 60.0 * std::cbrt(rng.uniform()));
            const double theta = rng.uniform(0.0, 2.0 * pi), phi = std::acos(rng.uniform(-1.0, 1.0));
            s.x = r * std::sin(phi) * std::cos(theta);
            s.y = r * std::cos(phi) * 0.5;
            s.z = r * std::sin(phi) * std::sin(theta);
            stars_.push_back(s);
        }
        for (int i = 0; i < distant_stars; ++i)
        {
            std::snprintf(label, sizeof label, "KG %d", static_cast<int>(stars_.size()));
            Star s = make_star(rng, label);
            double gx, gy, gz;
            if (rng.chance(0.15))
            {
                gx = rng.normal(0.0, 2500.0);
                gy = rng.normal(0.0, 1200.0);
                gz = rng.normal(0.0, 2500.0);
            }
            else
            {
                // Two logarithmic spiral arms with scatter, a thin disc that thickens toward the centre.
                const double r = std::min(-6500.0 * std::log(1.0 - rng.uniform() * 0.97), 55000.0);
                const double theta = (rng.chance(0.5) ? pi : 0.0) + 3.2 * std::log(1.0 + r / 3000.0) + rng.normal(0.0, 0.3);
                gx = r * std::cos(theta);
                gz = r * std::sin(theta);
                gy = rng.normal(0.0, 150.0 + 500.0 * std::exp(-r / 5000.0));
            }
            // Sol sits 26,000 ly from the centre.
            s.x = gx - 26000.0;
            s.y = gy;
            s.z = gz;
            stars_.push_back(s);
        }
    }

    int Galaxy::find(const std::string &name) const
    {
        auto lower = [](std::string s)
        {
            for (char &c : s)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        };
        const std::string wanted = lower(name);
        for (std::size_t i = 0; i < stars_.size(); ++i)
            if (lower(stars_[i].name) == wanted)
                return static_cast<int>(i);
        return -1;
    }

    double Galaxy::distance_ly(int a, int b) const
    {
        const Star &p = star(a), &q = star(b);
        return std::sqrt((p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y) + (p.z - q.z) * (p.z - q.z));
    }

    std::vector<int> Galaxy::nearest(int from, int count) const
    {
        std::vector<int> ids;
        ids.reserve(stars_.size());
        for (int i = 0; i < size(); ++i)
            if (i != from)
                ids.push_back(i);
        count = std::min(count, static_cast<int>(ids.size()));
        std::partial_sort(ids.begin(), ids.begin() + count, ids.end(), [&](int a, int b) { return distance_ly(from, a) < distance_ly(from, b); });
        ids.resize(static_cast<std::size_t>(count));
        return ids;
    }

    std::string Galaxy::system_slug(int index) const
    {
        std::string slug;
        for (const char c : star(index).name)
            slug += std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : '-';
        return slug;
    }

    std::string Galaxy::authored_json(int index) const
    {
        if (systems_dir_.empty() || index <= 0)
            return {};
        std::ifstream file(systems_dir_ + "/" + system_slug(index) + ".json");
        if (!file)
            return {};
        std::string text(std::istreambuf_iterator<char>(file), {});
        // A file that lists only the star gets planets from the generator.
        const nlohmann::json doc = nlohmann::json::parse(text, nullptr, false);
        if (!doc.is_object() || !doc.contains("bodies"))
            return {};
        for (const auto &b : doc["bodies"])
            if (b.value("kind", "") == "planet")
                return text;
        return {};
    }

    int Galaxy::planet_count(int index) const
    {
        if (index == 0)
            return 8;
        if (const std::string text = authored_json(index); !text.empty())
        {
            int count = 0;
            const nlohmann::json doc = nlohmann::json::parse(text, nullptr, false);
            if (doc.is_object() && doc.contains("bodies"))
                for (const auto &b : doc["bodies"])
                    count += b.value("kind", "") == "planet" ? 1 : 0;
            return count;
        }
        Rng rng(mix(seed_, static_cast<std::uint64_t>(index)));
        return static_cast<int>(plan_planets(star(index), rng).size());
    }

    std::string Galaxy::system_json(int index) const
    {
        using nlohmann::json;
        if (std::string text = authored_json(index); !text.empty())
            return text;
        const Star &star_info = star(index);
        Rng rng(mix(seed_, static_cast<std::uint64_t>(index)));
        const std::vector<Plan> plans = plan_planets(star_info, rng);
        const Hz hz = habitable_zone(star_info);
        const double star_gm_earth = star_info.mass * sun_gm / earth_gm;

        auto colour = [](double r, double g, double b) { return json::array({std::round(r), std::round(g), std::round(b)}); };
        auto orbit_angles = [&](json &o, double max_i)
        {
            o["i"] = rng.uniform(0.0, max_i);
            o["raan"] = rng.uniform(0.0, 360.0);
            o["argp"] = rng.uniform(0.0, 360.0);
            o["m0"] = rng.uniform(0.0, 360.0);
        };

        json bodies = json::array();
        bodies.push_back({{"name", star_info.name},
                          {"kind", "star"},
                          {"gm", star_info.mass * sun_gm},
                          {"radius", star_info.radius * sun_radius},
                          {"spin", 2.9e-6},
                          {"colour", colour(star_info.colour[0] * 255.0, star_info.colour[1] * 255.0, star_info.colour[2] * 255.0)}});

        std::string home_name;
        for (std::size_t p = 0; p < plans.size(); ++p)
        {
            const Plan &plan = plans[p];
            const std::string name = star_info.name + " " + letters[p];
            double mass_e, radius_m;
            json planet = {{"name", name}, {"kind", "planet"}, {"parent", star_info.name}};
            switch (plan.kind)
            {
            case Kind::home:
                mass_e = rng.uniform(0.6, 1.5);
                radius_m = earth_radius * std::pow(mass_e, 0.27);
                planet["terrain"] = "earth";
                planet["colour"] = colour(60, 110, 170);
                planet["atmosphere"] = {{"density", 1.225 * rng.uniform(0.8, 1.3)}, {"scale_height", 8500.0 * rng.uniform(0.85, 1.15)}, {"colour", colour(110, 160, 235)}};
                planet["spin"] = rng.uniform(5.0e-5, 1.1e-4);
                home_name = name;
                break;
            case Kind::gas:
            {
                mass_e = rng.log_uniform(15.0, 400.0);
                radius_m = earth_radius * rng.uniform(8.0, 11.5);
                const double warm = rng.uniform();
                planet["colour"] = colour(200 + 40 * warm, 160 + 30 * warm, 110 + 70 * warm);
                planet["atmosphere"] = {{"density", 0.2}, {"scale_height", 27000.0}, {"colour", colour(220, 190, 150)}};
                planet["spin"] = rng.uniform(1.2e-4, 2.0e-4);
                if (rng.chance(0.3))
                    planet["rings"] = {{"inner", radius_m * 1.3}, {"outer", radius_m * rng.uniform(1.9, 2.6)}, {"colour", colour(205, 190, 165)}};
                break;
            }
            case Kind::ice:
                mass_e = rng.log_uniform(8.0, 30.0);
                radius_m = earth_radius * rng.uniform(3.4, 4.6);
                planet["colour"] = colour(90, 160 + 40 * rng.uniform(), 210);
                planet["atmosphere"] = {{"density", 0.15}, {"scale_height", 20000.0}, {"colour", colour(130, 200, 230)}};
                planet["spin"] = rng.uniform(8.0e-5, 1.5e-4);
                if (rng.chance(0.15))
                    planet["rings"] = {{"inner", radius_m * 1.4}, {"outer", radius_m * rng.uniform(1.7, 2.2)}, {"colour", colour(170, 200, 210)}};
                break;
            case Kind::rocky:
            default:
            {
                mass_e = rng.log_uniform(0.05, 2.5);
                radius_m = earth_radius * std::pow(mass_e, 0.27);
                const double grey = rng.uniform(110.0, 170.0);
                planet["colour"] = colour(grey + rng.uniform(0.0, 40.0), grey, grey - rng.uniform(0.0, 30.0));
                const bool inner = plan.a_au < 0.85 * hz.centre;
                if (inner && plan.a_au > 0.5 * hz.centre && rng.chance(0.5))
                {
                    planet["colour"] = colour(225, 190, 120);
                    planet["atmosphere"] = {{"density", rng.uniform(5.0, 60.0)}, {"scale_height", 15000.0}, {"colour", colour(235, 200, 130)}};
                }
                else if (!inner && rng.chance(0.4))
                {
                    planet["colour"] = colour(190, 105, 70);
                    planet["atmosphere"] = {{"density", rng.uniform(0.01, 0.05)}, {"scale_height", 11000.0}, {"colour", colour(210, 150, 110)}};
                }
                planet["spin"] = rng.uniform(-1.0e-4, 1.2e-4);
                break;
            }
            }
            planet["gm"] = earth_gm * mass_e;
            planet["radius"] = radius_m;
            planet["tilt"] = rng.uniform(0.0, 30.0);
            json orbit = {{"a_au", plan.a_au}, {"e", rng.uniform(0.0, plan.kind == Kind::rocky ? 0.15 : 0.08)}};
            orbit_angles(orbit, 4.0);
            planet["orbit"] = orbit;
            bodies.push_back(planet);

            int moons = plan.kind == Kind::home ? (rng.chance(0.7) ? 1 : 0) : plan.kind == Kind::rocky ? (rng.chance(0.3) ? 1 : 0)
                        : plan.kind == Kind::gas ? rng.integer(2, 5) : rng.integer(1, 3);
            // Moons stay well inside the planet's sphere of influence.
            const double soi_m = plan.a_au * 1.495978707e11 * std::pow(mass_e / star_gm_earth, 0.4);
            double a_km = radius_m * rng.uniform(2.6, 6.0) / 1000.0;
            for (int m = 0; m < moons; ++m, a_km *= rng.uniform(1.5, 2.2))
            {
                if (a_km * 1000.0 > 0.25 * soi_m)
                    break;
                const double r_moon = earth_radius * (plan.kind == Kind::home || plan.kind == Kind::rocky ? rng.uniform(0.08, 0.28) : rng.uniform(0.05, 0.4));
                const double moon_mass_e = std::pow(r_moon / earth_radius, 3.0) * 0.6;
                const double grey = rng.uniform(120.0, 185.0);
                json moon = {{"name", name + " " + std::string(1, static_cast<char>('I' + m))},
                             {"kind", "moon"},
                             {"parent", name},
                             {"gm", earth_gm * moon_mass_e},
                             {"radius", r_moon},
                             {"synchronous", true},
                             {"colour", colour(grey, grey * rng.uniform(0.9, 1.0), grey * rng.uniform(0.8, 1.0))}};
                json mo = {{"a_km", a_km}, {"e", rng.uniform(0.0, 0.03)}};
                orbit_angles(mo, 6.0);
                moon["orbit"] = mo;
                bodies.push_back(moon);
            }
        }

        if (rng.chance(0.5))
        {
            const double a = hz.frost * rng.uniform(8.0, 20.0);
            json comet = {{"name", star_info.name + " Comet"}, {"kind", "comet"}, {"parent", star_info.name}, {"gm", 1.0e5}, {"radius", 4.0e3},
                          {"spin", 1.0e-4}, {"colour", colour(120, 120, 125)}};
            json o = {{"a_au", a}, {"e", rng.uniform(0.85, 0.97)}};
            orbit_angles(o, 90.0);
            comet["orbit"] = o;
            bodies.push_back(comet);
        }

        json system = {{"name", star_info.name}, {"home", home_name}, {"bodies", bodies}};
        for (std::size_t p = 0; p + 1 < plans.size(); ++p)
            if (plans[p + 1].a_au / plans[p].a_au > 1.7 && rng.chance(0.6))
            {
                system["belt"] = {{"name", star_info.name + " Belt"}, {"parent", star_info.name}, {"count", 70}, {"seed", static_cast<int>(mix(seed_, static_cast<std::uint64_t>(index)) & 0x7fffffff)},
                                  {"a_min", plans[p].a_au * 1.15}, {"a_max", plans[p + 1].a_au / 1.2}, {"e_max", 0.15}, {"i_max", 12.0}};
                break;
            }
        return system.dump();
    }
} // namespace kosmos
