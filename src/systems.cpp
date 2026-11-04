#include <sstream>
#include <fstream>
#include "systems.h"

#include <map>

#include "ship.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>

namespace kosmos
{
    namespace
    {
        constexpr double gas_constant = 8.314462;
        constexpr double molar_o2 = 0.032, molar_n2 = 0.028, molar_co2 = 0.044;
        constexpr double solar_constant = 1361.0;
        constexpr double energy_per_kg_fuel = 8.2e13;     // fission or fusion fuel, J/kg of fuel burned
        constexpr double fuel_cell_j_per_kg_h2 = 7.2e7;   // usable electricity per kg of hydrogen
        constexpr double electrolysis_j_per_kg_water = 2.0e7;
        constexpr double crew_o2_kg_per_s = 0.84 / 86400.0;
        constexpr double crew_co2_kg_per_s = 1.0 / 86400.0;
        constexpr double crew_water_kg_per_s = 3.0 / 86400.0;
        constexpr double regulator_kg_per_s = 0.02;

        sl::dvec3 vec3(const nlohmann::json &j, sl::dvec3 fallback)
        {
            if (!j.is_array() || j.size() < 3)
                return fallback;
            return {j[0].get<double>(), j[1].get<double>(), j[2].get<double>()};
        }

        int resolve_stage(const nlohmann::json &j, const std::vector<std::string> &names)
        {
            const int last = std::max(0, static_cast<int>(names.size()) - 1);
            if (!j.contains("stage"))
                return last;
            const nlohmann::json &s = j["stage"];
            if (s.is_number_integer())
                return std::clamp(s.get<int>(), 0, last);
            if (s.is_string())
                for (std::size_t i = 0; i < names.size(); ++i)
                    if (names[i] == s.get<std::string>())
                        return static_cast<int>(i);
            return last;
        }

        std::string upper(std::string text)
        {
            for (char &c : text)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return text;
        }

        double pressure_to_mass(double kpa, double volume, double temperature, double molar)
        {
            return kpa * 1000.0 * volume * molar / (gas_constant * temperature);
        }

        double mass_to_pressure(double kg, double volume, double temperature, double molar)
        {
            return volume > 0.0 ? kg / molar * gas_constant * temperature / volume / 1000.0 : 0.0;
        }
    } // namespace

    LifeSupportMode parse_life_support(const std::string &text, LifeSupportMode fallback)
    {
        if (text == "off")
            return LifeSupportMode::off;
        if (text == "warn")
            return LifeSupportMode::warn;
        if (text == "fatal")
            return LifeSupportMode::fatal;
        return fallback;
    }

    const char *life_support_name(LifeSupportMode mode)
    {
        switch (mode)
        {
        case LifeSupportMode::off:
            return "off";
        case LifeSupportMode::fatal:
            return "fatal";
        default:
            return "warn";
        }
    }

    void parse_systems(const nlohmann::json &json, const std::vector<std::string> &stage_names, SystemsSpec &out)
    {
        for (const nlohmann::json &jc : json.value("systems", nlohmann::json::array()))
        {
            SystemComponent c;
            c.type = jc.value("type", "");
            if (c.type.empty())
                continue;
            c.name = jc.value("name", c.type);
            c.stage = resolve_stage(jc, stage_names);
            c.gas = jc.value("gas", "");
            c.vessel = jc.value("vessel", "");
            c.watts = jc.value("watts", 0.0);
            c.max_w = jc.value("max_w", 0.0);
            c.area = jc.value("area_m2", 0.0);
            c.efficiency = jc.value("efficiency", c.efficiency);
            c.half_life_years = jc.value("half_life_years", c.half_life_years);
            c.age_years = jc.value("age_years", 0.0);
            c.kg_per_day = jc.value("kg_per_day", 0.0);
            c.n2_kg_per_day = jc.value("n2_kg_per_day", 0.0);
            c.regen_kg_per_day = jc.value("regen_kg_per_day", 0.0);
            c.site = jc.value("site", false);
            c.wear_hours = jc.value("wear_hours", 0.0);
            c.wear = jc.value("wear", 0.0);
            c.enabled = jc.value("enabled", c.type != "electrolyser" && c.type != "isru" && c.type != "refinery");
            c.priority = jc.value("priority", (c.type == "regulator" || c.type == "scrubber") ? 0 : 1);
            c.facing = vec3(jc.value("facing", nlohmann::json()), c.facing);
            c.tracking = jc.value("tracking", false);
            if (c.type == "battery")
            {
                c.capacity = jc.value("capacity_wh", 1000.0);
                c.amount = c.capacity * std::clamp(jc.value("charge", 1.0), 0.0, 1.0);
                if (c.max_w <= 0.0)
                    c.max_w = c.capacity; // a one-hour rate unless told otherwise
            }
            else if (c.type == "tank")
            {
                c.capacity = jc.value("capacity_kg", 10.0);
                c.amount = jc.value("kg", c.capacity);
            }
            else if (c.type == "fission" || c.type == "fusion")
            {
                c.capacity = c.amount = jc.value("fuel_kg", 10.0);
            }
            else if (c.type == "scrubber")
            {
                c.capacity = c.amount = jc.value("sorbent_kg", 1e9);
                if (c.kg_per_day <= 0.0)
                    c.kg_per_day = 6.0;
                if (c.watts <= 0.0)
                    c.watts = 60.0;
            }
            else if (c.type == "regulator" && c.watts <= 0.0)
                c.watts = 20.0;
            out.components.push_back(std::move(c));
        }
        for (const nlohmann::json &jv : json.value("vessels", nlohmann::json::array()))
        {
            VesselSpec v;
            v.name = jv.value("name", v.name);
            v.stage = resolve_stage(jv, stage_names);
            v.centre = vec3(jv.value("position", nlohmann::json()), v.centre);
            v.size = vec3(jv.value("size", nlohmann::json()), v.size);
            v.crew = jv.value("crew", 0);
            v.pressure_kpa = jv.value("pressure_kpa", v.pressure_kpa);
            v.o2_fraction = jv.value("o2_fraction", v.o2_fraction);
            v.co2_kpa = jv.value("co2_kpa", 0.0);
            v.temperature_k = jv.value("temperature_k", v.temperature_k);
            for (const nlohmann::json &jc : jv.value("crew_at", nlohmann::json::array()))
                v.crew_at.push_back(vec3(jc, {0.5, 0.5, 0.5}));
            for (const nlohmann::json &jt : jv.value("vents", nlohmann::json::array()))
            {
                VesselSpec::Vent vent;
                vent.at = vec3(jt.value("at", nlohmann::json()), vent.at);
                vent.area_cm2 = jt.value("area_cm2", 0.0);
                v.vents.push_back(vent);
            }
            out.vessels.push_back(std::move(v));
        }
    }

