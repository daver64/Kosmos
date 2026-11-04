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
     *  solar        area_m2, efficiency, facing (ship frame, default +Y), tracking (true: always faces the Sun)
     *  rtg          watts, half_life_years, age_years
     *  fission      watts, fuel_kg
     *  fusion       watts, fuel_kg (deuterium)
     *  fuel_cell    watts (draws hydrogen and oxygen tanks, fills water tanks)
     *  tank         gas (oxygen, nitrogen, water, hydrogen, deuterium), capacity_kg, kg
     *  electrolyser watts (splits water into oxygen and hydrogen)
     *  isru         watts, kg_per_day (water from regolith). With "site": true it works only while the ship is on the ground and the yield
     *               scales with how much water the body has; "n2_kg_per_day" adds nitrogen drawn from the body's air and
     *               "regen_kg_per_day" restores CO2 scrubber sorbent
     *  refinery     watts, kg_per_day (turns water from the water tank into main propellant, while there is room in the tank)
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
        double kg_per_day = 0.0, n2_kg_per_day = 0.0, regen_kg_per_day = 0.0;
        /** An in-situ unit that needs the ground and the body's resources (see `isru`). */
        bool site = false;
        /** Hours of running before a "wear" component fails; 0 means it never wears out. Repairs reset `wear`. */
        double wear_hours = 0.0;
        int priority = 1;
        sl::dvec3 facing{0.0, 1.0, 0.0};
        /** Solar arrays only: the array steers to face the Sun, so the facing is ignored. */
        bool tracking = false;
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
        /** Where the ship is, for in-situ resource units that set "site": on the ground, and how rich the body is (0..1) in water ice and in nitrogen air. */
        bool landed = false;
        double water_richness = 0.0, nitrogen_richness = 0.0;
        /** Free space (kg) in the active stage's propellant tank, for a refinery. */
        double propellant_room = 0.0;
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
        /** Move up to `kg` of consumables (gas and water tanks, scrubber sorbent) from a docked supply pod's systems into this ship's matching, part-empty ones. Returns the mass moved. */
        double refill_from(Systems &donor, double kg);
        /**
         * Docked ships share their reserves: batteries, gas and water tanks and scrubber sorbent flow from whichever holds the larger
         * fraction to the other until both are level, limited to `power_w` of charge and `kg_per_s` of consumables.
         */
        void share_with(Systems &other, double dt, double power_w = 20000.0, double kg_per_s = 0.5);
        void set_battery_fraction(double fraction);
        /** Adds years of age to every RTG, so a probe can start part-way through its mission. */
        void add_rtg_age(double years);
        bool breach(int vessel, double area_cm2, const sl::dvec3 &at = {0.0, 0.5, 0.5});
        /** A controllable hole from vessel `index` to vacuum (an open hatch). `key` names it so it can be changed or closed with area 0. */
        void set_opening(int vessel, int key, double area_cm2);
        /** A passage between two vessels (an open door): gas flows until the pressures match. Closed with area 0. */
        void set_link(int vessel_a, int vessel_b, int key, double area_cm2);
        void set_mode(LifeSupportMode mode) { mode_ = mode; }
        /** Put `crew` people aboard vessel `index` (0 when they leave). Arriving after an empty spell restores full health. */
        bool set_crew(int vessel, int crew);
        /** Turn every load, CO2 scrubber and pressure regulator on or off together (a crew arriving or leaving). */
        void set_consumers(bool on);
        /** Switch every in-situ resource unit (ISRU, electrolyser, refinery) on or off. */
        void set_processing(bool on);
        bool processing_on() const;
        /** Propellant the refineries made since the last call; the ship adds it to its tank. */
        double take_propellant_made() { const double m = propellant_made_; propellant_made_ = 0.0; return m; }
        bool has_processing() const;
        bool consumers_on() const;
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
            /** Grid updates since the vessel last had a crew or a vent; once the air has mixed, an idle grid is left alone. */
            int grid_idle = 0;
            double grid_o2 = -1.0, grid_n2 = -1.0, grid_co2 = -1.0; // the totals the grid was last scaled to
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

        /** The consumable and wear levels of a ship's equipment, to carry it into a freshly built copy of the same ship. */
        struct State
        {
            struct Component
            {
                double amount = 0.0, age_years = 0.0, wear = 0.0;
                bool enabled = true, alive = true;
            };
            struct VesselLevels
            {
                double o2 = 0.0, n2 = 0.0, co2 = 0.0, leak_k = 0.0, health = 1.0;
            };
            std::vector<Component> components;
            std::vector<VesselLevels> vessels;
        };
        State state() const;
        /** Applies a state taken from the same ship design; a mismatched layout is ignored. */
        void restore(const State &state);

    private:
        void solve_bus(double dt, const SystemsContext &ctx);
        /** Zeroes and recomputes solar and RTG output; returns the total fixed supply in watts. */
        double update_fixed_sources(const SystemsContext &ctx);
        void wear_components(double dt);
        void balance_supply(double need, double discharge_capacity, double charge_room, double dt);
        void run_processors(const std::vector<SystemComponent *> &loads, double dt, const SystemsContext &ctx);
        void step_vessels(double dt, const SystemsContext &ctx);
        void step_grid(Vessel &v, double dt, const SystemsContext &ctx, bool crewed);
        void step_links(double dt);
        SystemComponent *find_tank(const std::string &gas);
        double take(const std::string &gas, double kg);
        double give(const std::string &gas, double kg);

        double propellant_made_ = 0.0, propellant_room_ = 0.0;
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
