#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "json.hpp"
#include <sl.h>

namespace kosmos
{
    /** What happens when a crew compartment turns hostile: nothing, readouts and alarms only, or the crew can die. */
    enum class LifeSupportMode
    {
        off,
        warn,
        fatal
    };
    LifeSupportMode parse_life_support(const std::string &text, LifeSupportMode fallback = LifeSupportMode::warn);
    const char *life_support_name(LifeSupportMode mode);

    /**
     * One piece of ship equipment, described by `type`:
     *  battery      capacity_wh, charge (0..1), max_w
     *  solar        area_m2, efficiency, facing (ship frame, default +Y)
     *  rtg          watts, half_life_years, age_years
     *  fission      watts, fuel_kg
     *  fusion       watts, fuel_kg (deuterium)
     *  fuel_cell    watts (draws hydrogen and oxygen tanks, fills water tanks)
     *  tank         gas (oxygen, nitrogen, water, hydrogen, deuterium), capacity_kg, kg
     *  electrolyser watts (splits water into oxygen and hydrogen)
     *  scrubber     vessel, watts, kg_per_day, sorbent_kg
     *  regulator    vessel, watts (keeps a vessel at its pressure from the tanks)
     *  load         watts, priority (0 is shed last)
     */
    struct SystemComponent
    {
        std::string type, name, gas, vessel;
        /** Stage index that carries it; it is lost when the stage separates. -1 means the last (top) stage. */
        int stage = -1;
        double watts = 0.0, max_w = 0.0, capacity = 0.0, amount = 0.0;
        double area = 0.0, efficiency = 0.28, half_life_years = 87.7, age_years = 0.0;
        double kg_per_day = 0.0;
        /** Hours of running before a "wear" component fails; 0 means it never wears out. Repairs reset `wear`. */
        double wear_hours = 0.0;
        int priority = 1;
        sl::dvec3 facing{0.0, 1.0, 0.0};
        // Runtime
        bool enabled = true;
        bool alive = true;
        double wear = 0.0;
        double output_w = 0.0;
        bool powered = true;
    };

    /** A pressure vessel: a box in the ship frame holding a crew atmosphere, simulated on a 12x12x12 gas grid. */
    struct VesselSpec
    {
        std::string name = "Cabin";
        int stage = -1;
        /** Box centre and full size in the ship frame, metres. */
        sl::dvec3 centre{0.0};
        sl::dvec3 size{2.0, 2.0, 2.0};
        int crew = 0;
        double pressure_kpa = 101.3;
        double o2_fraction = 0.21;
        double co2_kpa = 0.0;
        double temperature_k = 294.0;
        /** Crew and vent positions in the box's unit cube (0..1 on each axis). */
        std::vector<sl::dvec3> crew_at;
        struct Vent
        {
            sl::dvec3 at{0.0, 0.5, 0.5};
            double area_cm2 = 0.0;
        };
        std::vector<Vent> vents;
        /** True when the box came from a tagged mesh object in a part model. */
        bool from_mesh = false;
    };

    struct SystemsSpec
    {
        std::vector<SystemComponent> components;
        std::vector<VesselSpec> vessels;
        bool empty() const { return components.empty() && vessels.empty(); }
    };

    /** Read the "systems" and "vessels" keys of a ship or part JSON. Stage names resolve against `stage_names`. */
    void parse_systems(const nlohmann::json &json, const std::vector<std::string> &stage_names, SystemsSpec &out);

    struct ShipSpec;
    /**
     * Add the pressure vessels a model describes. Objects named "vessel_<Name>" in a part's model give the box (their bounds,
     * placed in the ship frame); "vent_*" objects add a vent at their centre and "crew_*" objects a crew position. The box
     * comes from the mesh; crew count, pressure and the rest come from a "vessels" entry of the same name, if there is one.
     * Models are loaded only to read bounds.
     */
    void find_tagged_vessels(ShipSpec &spec);

    /** Conditions the ship supplies each step. */
    struct SystemsContext
    {
        /** Unit vector toward the star in the ship frame; flux is in units of the flux at 1 AU (0 in an eclipse). */
        sl::dvec3 sun_dir{0.0, 1.0, 0.0};
        double sun_flux = 1.0;
        /** Seconds of simulated time since the ship was created (for RTG decay). */
        double mission_time = 0.0;
        /** Gas-grid cost control: above this timestep the grid only rescales, it does not diffuse. */
        double grid_dt_limit = 30.0;
    };