    double Systems::Vessel::pressure_kpa() const
    {
        return mass_to_pressure(o2, volume, spec.temperature_k, molar_o2) + mass_to_pressure(n2, volume, spec.temperature_k, molar_n2) +
               mass_to_pressure(co2, volume, spec.temperature_k, molar_co2);
    }
    double Systems::Vessel::ppo2_kpa() const { return mass_to_pressure(o2, volume, spec.temperature_k, molar_o2); }
    double Systems::Vessel::ppco2_kpa() const { return mass_to_pressure(co2, volume, spec.temperature_k, molar_co2); }

    namespace
    {
        void grid_cell_for(const sl::dvec3 &unit, int n, int &x, int &y, int &z)
        {
            x = std::clamp(static_cast<int>(std::lround(unit.x * (n - 1))), 1, n - 2);
            y = std::clamp(static_cast<int>(std::lround(unit.y * (n - 1))), 1, n - 2);
            z = std::clamp(static_cast<int>(std::lround(unit.z * (n - 1))), 1, n - 2);
        }

        // Vents sit in the hull layer: move the cell to the nearest face of the cube.
        void hull_cell_for(const sl::dvec3 &unit, int n, int &x, int &y, int &z)
        {
            x = std::clamp(static_cast<int>(std::lround(unit.x * (n - 1))), 0, n - 1);
            y = std::clamp(static_cast<int>(std::lround(unit.y * (n - 1))), 0, n - 1);
            z = std::clamp(static_cast<int>(std::lround(unit.z * (n - 1))), 0, n - 1);
            int *c[3] = {&x, &y, &z};
            int best = 0, depth = n;
            for (int i = 0; i < 3; ++i)
            {
                const int d = std::min(*c[i], n - 1 - *c[i]);
                if (d < depth)
                    depth = d, best = i;
            }
            *c[best] = *c[best] < n / 2 ? 0 : n - 1;
        }
    } // namespace

    Systems::Systems(const SystemsSpec &spec, LifeSupportMode mode) : components_(spec.components), mode_(mode)
    {
        for (const VesselSpec &vs : spec.vessels)
        {
            Vessel v;
            v.spec = vs;
            v.volume = std::max(vs.size.x * vs.size.y * vs.size.z, 0.01);
            const double o2_kpa = vs.pressure_kpa * vs.o2_fraction;
            const double n2_kpa = std::max(vs.pressure_kpa - o2_kpa - vs.co2_kpa, 0.0);
            v.o2 = pressure_to_mass(o2_kpa, v.volume, vs.temperature_k, molar_o2);
            v.n2 = pressure_to_mass(n2_kpa, v.volume, vs.temperature_k, molar_n2);
            v.co2 = pressure_to_mass(vs.co2_kpa, v.volume, vs.temperature_k, molar_co2);
            if (v.spec.crew_at.empty())
                for (int i = 0; i < (v.spec.crew > 0 ? v.spec.crew : 2); ++i)
                    v.spec.crew_at.push_back({0.3 + 0.4 * ((i % 3) / 2.0), 0.35, 0.3 + 0.4 * ((i / 3 % 3) / 2.0)});
            v.grid = sl::create_gas_vessel(12);
            if (v.grid)
            {
                v.grid->cell_volume = static_cast<float>(v.volume / 1000.0);
                sl::gas_vessel_fill(v.grid, sl::VesselGas::Oxygen, static_cast<float>(v.o2));
                sl::gas_vessel_fill(v.grid, sl::VesselGas::Nitrogen, static_cast<float>(v.n2));
                sl::gas_vessel_fill(v.grid, sl::VesselGas::CarbonDioxide, static_cast<float>(v.co2));
            }
            for (const VesselSpec::Vent &vent : v.spec.vents)
                if (vent.area_cm2 > 0.0)
                {
                    v.leak_k += vent.area_cm2 * 1e-4 * 170.0 / v.volume;
                    if (v.grid)
                    {
                        int x, y, z;
                        hull_cell_for(vent.at, v.grid->size, x, y, z);
                        sl::gas_vessel_set_vent(v.grid, x, y, z, 0.4f);
                    }
                }
            vessels_.push_back(std::move(v));
        }
    }

    Systems::~Systems()
    {
        for (Vessel &v : vessels_)
            sl::destroy_gas_vessel(v.grid);
    }

    SystemComponent *Systems::find_tank(const std::string &gas)
    {
        for (SystemComponent &c : components_)
            if (c.alive && c.type == "tank" && c.gas == gas)
                return &c;
        return nullptr;
    }

    double Systems::refill_from(Systems &donor, double kg)
    {
        double moved = 0.0;
        for (SystemComponent &c : components_)
        {
            const bool tank = c.type == "tank", sorbent = c.type == "scrubber" && c.capacity < 1e8;
            if (!c.alive || (!tank && !sorbent))
                continue;
            for (SystemComponent &d : donor.components_)
            {
                if (!d.alive || d.type != c.type || d.gas != c.gas || d.amount <= 0.0)
                    continue;
                const double n = std::min({kg - moved, d.amount, c.capacity - c.amount});
                if (n <= 0.0)
                    continue;
                d.amount -= n;
                c.amount += n;
                moved += n;
            }
        }
        return moved;
    }

