#pragma once

#include "lights.h"
#include "universe.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace kosmos
{
    /**
     * What kind of drive an engine is. Jets and scramjets breathe air (and so stop working in a vacuum), rockets carry
     * their own propellant (liquid or solid, see EngineSpec::fuel), a nuclear rocket heats hydrogen in a reactor, and a
     * hyperjump drive makes no thrust or plume: it is the interstellar jump.
     */
    enum class EngineType { jet, scramjet, rocket, solid, nuclear, hyperjump };

    const char *engine_type_name(EngineType type);
    EngineType engine_type_from_name(const std::string &name, EngineType fallback);

    struct EngineSpec
    {
        sl::dvec3 position{0.0, 0.0, 0.0};
        /** Direction the engine pushes the ship, in the ship frame (nose is -Z). */
        sl::dvec3 direction{0.0, 0.0, -1.0};
        double thrust = 0.0;
        double isp = 300.0;
        /** Visual plume length in metres. */
        double plume = 10.0;
        /** Thrust fades with air density (propellers, jets); such an engine does not work in a vacuum. */
        bool air_breathing = false;
        EngineType type = EngineType::rocket;
        /** Propellant of a rocket, which picks the plume: "kerosene", "hydrogen", "methane", "hypergolic" or "solid". */
        std::string fuel = "kerosene";
        /** A jet with an afterburner shows a plume at high throttle; without one it is invisible. */
        bool afterburner = false;
        /** Jump range of a hyperjump drive, in light years. */
        double range_ly = 50.0;
    };

    /**
     * A wing, tail or control surface, in the ship frame (nose is -Z, roof +Y, right +X, origin at the centre of mass).
     * `control` names the input it responds to: "elevator" (pitch), "aileron" (roll) or "rudder" (yaw), or empty for a
     * fixed surface. `gain` is the change in angle of attack in radians for a full command (pitch up, roll left, yaw
     * left), so an elevator that raises the nose has a negative gain, and the two ailerons have opposite signs.
     */
    struct SurfaceSpec
    {
        std::string control;
        sl::dvec3 position{0.0};
        sl::dvec3 forward{0.0, 0.0, -1.0};
        sl::dvec3 up{0.0, 1.0, 0.0};
        double area = 1.0, cl0 = 0.0, cl_alpha = 5.0, cd0 = 0.02, induced_drag = 0.05;
        double stall = 0.28, gain = 0.0;
    };

    /** A wheel of an aircraft or rover. `steer` is the largest steering angle in radians (0 for fixed). */
    struct WheelSpec
    {
        sl::dvec3 position{0.0};
        double steer = 0.0;
        double rolling_resistance = 0.02;
    };

    /** A solid of revolution about the Z axis, from (z, radius) points, drawn in a flat colour. */
    struct PartSpec
    {
        /**
         * When set, the part comes from the props library instead of a profile: "stage", "adapter", "engines", "fins",
         * "fairing" or "csm". Props stack along their own +Y from y = 0 to y = length; they are placed with y = 0 at ship
         * z = `z` and +Y pointing to the nose (-Z).
         */
        std::string prop;
        /** An OBJ/glTF model path relative to the addon folder, already in the ship frame (nose is -Z). Takes precedence over prop and profile. */
        std::string model;
        float radius = 1.0f, top_radius = 1.0f, length = 1.0f, span = 1.0f;
        int count = 1;
        float z = 0.0f;
        float accent[3] = {0.11f, 0.11f, 0.125f};
        std::vector<std::pair<float, float>> profile;
        float colour[3] = {0.8f, 0.8f, 0.8f};
        float emissive = 0.0f;
        sl::dvec3 offset{0.0};
        /** Model parts only: a scale, then a rotation in degrees about X, Y and Z, applied before `offset`. */
        sl::dvec3 scale{1.0};
        sl::dvec3 rotate{0.0};
    };

    /** One separable section of a ship. Stages are listed from the bottom of the stack, which is jettisoned first. */
    struct StageSpec
    {
        std::string name;
        double dry_mass = 1000.0;
        double propellant = 0.0;
        /** Centre, length and radius for the inertia estimate and for debris. */
        double z = 0.0, length = 1.0, radius = 1.0;
        std::vector<EngineSpec> engines;
        std::vector<PartSpec> parts;
        /** Landing gear contact points in the ship frame; they leave with the stage. */
        std::vector<sl::dvec3> gear;
        double separation_speed = 1.0;
    };

    /** A docking port: where it sits in the ship frame, which way it faces (outward) and the "up" that fixes roll alignment. */
    struct PortSpec
    {
        std::string name = "Port";
        sl::dvec3 position{0.0, 0.0, 0.0};
        sl::dvec3 direction{0.0, 0.0, -1.0};
        sl::dvec3 up{0.0, 1.0, 0.0};
        /** IDS beacon frequency in MHz; 0 assigns one when the ship is created. */
        double frequency = 0.0;
    };

    /** One readout on a cockpit panel: any property-tree path, so addons can show whatever they publish. */
    struct Instrument
    {
        std::string label, property;
        double scale = 1.0;
        std::string format = "%.1f";
    };

    /** The view from inside a ship: an eye point in the ship frame and a panel of property readouts. */
    struct Cockpit
    {
        bool present = false;
        sl::dvec3 eye{0.0, 0.0, 0.0};
        std::vector<Instrument> panel;
        /** Draw the dashboard with two MFDs (default modes orbit and surface) below the instrument row. */
        bool mfds = true;
        std::string mfd_left = "orbit", mfd_right = "surface";
    };

    /** An extra viewpoint inside or around a ship, in the ship frame (nose is -Z). Angles are degrees from straight ahead. */
    struct CameraSpec
    {
        std::string name;
        sl::dvec3 position{0.0};
        double yaw = 0.0, pitch = 0.0, fov = 60.0;
    };

    struct ShipSpec
    {
        std::string name;
        std::string kind = "ship";
        std::string directory;
        /** The ship.json this was read from, so the part editor can write a converted part back. */
        std::string source_file;
        std::string script;
        Cockpit cockpit;
        std::vector<PortSpec> ports;
        /** Transponder frequency in MHz; 0 assigns one when the ship is created. */
        double transponder = 0.0;
        /** Attitude control authority in rad/s^2 and the fastest turn rate in rad/s. */
        double torque_accel = 0.3;
        double max_rate = 0.5;
        /** Translation thrust in newtons for the RCS (0 for none). */
        double rcs_thrust = 0.0;
        /** Cd * A for the whole stack in m^2. */
        double drag_area = 10.0;
        /** Camera distance that frames the ship. */
        double camera_distance = 60.0;
        std::vector<StageSpec> stages;
        /** Aerodynamic surfaces and wheels, for aircraft and anything else that flies in an atmosphere. */
        std::vector<CameraSpec> cameras;
        std::vector<SurfaceSpec> surfaces;
        std::vector<WheelSpec> wheels;
        /** Running lights, strobes and floodlamps, in the ship frame. */
        std::vector<LightSpec> lights;
        /** Moments of inertia about the ship X, Y and Z axes in kg m^2; zero estimates them from the stages. */
        sl::dvec3 inertia{0.0};
        /** Rate damping gains for pitch, yaw and roll while the stability hold is on (fly-by-wire). */
        sl::dvec3 damping{0.6, 0.6, 0.6};
        bool aircraft() const { return kind == "aircraft"; }
    };

    bool load_ship_spec(const std::string &file, ShipSpec &spec, std::string &error);

    /** Control inputs applied to the active ship each frame. */
    struct Controls
    {
        double throttle = 0.0;
        double pitch = 0.0, yaw = 0.0, roll = 0.0;
        double rcs_x = 0.0, rcs_y = 0.0, rcs_z = 0.0;
        /** off, stability, prograde, retrograde, radial-out, radial-in, normal, antinormal or attitude. */
        std::string sas = "stability";
        double target_pitch = 90.0, target_heading = 90.0;
        bool stage = false;
        /** Wheel brakes, 0 to 1. */
        double brake = 0.0;
        /** Flaps, 0 (up) to 1 (full). */
        double flaps = 0.0;
    };

    /** Values derived from the ship's state once per frame; published to the property tree. */
    struct Telemetry
    {
        int soi = 0;
        double altitude = 0.0;
        double surface_speed = 0.0;
        double orbital_speed = 0.0;
        double vertical_speed = 0.0;
        double apoapsis = 0.0, periapsis = 0.0;
        double eccentricity = 0.0, inclination = 0.0, period = 0.0;
        double time_to_apoapsis = 0.0, time_to_periapsis = 0.0;
        bool bound = false;
        double pitch = 0.0, heading = 0.0;
        double mass = 0.0, thrust = 0.0, twr = 0.0, acceleration_g = 0.0;
        double stage_fuel = 0.0, total_fuel = 0.0;
        /** Aerodynamics: angle of attack and sideslip in degrees, dynamic pressure in pascals and the climb angle in degrees. */
        double alpha = 0.0, sideslip = 0.0, dynamic_pressure = 0.0, flight_path = 0.0;
        double latitude = 0.0, longitude = 0.0;
        double semi_major_axis = 0.0, raan = 0.0, arg_periapsis = 0.0, true_anomaly = 0.0;
        double body_radius = 0.0;
        bool landed = false;
    };

    struct Debris
    {
        int body = -1;
        int stage = 0;
        double age = 0.0;
        double centre_z = 0.0;
    };

    /**
     * A vessel: a FlightWorld body built from a ShipSpec, with staging, attitude control, telemetry and rails handling.
     * Ships are not movable once published, because the property tree holds callbacks into them.
     */
    class Ship
    {
    public:
        Ship(Universe &universe, ShipSpec spec);
        ~Ship();
        Ship(const Ship &) = delete;
        Ship &operator=(const Ship &) = delete;

        const ShipSpec &spec() const { return spec_; }
        /** For the part editor, which swaps a procedural part for an exported mesh while the ship flies. */
        ShipSpec &editable_spec() { return spec_; }
        std::string name;
        int body_id() const { return body_; }
        int active_stage() const { return active_stage_; }
        int stage_count() const { return static_cast<int>(spec_.stages.size()); }
        bool crashed() const { return crashed_; }
        const Telemetry &telemetry() const { return telemetry_; }
        /** What the reaction-control jets did on the last update, in the ship frame: torque command (pitch, yaw, roll) and translation push, each -1..1. */
        const sl::dvec3 &rcs_torque() const { return rcs_torque_; }
        const sl::dvec3 &rcs_push() const { return rcs_push_; }
        const std::vector<Debris> &debris() const { return debris_; }
        sl::FlightBody &body();
        const sl::FlightBody &body() const;

        /** Stand on a body's surface at the given direction from its centre, nose up, facing a compass heading. */
        void place_on_surface(int body, const sl::dvec3 &direction, double heading_degrees, double ground_height);
        /** Place into a circular equatorial-ish orbit of the given altitude. */
        /** Fly level at `altitude` metres above `ground_height`, heading in degrees, at `speed` m/s through the air. */
        void place_airborne(int body, const sl::dvec3 &direction, double heading_degrees, double ground_height, double altitude, double speed);
        /** Put the ship at a position and velocity relative to the centre of body `body` (world axes), e.g. beside another player. */
        void place_relative(int body, const sl::dvec3 &position, const sl::dvec3 &velocity, const sl::dquat &orientation);
                void place_in_orbit(int body, double altitude, double inclination_degrees, double phase_degrees = 0.0);
        /**
         * Place on an ellipse with the given periapsis and apoapsis altitudes at a true anomaly. With `toward` >= 0 the orbit
         * lies in that body's orbital plane and its apoapsis points at where the body will be when the ship gets there.
         */
        void place_on_ellipse(int body, double periapsis, double apoapsis, double true_anomaly_degrees, int toward);

        /** Docking port `index` in the world frame: position, outward direction and up. */
        void port_world(int index, sl::dvec3 &position, sl::dvec3 &direction, sl::dvec3 &up) const;
        /** Velocity of a world-space point rigidly attached to the ship. */
        sl::dvec3 point_velocity(const sl::dvec3 &world_point) const;
        /** Mass of ships docked to this one that it carries along; included in its mass and inertia. */
        void set_attached_mass(double kilograms);

        void apply_controls(const Controls &controls, double dt);
        /** Refresh telemetry and handle impacts and debris after the world has stepped. */
        void after_step(double dt);

        /** Free the ship from rails and back to full dynamics. */
        void go_dynamic();
        /** Put a coasting ship on Kepler rails around the body whose sphere of influence holds it. */
        bool go_rails();
        bool on_rails() const;
        /** True when the ship may be put on rails: coasting above any atmosphere and not landed. */
        bool can_rails(const Controls &controls) const;

        bool stage_once();
        /** Direction of travel relative to the sphere-of-influence body (including its spin) in world axes. */
        sl::dvec3 relative_velocity() const;
        /** Local up, east and north for the body the ship is closest to in influence. */
        void local_frame(sl::dvec3 &up, sl::dvec3 &east, sl::dvec3 &north) const;

        /** Publish values under base (such as /ships/ship[0]). */
        void publish(sl::PropertyTree &tree, const std::string &base);
        void unpublish();

    private:
        void build_body();
        void refresh_mass_properties();
        void update_telemetry();
        sl::dvec3 target_direction(const Controls &controls, bool &valid) const;
        void apply_attitude(const Controls &controls, double dt);
        void apply_flight_controls(const Controls &controls);
        void drop_stage(int index);

        Universe &universe_;
        ShipSpec spec_;
        int body_ = -1;
        int active_stage_ = 0;
        bool crashed_ = false;
        double gear_k_ = 0.0;
        double attached_mass_ = 0.0;
        Telemetry telemetry_;
        std::vector<Debris> debris_;
        sl::PropertyTree *tree_ = nullptr;
        std::string base_;
        std::vector<int> stage_first_thruster_;
        sl::dvec3 rcs_torque_{0.0}, rcs_push_{0.0};
    };
} // namespace kosmos