    class Systems
    {
    public:
        Systems(const SystemsSpec &spec, LifeSupportMode mode);
        ~Systems();
        Systems(const Systems &) = delete;
        Systems &operator=(const Systems &) = delete;

        void step(double dt, const SystemsContext &ctx);
        /** The stage's equipment and compartments leave with it. */
        void drop_stage(int stage);
        /** Open a leak of `area_cm2` in vessel `index` at a point of its unit cube. Returns false for a bad index. */
        /** Set what fraction (0..1) of a gas's tank capacity is left, across all its tanks. */
        void set_tank_fraction(const std::string &gas, double fraction);
        void set_battery_fraction(double fraction);
        bool breach(int vessel, double area_cm2, const sl::dvec3 &at = {0.0, 0.5, 0.5});
        /** A controllable hole from vessel `index` to vacuum (an open hatch). `key` names it so it can be changed or closed with area 0. */
        void set_opening(int vessel, int key, double area_cm2);
        /** A passage between two vessels (an open door): gas flows until the pressures match. Closed with area 0. */
        void set_link(int vessel_a, int vessel_b, int key, double area_cm2);
        void set_mode(LifeSupportMode mode) { mode_ = mode; }
        LifeSupportMode mode() const { return mode_; }

        bool crew_lost() const { return crew_lost_; }
        bool powered() const { return bus_powered_; }
        /** True while any electricity is available; MFDs and lights go dark when it is not. */
        bool instruments_powered() const { return instruments_powered_; }
        const std::vector<std::string> &alarms() const { return alarms_; }
        std::string alarm_text() const;

        double supply_w() const { return supply_w_; }
        double demand_w() const { return demand_w_; }
        double battery_wh() const;
        double battery_capacity_wh() const;
        double tank_kg(const std::string &gas) const;
        int vessel_count() const { return static_cast<int>(vessels_.size()); }

        struct Vessel
        {
            VesselSpec spec;
            bool alive = true;
            double volume = 0.0;
            /** Lumped gas masses in kg: the authority. The grid only distributes them. */
            double o2 = 0.0, n2 = 0.0, co2 = 0.0;
            double leak_k = 0.0;
            double health = 1.0;
            std::string hazard;
            sl::GasVessel *grid = nullptr;
            double pressure_kpa() const;
            double ppo2_kpa() const;
            double ppco2_kpa() const;
        };
        const std::vector<Vessel> &vessels() const { return vessels_; }
        std::vector<SystemComponent> &components() { return components_; }
        const std::vector<SystemComponent> &components() const { return components_; }
        /** Grid cell masses for a display slice (gas 0..2) at height index y of a vessel. */
        float grid_cell(int vessel, int x, int y, int z, int gas) const;

        void publish(sl::PropertyTree &tree, const std::string &base);

    private:
        void solve_bus(double dt, const SystemsContext &ctx);
        /** Zeroes and recomputes solar and RTG output; returns the total fixed supply in watts. */
        double update_fixed_sources(const SystemsContext &ctx);
        void wear_components(double dt);
        void balance_supply(double need, double discharge_capacity, double charge_room, double dt);
        void run_processors(const std::vector<SystemComponent *> &loads, double dt);
        void step_vessels(double dt, const SystemsContext &ctx);
        void step_links(double dt);
        SystemComponent *find_tank(const std::string &gas);
        double take(const std::string &gas, double kg);
        double give(const std::string &gas, double kg);

        std::vector<SystemComponent> components_;
        std::vector<Vessel> vessels_;
        LifeSupportMode mode_ = LifeSupportMode::warn;
        bool crew_lost_ = false;
        bool bus_powered_ = true;
        bool instruments_powered_ = true;
        double supply_w_ = 0.0, demand_w_ = 0.0;
        std::vector<std::string> alarms_;
        std::map<std::pair<int, int>, double> openings_; // (vessel, key) -> area_cm2
        struct Link
        {
            int a = 0, b = 0;
            double area_cm2 = 0.0;
        };
        std::map<int, Link> links_;
    };
} // namespace kosmos