    void Systems::share_with(Systems &other, double dt, double power_w, double kg_per_s)
    {
        auto key = [](const SystemComponent &c) {
            if (c.type == "battery")
                return std::string("battery");
            if (c.type == "tank")
                return "tank:" + c.gas;
            if (c.type == "scrubber" && c.capacity < 1e8)
                return std::string("sorbent");
            return std::string();
        };
        std::map<std::string, int> kinds;
        for (const SystemComponent &c : components_)
            if (c.alive && !key(c).empty())
                kinds[key(c)] = 1;
        for (const auto &kind : kinds)
        {
            const std::string &k = kind.first;
            double amount[2] = {0, 0}, capacity[2] = {0, 0};
            Systems *side[2] = {this, &other};
            for (int i = 0; i < 2; ++i)
                for (const SystemComponent &c : side[i]->components_)
                    if (c.alive && key(c) == k)
                    {
                        amount[i] += c.amount;
                        capacity[i] += c.capacity;
                    }
            if (capacity[0] <= 0.0 || capacity[1] <= 0.0)
                continue;
            const double f0 = amount[0] / capacity[0], f1 = amount[1] / capacity[1];
            const int from = f0 > f1 ? 0 : 1, to = 1 - from;
            // Moving x lowers the donor's fraction and raises the receiver's equally when x = (f_from - f_to) / (1/C_from + 1/C_to).
            double x = std::fabs(f0 - f1) / (1.0 / capacity[from] + 1.0 / capacity[to]);
            x = std::min(x, (k == "battery" ? power_w / 3600.0 : kg_per_s) * dt);
            for (int pass = 0; pass < 2; ++pass)
            {
                const int i = pass == 0 ? from : to;
                double left = x;
                for (SystemComponent &c : side[i]->components_)
                {
                    if (!c.alive || key(c) != k || left <= 0.0)
                        continue;
                    const double room = pass == 0 ? c.amount : c.capacity - c.amount;
                    const double n = std::min(left, room);
                    c.amount += pass == 0 ? -n : n;
                    left -= n;
                }
            }
        }
    }

    double Systems::take(const std::string &gas, double kg)
    {
        double got = 0.0;
        for (SystemComponent &c : components_)
            if (c.alive && c.type == "tank" && c.gas == gas && got < kg)
            {
                const double t = std::min(c.amount, kg - got);
                c.amount -= t;
                got += t;
            }
        return got;
    }

    double Systems::give(const std::string &gas, double kg)
    {
        double put = 0.0;
        for (SystemComponent &c : components_)
            if (c.alive && c.type == "tank" && c.gas == gas && put < kg)
            {
                const double t = std::min(c.capacity - c.amount, kg - put);
                c.amount += t;
                put += t;
            }
        return put;
    }

    double Systems::tank_kg(const std::string &gas) const
    {
        double sum = 0.0;
        for (const SystemComponent &c : components_)
            if (c.alive && c.type == "tank" && c.gas == gas)
                sum += c.amount;
        return sum;
    }

    double Systems::battery_wh() const
    {
        double sum = 0.0;
        for (const SystemComponent &c : components_)
            if (c.alive && c.type == "battery")
                sum += c.amount;
        return sum;
    }

    double Systems::battery_capacity_wh() const
    {
        double sum = 0.0;
        for (const SystemComponent &c : components_)
            if (c.alive && c.type == "battery")
                sum += c.capacity;
        return sum;
    }

    void Systems::drop_stage(int stage)
    {
        for (SystemComponent &c : components_)
            if (c.stage == stage)
                c.alive = false, c.output_w = 0.0;
        for (Vessel &v : vessels_)
            if (v.spec.stage == stage && v.alive)
            {
                v.alive = false;
                sl::destroy_gas_vessel(v.grid);
                v.grid = nullptr;
            }
    }

    Systems::State Systems::state() const
    {
        State s;
        s.components.reserve(components_.size());
        for (const SystemComponent &c : components_)
            s.components.push_back({c.amount, c.age_years, c.wear, c.enabled, c.alive});
        s.vessels.reserve(vessels_.size());
        for (const Vessel &v : vessels_)
            s.vessels.push_back({v.o2, v.n2, v.co2, v.leak_k, v.health});
        return s;
    }

    void Systems::restore(const State &state)
    {
        if (state.components.size() != components_.size() || state.vessels.size() != vessels_.size())
            return;
        for (std::size_t i = 0; i < components_.size(); ++i)
        {
            SystemComponent &c = components_[i];
            const State::Component &saved = state.components[i];
            c.amount = c.capacity > 0.0 ? std::clamp(saved.amount, 0.0, c.capacity) : saved.amount;
            c.age_years = saved.age_years;
            c.wear = saved.wear;
            c.enabled = saved.enabled;
            if (!saved.alive)
                c.alive = false, c.output_w = 0.0;
        }
        for (std::size_t i = 0; i < vessels_.size(); ++i)
        {
            Vessel &v = vessels_[i];
            const State::VesselLevels &saved = state.vessels[i];
            v.o2 = saved.o2;
            v.n2 = saved.n2;
            v.co2 = saved.co2;
            v.leak_k = saved.leak_k;
            v.health = saved.health;
        }
    }

    void Systems::set_tank_fraction(const std::string &gas, double fraction)
    {
        for (SystemComponent &c : components_)
            if (c.alive && c.type == "tank" && c.gas == gas)
                c.amount = c.capacity * std::clamp(fraction, 0.0, 1.0);
    }

    void Systems::set_battery_fraction(double fraction)
    {
        for (SystemComponent &c : components_)
            if (c.alive && c.type == "battery")
                c.amount = c.capacity * std::clamp(fraction, 0.0, 1.0);
    }

    bool Systems::breach(int index, double area_cm2, const sl::dvec3 &at)
    {
        if (index < 0 || index >= static_cast<int>(vessels_.size()) || !vessels_[static_cast<std::size_t>(index)].alive)
            return false;
        Vessel &v = vessels_[static_cast<std::size_t>(index)];
        v.spec.vents.push_back({at, area_cm2});
        v.leak_k += area_cm2 * 1e-4 * 170.0 / v.volume;
        if (v.grid)
        {
            int x, y, z;
            hull_cell_for(at, v.grid->size, x, y, z);
            sl::gas_vessel_set_vent(v.grid, x, y, z, 0.4f);
        }
        return true;
    }

    void Systems::set_opening(int index, int key, double area_cm2)
    {
        if (index < 0 || index >= static_cast<int>(vessels_.size()))
            return;
        if (area_cm2 > 0.0)
            openings_[{index, key}] = area_cm2;
        else
            openings_.erase({index, key});
    }

    void Systems::set_link(int a, int b, int key, double area_cm2)
    {
        if (a < 0 || b < 0 || a == b || a >= static_cast<int>(vessels_.size()) || b >= static_cast<int>(vessels_.size()))
            return;
        if (area_cm2 > 0.0)
            links_[key] = {a, b, area_cm2};
        else
            links_.erase(key);
    }

