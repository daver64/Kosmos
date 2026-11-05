#include "bodies.h"

#include <fstream>
#include "json.hpp"

namespace km
{
    bool Bodies::load(const std::string &path, std::string &err)
    {
        std::ifstream in(path);
        if (!in)
        {
            err = "cannot open " + path;
            return false;
        }
        nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
        if (j.is_discarded() || !j.contains("bodies"))
        {
            err = "bad solar system file " + path;
            return false;
        }
        bodies_.clear();
        const double rad = PI / 180.0;
        for (const nlohmann::json &b : j["bodies"])
        {
            Body body;
            body.name = b.value("name", "");
            body.kind = b.value("kind", "");
            body.parent = b.value("parent", "");
            body.gm = b.value("gm", 0.0);
            body.radius = b.value("radius", 0.0);
            body.spin = b.value("spin", 0.0);
            if (b.contains("atmosphere"))
            {
                body.atm_density = b["atmosphere"].value("density", 0.0);
                body.scale_height = b["atmosphere"].value("scale_height", 0.0);
            }
            if (b.contains("orbit"))
            {
                const nlohmann::json &o = b["orbit"];
                body.has_orbit = true;
                body.a = o.contains("a_au") ? o["a_au"].get<double>() * AU : o.value("a_km", 0.0) * 1000.0;
                body.e = o.value("e", 0.0);
                body.i = o.value("i", 0.0) * rad;
                body.raan = o.value("raan", 0.0) * rad;
                body.argp = o.value("argp", 0.0) * rad;
                body.m0 = o.value("m0", 0.0) * rad;
            }
            if (!body.name.empty())
                bodies_.push_back(body);
        }
        return !bodies_.empty();
    }

    const Body *Bodies::find(const std::string &name) const
    {
        for (const Body &b : bodies_)
            if (b.name == name)
                return &b;
        return nullptr;
    }

    Orbit Bodies::orbit_of(const Body &b) const
    {
        const Body *parent = find(b.parent);
        Orbit o = orbit_from_elements(parent ? parent->gm : 0, b.a, b.e, b.i, b.raan, b.argp, 0);
        o.nu = nu_from_mean(o, b.m0);
        return o;
    }

    void Bodies::state(const Body &b, double t, Vec &r, Vec &v) const
    {
        r = v = Vec{};
        if (!b.has_orbit)
            return;
        const Orbit o = orbit_of(b);
        state_at(o, advance(o, o.nu, t), r, v);
    }
} // namespace km
