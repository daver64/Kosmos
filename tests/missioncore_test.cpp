// Tests for KosmosMission's planning core: cmake --build build --target missioncore_test && build/missioncore_test
#include "bodies.h"
#include "budget.h"
#include "mission.h"
#include "json.hpp"
#include "orbit.h"
#include "transfer.h"

#include <cstdio>
#include <string>

using namespace km;

static int failures = 0;
#define CHECK(cond)                                                                \
    do                                                                             \
    {                                                                              \
        if (!(cond))                                                               \
        {                                                                          \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);            \
            ++failures;                                                            \
        }                                                                          \
    } while (0)
#define NEAR(a, b, tol) CHECK(std::fabs((a) - (b)) <= (tol))

int main()
{
    const std::string root = KOSMOS_ROOT;

    // Lambert recovers a propagated orbit's velocities.
    {
        const double mu = 3.986e14;
        const Orbit o = orbit_from_elements(mu, 2e7, 0.3, 0.4, 0.5, 0.6, 0.2);
        Vec r1, v1, r2, v2, va;
        state_at(o, 0.2, r1, v1);
        state_at(o, advance(o, 0.2, 9000), r2, v2);
        const Vec n = o.W;
        const auto v = lambert(r1, r2, 9000, mu, &n, &va);
        CHECK(v.has_value());
        if (v)
        {
            CHECK(length(*v - v1) < 1e-6);
            CHECK(length(va - v2) < 1e-6);
        }
        const auto back = make_orbit(r1, v1, mu);
        CHECK(back && std::fabs(back->a - 2e7) < 1.0);
    }

    // Hohmann LEO -> GEO.
    {
        const Hohmann h = hohmann(3.986004418e14, 6.678e6, 4.2164e7);
        NEAR(h.dv1, 2426, 15);
        NEAR(h.dv2, 1467, 15);
        NEAR(h.tof / 3600, 5.27, 0.05);
    }

    Bodies bodies;
    std::string err;
    CHECK(bodies.load(root + "/data/sol.json", err));
    const Body *earth = bodies.find("Earth"), *mars = bodies.find("Mars"), *moon = bodies.find("Moon");
    CHECK(earth && mars && moon);
    if (!earth || !mars || !moon)
        return 1;

    // Earth -> Mars: the ideal Hohmann estimate from 200 km, and the Lambert optimum agrees to within a few percent.
    {
        const Transfer est = sibling_hohmann(bodies, "Earth", "Mars", earth->radius + 2e5, mars->radius + 3e5, err);
        CHECK(est.valid);
        NEAR(est.vinf_dep, 2945, 60);
        NEAR(est.dv_dep, 3600, 100);
        NEAR(est.tof / DAY, 259, 3);

        LambertProblem p;
        CHECK(sibling_problem(bodies, "Earth", "Mars", earth->radius + 2e5, mars->radius + 3e5, p, err));
        const Porkchop g = porkchop(p, 0, 780 * DAY, 150 * DAY, 250 * DAY, 80, 40);
        CHECK(g.best >= 0);
        const Transfer best = optimise(p, g);
        CHECK(best.valid);
        CHECK(best.total > est.total * 0.9 && best.total < est.total * 1.35);
    }

    // Earth -> Moon from a 200 km orbit: the TLI burn is about 3.1-3.2 km/s.
    {
        const double r = earth->radius + 2e5, v = std::sqrt(earth->gm / r);
        LambertProblem p;
        CHECK(moon_problem(bodies, "Earth", "Moon", {r, 0, 0}, {0, v, 0}, moon->radius + 1e5, p, err));
        const Porkchop g = porkchop(p, 0, 3 * DAY, 2 * DAY, 4 * DAY, 120, 40);
        const Transfer t = optimise(p, g);
        CHECK(t.valid);
        CHECK(t.dv_dep > 2900 && t.dv_dep < 4500);
        CHECK(t.dv_arr > 500 && t.dv_arr < 1200);
    }

    // Moon -> Earth return is analytic.
    {
        const Transfer t = return_to_parent(bodies, "Moon", moon->radius + 1e5, earth->radius + 2e5, err);
        CHECK(t.valid);
        CHECK(t.dv_dep > 600 && t.dv_dep < 1000);
    }

    // Ascent estimate: Earth to low orbit is 9-10 km/s with the loss; Moon is about 1.8-2 km/s.
    {
        const Ascent a = ascent_estimate(*earth, 2e5, 28.5);
        CHECK(a.dv > 9000 && a.dv < 10500);
        const Ascent m = ascent_estimate(*moon, 1e5, 0);
        CHECK(m.dv > 1700 && m.dv < 2100);
        AscentTable t;
        t.set("Foo", "Earth", 9100, "flown");
        const Ascent o = ascent(*earth, 2e5, 28.5, "Foo", t);
        CHECK(o.measured && o.dv == 9100);
        CHECK(!ascent(*earth, 2e5, 28.5, "Bar", t).measured);
    }

    // Budget: a single stage obeys the rocket equation and a payload lowers it.
    {
        ShipSpec s;
        s.name = "Test";
        s.stages.push_back({"first", 1000, 3000, 50000, 300, 280});
        s.stages.push_back({"second", 500, 1500, 20000, 340, 300});
        const Budget b = budget(s, 0);
        NEAR(b.stages[1].dv, 340 * G0 * std::log(2000.0 / 500.0), 0.5);
        NEAR(b.stages[0].dv, 300 * G0 * std::log(6000.0 / 3000.0), 0.5);
        CHECK(budget(s, 500).total < b.total);
        CHECK(budget(s, 0, true).total < b.total);
    }
    {
        const std::vector<ShipSpec> ships = list_ships(root);
        CHECK(!ships.empty());
        for (const ShipSpec &s : ships)
        {
            const Budget b = budget(s, 0);
            CHECK(b.total >= 0 && std::isfinite(b.total));
        }
    }

    // A mission round-trips through its scenario JSON and keeps the ledger honest.
    {
        Mission m;
        m.name = "Test run";
        m.ship = "Kestrel";
        m.start.base = "chippenham";
        m.payload = 250;
        m.legs.push_back({LegType::Ascent, "Ascent", "Earth", "", 9400, 600, 0, 2e5, 0, "rule of thumb"});
        m.legs.push_back({LegType::Transfer, "TMI", "Earth", "Mars", 3600, 259 * DAY, 12.5, 2e5, 0, ""});
        m.legs.push_back({LegType::Arrival, "MOI", "Mars", "", 2100, 0, 271, 3e5, 0, ""});
        NEAR(m.total_dv(), 15100, 1e-9);
        const std::vector<LedgerRow> rows = ledger(m, 15500);
        CHECK(rows.size() == 3 && !rows[0].short_of && rows[2].short_of);

        Mission back;
        CHECK(from_scenario_json(to_scenario_json(m), back, err));
        CHECK(back.name == m.name && back.ship == "Kestrel" && back.start.base == "chippenham");
        CHECK(back.legs.size() == 3 && back.legs[1].type == LegType::Transfer && back.legs[1].to_body == "Mars");
        NEAR(back.legs[1].depart_days, 12.5, 1e-9);
        NEAR(back.payload, 250, 1e-9);
        Mission none;
        CHECK(!from_scenario_json("{\"name\":\"x\",\"ships\":[]}", none, err));
        CHECK(summary_csv(m, 16000).find("Ascent") != std::string::npos);
        CHECK(summary_text(m, 15500).find("SHORT") != std::string::npos);
    }

    // Payload fit: a cylinder bay takes a stack of probes beside nothing it cannot hold, and turns a long item to fit.
    {
        PayloadBay bay;
        bay.name = "Fairing";
        bay.round = true;
        bay.radius = 4.5;
        bay.length = 20;
        PayloadItem big, probe;
        big.label = "Dragonfly";
        big.mass = 11000;
        big.size = {true, true, 7.4, 0, 0, 15};
        probe.label = "Probe";
        probe.mass = 380;
        probe.count = 12;
        probe.size = {true, true, 1.6, 0, 0, 1.8};
        FitReport r = check_fit({bay}, {big, probe});
        CHECK(r.checked && r.ok);
        NEAR(items_mass({big, probe}), 11000 + 12 * 380, 1e-9);
        big.count = 2;
        CHECK(!check_fit({bay}, {big, probe}).ok); // 30 m of Dragonfly in a 20 m bay (lying across is 7.4 m wide, 15 m tall: no)
        PayloadItem wide = big;
        wide.count = 1;
        wide.size = {true, true, 9.5, 0, 0, 3};
        CHECK(!check_fit({bay}, {wide}).ok); // wider than the bay
        PayloadBay box;
        box.name = "Cargo";
        box.round = false;
        box.width = 4;
        box.height = 4;
        box.length = 6;
        PayloadItem crate;
        crate.label = "Crate";
        crate.count = 4;
        crate.size = {true, false, 0, 2, 2, 3};
        CHECK(check_fit({box}, {crate}).ok); // 2x2 across, one layer
        crate.count = 5;
        CHECK(check_fit({box}, {crate}).ok); // a second layer behind (3 + 3 m)
        crate.count = 9;
        CHECK(!check_fit({box}, {crate}).ok);
        PayloadItem heavy = crate;
        heavy.count = 1;
        heavy.mass = 500;
        box.max_mass = 100;
        CHECK(!check_fit({box}, {heavy}).ok);
        CHECK(check_fit({}, {heavy}).problems.size() == 1 && !check_fit({}, {heavy}).checked);
        Mission m;
        m.payload = 100;
        m.payloads = {probe};
        NEAR(m.payload_mass(), 100 + 12 * 380, 1e-9);
        Mission back;
        std::string perr;
        CHECK(from_scenario_json(to_scenario_json(m), back, perr) && back.payloads.size() == 1 && back.payloads[0].count == 12 &&
              back.payloads[0].size.known && back.payloads[0].size.round);
    }

    // Stacks and spawning: a Centaur carrying a Fregat carrying a probe, on a Titan IVA, becomes four docked ships in the scenario.
    {
        const std::vector<ShipSpec> ships = list_ships(root);
        auto find = [&](const std::string &name) -> const ShipSpec * {
            for (const ShipSpec &sp : ships)
                if (sp.name == name)
                    return &sp;
            return nullptr;
        };
        const ShipSpec *titan = find("CVEL Titan IVA"), *centaur = find("CVEL Centaur G"), *fregat = find("CVEL Fregat"), *luna = find("Luna E3");
        CHECK(titan && centaur && fregat && luna); // ships listed from addon.json, not only ship.json
        CHECK(find("CVEL Ariane 5 ECA") != nullptr && find("CVEL Energia") != nullptr);
        if (titan && centaur && fregat && luna)
        {
            CHECK(!titan->bays.empty() && titan->bays[0].port == "Payload");
            const PayloadItem stack = make_payload({*centaur, *fregat, *luna}, 1, true);
            CHECK(stack.members.size() == 3 && stack.members[1].mount_port == 0 && stack.members[0].carry_port == 1);
            NEAR(stack.mass, centaur->mass() + fregat->mass() + luna->mass(), 1e-6);
            CHECK(stack.size.known && stack.size.round && stack.size.diameter >= 2 * 2.1 - 1e-9);
            CHECK(stack.dv > 0 && stack.label == "CVEL Centaur G > CVEL Fregat > Luna E3");
            Mission m;
            m.name = "Stack";
            m.ship = titan->name;
            m.start.base = "kennedy";
            m.payloads = {stack};
            NEAR(powered_dv(m.payloads), stack.dv, 1e-9);
            m.payloads[0].powered = false;
            NEAR(powered_dv(m.payloads), 0, 1e-9);
            const nlohmann::json j = nlohmann::json::parse(to_scenario_json(m, titan));
            CHECK(j["ships"].size() == 4);
            CHECK(j["ships"][1]["dock"]["to"] == 0 && j["ships"][2]["dock"]["to"] == 1 && j["ships"][3]["dock"]["to"] == 2);
            CHECK(j["ships"][2]["dock"]["port"] == 1 && j["ships"][3]["ship"] == "Luna E3");
            Mission back;
            std::string e2;
            CHECK(from_scenario_json(to_scenario_json(m, titan), back, e2) && back.payloads.size() == 1 && back.payloads[0].members.size() == 3);
            // Twelve probes: only the first can take the bay's port, and the scenario says the rest were left out.
            m.payloads = {make_payload({*luna}, 12)};
            const nlohmann::json j2 = nlohmann::json::parse(to_scenario_json(m, titan));
            CHECK(j2["ships"].size() == 2 && j2["description"].get<std::string>().find("11") != std::string::npos);
        }
    }

    std::printf(failures ? "%d failure(s)\n" : "missioncore_test: all passed\n", failures);
    return failures ? 1 : 0;
}