    float Systems::grid_cell(int vessel, int x, int y, int z, int gas) const
    {
        if (vessel < 0 || vessel >= static_cast<int>(vessels_.size()) || !vessels_[static_cast<std::size_t>(vessel)].grid)
            return 0.0f;
        return sl::gas_vessel_cell(vessels_[static_cast<std::size_t>(vessel)].grid, x, y, z, static_cast<sl::VesselGas>(gas));
    }

    double Systems::update_fixed_sources(const SystemsContext &ctx)
    {
        const double sun_len = std::sqrt(ctx.sun_dir.x * ctx.sun_dir.x + ctx.sun_dir.y * ctx.sun_dir.y + ctx.sun_dir.z * ctx.sun_dir.z);
        const sl::dvec3 sun = sun_len > 0.0 ? ctx.sun_dir / sun_len : sl::dvec3(0.0, 1.0, 0.0);

        // Fixed sources first.
        double fixed = 0.0;
        for (SystemComponent &c : components_)
        {
            c.output_w = 0.0;
            if (!c.alive || !c.enabled)
                continue;
            if (c.type == "solar")
            {
                const double len = std::sqrt(c.facing.x * c.facing.x + c.facing.y * c.facing.y + c.facing.z * c.facing.z);
                const double cosine = c.tracking ? 1.0 : len > 0.0 ? (c.facing.x * sun.x + c.facing.y * sun.y + c.facing.z * sun.z) / len : 0.0;
                c.output_w = c.area * c.efficiency * solar_constant * ctx.sun_flux * std::max(cosine, 0.0);
            }
            else if (c.type == "rtg")
            {
                const double age = c.age_years + ctx.mission_time / (365.25 * 86400.0);
                c.output_w = c.watts * std::pow(0.5, age / std::max(c.half_life_years, 1e-6));
            }
            fixed += c.output_w;
        }
        return fixed;
    }

    void Systems::wear_components(double dt)
    {
        // Wear accrues while a component runs; a worn-out one drops off the bus until repaired.
        for (SystemComponent &c : components_)
        {
            if (c.wear_hours > 0.0 && c.powered && c.enabled && c.wear < 1.0)
                c.wear = std::min(1.0, c.wear + dt / (c.wear_hours * 3600.0));
            if (c.wear >= 1.0)
                c.powered = false;
        }
    }

    void Systems::solve_bus(double dt, const SystemsContext &ctx)
    {
        const double fixed = update_fixed_sources(ctx);
        wear_components(dt);

        // Loads, in priority order.
        std::vector<SystemComponent *> loads;
        for (SystemComponent &c : components_)
            if (c.alive && c.enabled && c.watts > 0.0 && (c.type == "load" || c.type == "regulator" || c.type == "scrubber" || c.type == "electrolyser" || c.type == "isru" || c.type == "refinery" || c.type == "comms") && c.wear < 1.0)
                loads.push_back(&c);
        std::stable_sort(loads.begin(), loads.end(), [](const SystemComponent *a, const SystemComponent *b) { return a->priority < b->priority; });

        double dispatch_capacity = 0.0;
        for (const SystemComponent &c : components_)
            if (c.alive && c.enabled && (c.type == "fission" || c.type == "fusion") && c.amount > 0.0)
                dispatch_capacity += c.watts;
            else if (c.alive && c.enabled && c.type == "fuel_cell" && find_tank("hydrogen") && tank_kg("hydrogen") > 0.0 && tank_kg("oxygen") > 0.0)
                dispatch_capacity += c.watts;

        double discharge_capacity = 0.0, charge_room = 0.0;
        for (const SystemComponent &c : components_)
            if (c.alive && c.type == "battery")
            {
                discharge_capacity += std::min(c.max_w, c.amount * 3600.0 / dt);
                charge_room += std::min(c.max_w, (c.capacity - c.amount) * 3600.0 / dt);
            }

        double budget = fixed + dispatch_capacity + discharge_capacity;
        double used = 0.0, wanted = 0.0;
        bool shed = false;
        for (SystemComponent *c : loads)
        {
            wanted += c->watts;
            c->powered = c->watts <= budget - used + 1e-9;
            if (c->powered)
                used += c->watts;
            else
                shed = true;
        }
        supply_w_ = fixed;
        demand_w_ = wanted;
        bus_powered_ = !shed;
        instruments_powered_ = budget > 1e-9 || loads.empty();

        balance_supply(used - fixed, discharge_capacity, charge_room, dt);
        run_processors(loads, dt, ctx);
    }

    // Generators and batteries cover a shortfall (need > 0); surplus power charges the batteries.
    void Systems::balance_supply(double need, double discharge_capacity, double charge_room, double dt)
    {
        if (need > 0.0)
        {
            for (SystemComponent &c : components_)
            {
                if (need <= 0.0)
                    break;
                if (!c.alive || !c.enabled)
                    continue;
                double produce = 0.0;
                if (c.type == "fission" || c.type == "fusion")
                {
                    produce = std::min({c.watts, need, c.amount * energy_per_kg_fuel * 0.3 / dt});
                    c.amount -= produce * dt / (energy_per_kg_fuel * 0.3);
                }
                else if (c.type == "fuel_cell")
                {
                    produce = std::min(c.watts, need);
                    const double h2 = take("hydrogen", produce * dt / fuel_cell_j_per_kg_h2);
                    const double ratio = produce * dt / fuel_cell_j_per_kg_h2;
                    if (ratio > 0.0)
                        produce *= h2 / ratio;
                    const double o2 = take("oxygen", h2 * 8.0);
                    if (h2 > 0.0)
                        produce *= o2 / (h2 * 8.0);
                    give("water", (h2 + o2) * 0.99);
                }
                c.output_w = produce;
                supply_w_ += produce;
                need -= produce;
            }
            if (need > 0.0 && discharge_capacity > 0.0)
            {
                const double draw = std::min(need, discharge_capacity);
                for (SystemComponent &c : components_)
                    if (c.alive && c.type == "battery")
                    {
                        const double cap = std::min(c.max_w, c.amount * 3600.0 / dt);
                        c.amount -= draw * (cap / discharge_capacity) * dt / 3600.0;
                    }
                supply_w_ += draw;
            }
        }
        else if (need < 0.0 && charge_room > 0.0)
        {
            const double put = std::min(-need, charge_room);
            for (SystemComponent &c : components_)
                if (c.alive && c.type == "battery")
                {
                    const double room = std::min(c.max_w, (c.capacity - c.amount) * 3600.0 / dt);
                    c.amount = std::min(c.capacity, c.amount + put * (room / charge_room) * dt / 3600.0);
                }
        }
    }

    void Systems::run_processors(const std::vector<SystemComponent *> &loads, double dt, const SystemsContext &ctx)
    {
        // Regolith processing: water into the tank. A "site" unit needs the ground and scales with the body's water.
        for (SystemComponent *c : loads)
            if (c->type == "isru" && c->powered)
            {
                const double k = c->site ? (ctx.landed ? ctx.water_richness : 0.0) : 1.0;
                give("water", c->kg_per_day * k / 86400.0 * dt);
                if (c->site && ctx.landed)
                {
                    give("nitrogen", c->n2_kg_per_day * ctx.nitrogen_richness / 86400.0 * dt);
                    double regen = c->regen_kg_per_day / 86400.0 * dt;
                    for (SystemComponent &s : components_)
                        if (regen > 0.0 && s.alive && s.type == "scrubber" && s.capacity < 1e8 && s.amount < s.capacity)
                        {
                            const double n = std::min(regen, s.capacity - s.amount);
                            s.amount += n;
                            regen -= n;
                        }
                }
            }

        // Refinery: water into main propellant while the tank has room.
        for (SystemComponent *c : loads)
            if (c->type == "refinery" && c->powered)
            {
                // Keep a quarter of the water tank for the crew.
                double capacity = 0.0;
                for (const SystemComponent &t : components_)
                    if (t.alive && t.type == "tank" && t.gas == "water")
                        capacity += t.capacity;
                const double spare = std::max(tank_kg("water") - 0.25 * capacity, 0.0);
                const double want = std::min({c->kg_per_day / 86400.0 * dt, propellant_room_, spare});
                if (want <= 0.0)
                    continue;
                const double water = take("water", want);
                propellant_made_ += water;
                propellant_room_ -= water;
            }

        // Electrolyser output.
        for (SystemComponent *c : loads)
            if (c->type == "electrolyser" && c->powered)
            {
                const double water = take("water", c->watts * dt / electrolysis_j_per_kg_water);
                const double o2 = give("oxygen", water * 8.0 / 9.0);
                give("hydrogen", water / 9.0);
                if (o2 < water * 8.0 / 9.0)
                    give("water", water - o2 * 9.0 / 8.0);
            }
    }

    // Open doors mix two vessels toward the same gas density, so the pressures and the air itself even out.
    void Systems::step_links(double dt)
    {
        for (const auto &entry : links_)
        {
            Vessel &a = vessels_[static_cast<std::size_t>(entry.second.a)], &b = vessels_[static_cast<std::size_t>(entry.second.b)];
            if (!a.alive || !b.alive || a.volume <= 0.0 || b.volume <= 0.0)
                continue;
            const double k = entry.second.area_cm2 * 1e-4 * 170.0 / std::min(a.volume, b.volume);
            const double mix = 1.0 - std::exp(-k * dt);
            auto share = [&](double &ma, double &mb) {
                const double target_a = (ma + mb) * a.volume / (a.volume + b.volume);
                const double move = (target_a - ma) * mix;
                ma += move;
                mb -= move;
            };
            share(a.o2, b.o2);
            share(a.n2, b.n2);
            share(a.co2, b.co2);
        }
    }

    namespace
    {
        // Calls with no crew and no vent after which the air in a vessel counts as evenly mixed.
        constexpr int grid_settle_steps = 900;

        bool differs(double a, double b) { return std::fabs(a - b) > 1.0e-4 * std::max(std::fabs(b), 1.0e-9); }
    } // namespace

    // Mixes the vessel's gas grid and keeps it scaled to the lumped totals. An empty, sealed vessel has nothing to mix once it has
    // settled, so it only follows the totals when they change.
    void Systems::step_grid(Vessel &v, double dt, const SystemsContext &ctx, bool crewed)
    {
        const bool active = crewed || !v.spec.vents.empty();
        v.grid_idle = active ? 0 : std::min(v.grid_idle + 1, grid_settle_steps);
        const bool settled = v.grid_idle >= grid_settle_steps;
        if (!settled && dt <= ctx.grid_dt_limit)
            sl::gas_vessel_step(v.grid, static_cast<float>(std::min(dt, 4.0)));
        if (settled && !differs(v.o2, v.grid_o2) && !differs(v.n2, v.grid_n2) && !differs(v.co2, v.grid_co2))
            return;
        sl::gas_vessel_set_total(v.grid, sl::VesselGas::Oxygen, static_cast<float>(v.o2));
        sl::gas_vessel_set_total(v.grid, sl::VesselGas::Nitrogen, static_cast<float>(v.n2));
        sl::gas_vessel_set_total(v.grid, sl::VesselGas::CarbonDioxide, static_cast<float>(v.co2));
        v.grid_o2 = v.o2;
        v.grid_n2 = v.n2;
        v.grid_co2 = v.co2;
    }

    void Systems::step_vessels(double dt, const SystemsContext &ctx)
    {
        for (std::size_t vi = 0; vi < vessels_.size(); ++vi)
        {
            Vessel &v = vessels_[vi];
            if (!v.alive)
                continue;
            const double crew = std::max(v.spec.crew, 0);
            const double o2_use = std::min(crew * crew_o2_kg_per_s * dt, v.o2);
            v.o2 -= o2_use;
            v.co2 += crew * crew_co2_kg_per_s * dt;
            if (crew > 0.0)
                take("water", crew * crew_water_kg_per_s * dt);

            // Local sources and sinks go into the grid before it mixes.
            if (v.grid && crew > 0.0 && !v.spec.crew_at.empty())
            {
                const double share = 1.0 / static_cast<double>(v.spec.crew_at.size());
                for (const sl::dvec3 &at : v.spec.crew_at)
                {
                    int x, y, z;
                    grid_cell_for(at, v.grid->size, x, y, z);
                    sl::gas_vessel_inject(v.grid, x, y, z, sl::VesselGas::CarbonDioxide, static_cast<float>(crew * crew_co2_kg_per_s * dt * share));
                    sl::gas_vessel_consume(v.grid, x, y, z, sl::VesselGas::Oxygen, static_cast<float>(o2_use * share));
                }
            }

            for (SystemComponent &c : components_)
            {
                if (!c.alive || !c.enabled || !c.powered || c.wear >= 1.0)
                    continue;
                const bool mine = c.vessel.empty() ? vi == 0 : c.vessel == v.spec.name;
                if (!mine)
                    continue;
                if (c.type == "scrubber" && c.amount > 0.0)
                {
                    const double removed = std::min({v.co2, c.kg_per_day / 86400.0 * dt, c.amount});
                    v.co2 -= removed;
                    c.amount -= removed;
                }
                else if (c.type == "regulator")
                {
                    const double o2_target = pressure_to_mass(v.spec.pressure_kpa * v.spec.o2_fraction, v.volume, v.spec.temperature_k, molar_o2);
                    const double n2_target = pressure_to_mass(v.spec.pressure_kpa * (1.0 - v.spec.o2_fraction), v.volume, v.spec.temperature_k, molar_n2);
                    if (v.o2 < o2_target)
                        v.o2 += take("oxygen", std::min(o2_target - v.o2, regulator_kg_per_s * dt));
                    if (v.n2 < n2_target)
                        v.n2 += take("nitrogen", std::min(n2_target - v.n2, regulator_kg_per_s * dt));
                }
            }

            double hatch_k = 0.0;
            for (const auto &opening : openings_)
                if (opening.first.first == static_cast<int>(vi) && v.volume > 0.0)
                    hatch_k += opening.second * 1e-4 * 170.0 / v.volume;
            if (v.leak_k + hatch_k > 0.0)
            {
                const double keep = std::exp(-(v.leak_k + hatch_k) * dt);
                v.o2 *= keep;
                v.n2 *= keep;
                v.co2 *= keep;
            }

            if (v.grid)
                step_grid(v, dt, ctx, crew > 0.0);

            const double p = v.pressure_kpa(), po2 = v.ppo2_kpa(), pco2 = v.ppco2_kpa();
            v.hazard.clear();
            double rate = -1.0 / 3600.0; // recovery
            // A low-pressure cabin (Apollo's 34.5 kPa pure oxygen) is judged against its own design pressure.
            const double scale = std::clamp(v.spec.pressure_kpa / 101.3, 0.1, 1.0);
            if (po2 < 12.0 || p < 30.0 * scale || pco2 > 8.0)
                rate = 1.0 / 180.0;
            else if (po2 < 16.0 || p < 50.0 * scale || pco2 > 4.0 || po2 > 50.0)
                rate = 1.0 / 1800.0;
            if (rate > 0.0 && crew > 0.0)
                v.hazard = pco2 > 4.0 ? "CO2 HIGH" : (po2 > 50.0 ? "O2 HIGH" : (p < 50.0 ? "LOW PRESSURE" : "O2 LOW"));
            if (crew > 0.0 && mode_ != LifeSupportMode::off)
            {
                v.health = std::clamp(v.health - rate * dt, 0.0, 1.0);
                if (v.health <= 0.0 && mode_ == LifeSupportMode::fatal)
                    crew_lost_ = true;
            }
        }
        step_links(dt);
    }

    bool Systems::set_crew(int vessel, int crew)
    {
        if (vessel < 0 || vessel >= static_cast<int>(vessels_.size()))
            return false;
        Vessel &v = vessels_[static_cast<std::size_t>(vessel)];
        crew = std::max(crew, 0);
        if (v.spec.crew <= 0 && crew > 0)
            v.health = 1.0;
        v.spec.crew = crew;
        return true;
    }

    namespace
    {
        bool is_consumer(const SystemComponent &c) { return c.type == "load" || c.type == "scrubber" || c.type == "regulator"; }
    } // namespace

    void Systems::add_rtg_age(double years)
    {
        for (SystemComponent &c : components_)
            if (c.type == "rtg")
                c.age_years += years;
    }

    void Systems::set_consumers(bool on)
    {
        for (SystemComponent &c : components_)
            if (is_consumer(c))
                c.enabled = on;
    }

    bool Systems::has_processing() const
    {
        for (const SystemComponent &c : components_)
            if (c.alive && (c.type == "isru" || c.type == "electrolyser" || c.type == "refinery"))
                return true;
        return false;
    }

    void Systems::set_processing(bool on)
    {
        for (SystemComponent &c : components_)
            if (c.type == "isru" || c.type == "electrolyser" || c.type == "refinery")
                c.enabled = on;
    }

    bool Systems::processing_on() const
    {
        for (const SystemComponent &c : components_)
            if (c.alive && c.enabled && (c.type == "isru" || c.type == "electrolyser" || c.type == "refinery"))
                return true;
        return false;
    }

    bool Systems::consumers_on() const
    {
        for (const SystemComponent &c : components_)
            if (c.alive && is_consumer(c) && c.enabled)
                return true;
        return false;
    }

    void Systems::step(double dt, const SystemsContext &ctx)
    {
        if (dt <= 0.0 || crew_lost_)
            return;
        propellant_room_ = ctx.propellant_room;
        const int n = std::clamp(static_cast<int>(std::ceil(dt / 10.0)), 1, 24);
        const double h = dt / n;
        for (int i = 0; i < n; ++i)
        {
            SystemsContext c = ctx;
            c.mission_time = ctx.mission_time - dt + h * (i + 1);
            solve_bus(h, c);
            step_vessels(h, c);
        }

        alarms_.clear();
        if (!bus_powered_)
            alarms_.push_back("POWER SHORTAGE");
        const double cap = battery_capacity_wh();
        if (cap > 0.0 && battery_wh() < 0.15 * cap)
            alarms_.push_back("BATTERY LOW");
        for (const SystemComponent &c : components_)
            if (c.alive && c.type == "tank" && c.gas == "oxygen" && c.capacity > 0.0 && c.amount < 0.1 * c.capacity)
            {
                alarms_.push_back("O2 TANK LOW");
                break;
            }
        for (const Vessel &v : vessels_)
            if (v.alive)
            {
                if (v.leak_k > 0.0)
                    alarms_.push_back(upper(v.spec.name) + " LEAK");
                if (!v.hazard.empty())
                    alarms_.push_back(upper(v.spec.name) + " " + v.hazard);
            }
        if (crew_lost_)
            alarms_.push_back("CREW LOST");
    }

    std::string Systems::alarm_text() const
    {
        std::string text;
        for (const std::string &a : alarms_)
            text += (text.empty() ? "" : "  ") + a;
        return text;
    }

    void Systems::publish(sl::PropertyTree &tree, const std::string &base)
    {
        const auto num = [&](const std::string &path, std::function<double()> getter) { tree.node(base + "/" + path)->tie_double(std::move(getter)); };
        tree.node(base + "/present")->tie_bool([] { return true; });
        tree.node(base + "/life-support")->tie_string([this] { return std::string(life_support_name(mode_)); });
        tree.node(base + "/alarm")->tie_string([this] { return alarm_text(); });
        tree.node(base + "/crew-lost")->tie_bool([this] { return crew_lost_; });
        tree.node(base + "/bus/instruments")->tie_bool([this] { return instruments_powered_; });
        tree.node(base + "/bus/powered")->tie_bool([this] { return bus_powered_; });
        num("bus/supply-w", [this] { return supply_w_; });
        num("bus/demand-w", [this] { return demand_w_; });
        num("battery/wh", [this] { return battery_wh(); });
        num("battery/percent", [this] {
            const double cap = battery_capacity_wh();
            return cap > 0.0 ? 100.0 * battery_wh() / cap : 0.0;
        });
        for (std::size_t i = 0; i < components_.size(); ++i)
        {
            const std::string cb = "component[" + std::to_string(i) + "]/";
            tree.node(base + "/" + cb + "name")->tie_string([this, i] { return components_[i].name; });
            tree.node(base + "/" + cb + "enabled")->tie_double([this, i] { return components_[i].enabled ? 1.0 : 0.0; }, [this, i](double v) { components_[i].enabled = v > 0.5; });
            tree.node(base + "/" + cb + "powered")->tie_double([this, i] { return components_[i].enabled && components_[i].powered && components_[i].wear < 1.0 ? 1.0 : 0.0; });
            tree.node(base + "/" + cb + "wear")->tie_double([this, i] { return components_[i].wear; });
            tree.node(base + "/" + cb + "output-w")->tie_double([this, i] { return components_[i].output_w; });
            tree.node(base + "/" + cb + "failed")->tie_double([this, i] { return components_[i].wear >= 1.0 ? 1.0 : 0.0; });
        }
        // Writing 1 or 0 switches every load, scrubber and regulator on or off.
        tree.node(base + "/processing")->tie_double([this] { return processing_on() ? 1.0 : 0.0; }, [this](double v) { set_processing(v > 0.5); });
        tree.node(base + "/consumers")->tie_double([this] { return consumers_on() ? 1.0 : 0.0; }, [this](double v) { set_consumers(v > 0.5); });
        // Writing 1 here overhauls every component.
        tree.node(base + "/repair")->tie_double([] { return 0.0; }, [this](double v) {
            if (v > 0.5)
                for (SystemComponent &c : components_)
                    c.wear = 0.0;
        });
        num("vessel-count", [this] { return static_cast<double>(vessels_.size()); });
        for (const char *gas : {"oxygen", "nitrogen", "water", "hydrogen", "deuterium"})
            num(std::string("tank/") + gas + "-kg", [this, gas] { return tank_kg(gas); });
        for (std::size_t i = 0; i < vessels_.size(); ++i)
        {
            const std::string vb = "vessel[" + std::to_string(i) + "]/";
            tree.node(base + "/" + vb + "name")->tie_string([this, i] { return vessels_[i].spec.name; });
            tree.node(base + "/" + vb + "hazard")->tie_string([this, i] { return vessels_[i].hazard; });
            num(vb + "pressure-kpa", [this, i] { return vessels_[i].alive ? vessels_[i].pressure_kpa() : 0.0; });
            num(vb + "ppo2-kpa", [this, i] { return vessels_[i].alive ? vessels_[i].ppo2_kpa() : 0.0; });
            num(vb + "ppco2-kpa", [this, i] { return vessels_[i].alive ? vessels_[i].ppco2_kpa() : 0.0; });
            tree.node(base + "/" + vb + "crew")->tie_double([this, i] { return static_cast<double>(vessels_[i].spec.crew); }, [this, i](double n) { set_crew(static_cast<int>(i), static_cast<int>(n)); });
            num(vb + "health", [this, i] { return vessels_[i].health; });
            num(vb + "leak-rate", [this, i] { return vessels_[i].leak_k; });
            // Writing an area (cm2) here opens a leak in this vessel; it reads back as 0.
            tree.node(base + "/" + vb + "breach-cm2")->tie_double([] { return 0.0; }, [this, i](double area) {
                if (area > 0.0)
                    breach(static_cast<int>(i), area);
            });
        }
    }
} // namespace kosmos

namespace kosmos
{
    namespace
    {
        bool starts_with(const std::string &text, const char *prefix) { return text.rfind(prefix, 0) == 0; }

        std::string tag_name(const std::string &object, const char *prefix)
        {
            std::string rest = object.substr(std::strlen(prefix));
            while (!rest.empty() && (rest[0] == '_' || rest[0] == '.' || rest[0] == ' '))
                rest.erase(0, 1);
            return rest;
        }
    } // namespace

    namespace
    {
        struct Tag
        {
            std::string name;
            glm::dvec3 lo, hi;
            int stage = 0;
        };

        struct Box
        {
            std::string name;
            glm::vec3 lo{1e30f}, hi{-1e30f};
        };

        // Parsed directly so discovery needs no graphics context; a part is named by its o object, or by its usemtl material when the file has no o lines (as simlib's loader does).
        std::vector<Box> obj_boxes(const std::filesystem::path &file)
        {
            std::ifstream in(file);
            std::vector<glm::vec3> verts;
            std::string line;
            Box *current = nullptr;
            std::string object, material;
            std::vector<Box> groups;
            while (std::getline(in, line))
            {
                std::istringstream words(line);
                std::string key;
                words >> key;
                if (key == "v")
                {
                    glm::vec3 p;
                    if (words >> p.x >> p.y >> p.z)
                        verts.push_back(p);
                }
                else if (key == "usemtl" || key == "o")
                {
                    std::string name;
                    words >> name;
                    (key == "o" ? object : material) = name;
                    const std::string &part = object.empty() ? material : object;
                    std::size_t at = 0;
                    while (at < groups.size() && groups[at].name != part)
                        ++at;
                    if (at == groups.size())
                        groups.push_back(Box{part, glm::vec3(1e30f), glm::vec3(-1e30f)});
                    current = &groups[at];
                }
                else if (key == "f" && current)
                {
                    std::string tok;
                    while (words >> tok)
                    {
                        int idx = std::atoi(tok.c_str());
                        idx = idx < 0 ? static_cast<int>(verts.size()) + idx : idx - 1;
                        if (idx >= 0 && idx < static_cast<int>(verts.size()))
                        {
                            current->lo = glm::min(current->lo, verts[idx]);
                            current->hi = glm::max(current->hi, verts[idx]);
                        }
                    }
                }
            }
            return groups;
        }

        std::vector<Box> part_boxes(const std::filesystem::path &file)
        {
            std::string ext = file.extension().string();
            for (char &ch : ext)
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (ext == ".obj")
                return obj_boxes(file);
            std::vector<Box> found;
            if (sl::Model *model = sl::load_model(file.string()))
            {
                for (const sl::ModelPart &mp : model->parts)
                    if (mp.has_bounds)
                        found.push_back(Box{mp.name, mp.bounds_min, mp.bounds_max});
                sl::destroy_model(model);
            }
            return found;
        }

        glm::mat4 part_placement(const PartSpec &part)
        {
            glm::mat4 place = glm::translate(glm::mat4(1.0f), glm::vec3(part.offset));
            place = glm::rotate(place, glm::radians(static_cast<float>(part.rotate.z)), glm::vec3(0.0f, 0.0f, 1.0f));
            place = glm::rotate(place, glm::radians(static_cast<float>(part.rotate.y)), glm::vec3(0.0f, 1.0f, 0.0f));
            place = glm::rotate(place, glm::radians(static_cast<float>(part.rotate.x)), glm::vec3(1.0f, 0.0f, 0.0f));
            return glm::scale(place, glm::vec3(part.scale));
        }

        struct TaggedBoxes
        {
            std::vector<Tag> boxes, vents, seats;
        };

        void add_part_tags(const Box &b, const glm::mat4 &place, int stage, TaggedBoxes &out)
        {
            const bool vessel = starts_with(b.name, "vessel"), vent = starts_with(b.name, "vent"), crew = starts_with(b.name, "crew");
            if (!(vessel || vent || crew) || b.lo.x > b.hi.x)
                return;
            glm::dvec3 lo(1e30), hi(-1e30);
            for (int c = 0; c < 8; ++c)
            {
                const glm::vec3 corner((c & 1) ? b.hi.x : b.lo.x, (c & 2) ? b.hi.y : b.lo.y, (c & 4) ? b.hi.z : b.lo.z);
                const glm::dvec3 p = glm::dvec3(place * glm::vec4(corner, 1.0f));
                lo = glm::min(lo, p);
                hi = glm::max(hi, p);
            }
            Tag tag{vessel ? tag_name(b.name, "vessel") : vent ? tag_name(b.name, "vent") : tag_name(b.name, "crew"), lo, hi, stage};
            if (vessel && tag.name.empty())
                tag.name = "Cabin";
            (vessel ? out.boxes : vent ? out.vents : out.seats).push_back(tag);
        }

        TaggedBoxes collect_tags(const ShipSpec &spec)
        {
            TaggedBoxes out;
            for (std::size_t s = 0; s < spec.stages.size(); ++s)
                for (const PartSpec &part : spec.stages[s].parts)
                {
                    if (part.model.empty())
                        continue;
                    const std::vector<Box> found = part_boxes(std::filesystem::path(spec.directory) / part.model);
                    const glm::mat4 place = part_placement(part);
                    for (const Box &b : found)
                        add_part_tags(b, place, static_cast<int>(s), out);
                }
            return out;
        }

        void fill_vessel(VesselSpec &target, const Tag &box, const std::vector<Tag> &vents, const std::vector<Tag> &seats)
        {
            target.centre = (box.lo + box.hi) * 0.5;
            target.size = glm::max(box.hi - box.lo, glm::dvec3(0.05));
            target.from_mesh = true;
            const auto unit = [&](const Tag &t) {
                const glm::dvec3 c = (t.lo + t.hi) * 0.5 - box.lo;
                const glm::dvec3 size = box.hi - box.lo;
                return glm::clamp(c / glm::max(size, glm::dvec3(1e-6)), glm::dvec3(0.0), glm::dvec3(1.0));
            };
            const auto inside = [&](const Tag &t) {
                const glm::dvec3 c = (t.lo + t.hi) * 0.5;
                return c.x >= box.lo.x - 1e-3 && c.x <= box.hi.x + 1e-3 && c.y >= box.lo.y - 1e-3 && c.y <= box.hi.y + 1e-3 && c.z >= box.lo.z - 1e-3 &&
                       c.z <= box.hi.z + 1e-3;
            };
            for (const Tag &t : vents)
                if (inside(t))
                {
                    // Tagged vents start closed; they mark where a breach() or hatch leak will show.
                    VesselSpec::Vent vent;
                    vent.at = unit(t);
                    target.vents.push_back(vent);
                }
            for (const Tag &t : seats)
                if (inside(t))
                    target.crew_at.push_back(unit(t));
            if (!target.crew_at.empty() && target.crew == 0)
                target.crew = static_cast<int>(target.crew_at.size());
        }
    } // namespace

    void find_tagged_vessels(ShipSpec &spec)
    {
        const TaggedBoxes tags = collect_tags(spec);
        for (const Tag &box : tags.boxes)
        {
            VesselSpec *target = nullptr;
            for (VesselSpec &v : spec.systems.vessels)
                if (v.name == box.name)
                    target = &v;
            if (!target)
            {
                spec.systems.vessels.emplace_back();
                target = &spec.systems.vessels.back();
                target->name = box.name;
                target->stage = box.stage;
            }
            fill_vessel(*target, box, tags.vents, tags.seats);
        }
    }
} // namespace kosmos
