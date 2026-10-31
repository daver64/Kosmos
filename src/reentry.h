#pragma once

#include "universe.h"

#include <string>
#include <vector>

namespace kosmos
{
    /**
     * Re-entry heating (the "thermal" block of a ship.json or an ordnance entry). Stagnation-point heat flux comes from the Sutton-Graves
     * relation q = k * sqrt(rho / Rn) * v^3; a fraction of it (`flux_factor`) heats the windward skin, which radiates away T^4. An ablative
     * shield absorbs the heat while it lasts. Where the skin runs past its limit the vehicle takes damage.
     */
    struct ThermalSpec
    {
        bool enabled = false;
        /** Radius of the nose or heat shield in metres; blunt shapes heat less. */
        double nose_radius = 1.0;
        /** Windward skin area in m^2. */
        double heat_area = 10.0;
        /** Heat capacity of the skin in J/K per m^2 of heat_area. */
        double heat_capacity = 1.5e4;
        double emissivity = 0.85;
        /** Average flux over the windward skin as a fraction of the stagnation flux. */
        double flux_factor = 0.5;
        /** Skin temperature in kelvin above which the vehicle takes damage while the shield faces the flow. */
        double max_temp = 1900.0;
        /** The limit where the flow strikes the bare structure (the shield turned away or used up). */
        double bare_max_temp = 650.0;
        /** Ablative shield: mass in kg and heat absorbed per kg burnt away in J. Zero mass means a passive (radiative) shield. */
        double shield_mass = 0.0;
        double ablation_heat = 2.5e7;
        /** Fraction of the shield's heat that still reaches the structure behind it. */
        double leak = 0.03;
        /** Outward normal of the shield (the windward side) in the ship frame, and how far off the flow it still protects, in degrees. */
        sl::dvec3 shield_axis{0.0, -1.0, 0.0};
        double shield_cone = 65.0;
        /** Sutton-Graves constant, 1.74e-4 for air in SI units. */
        double k = 1.74e-4;
        /** Heat flux in W/m^2 (stagnation) above which the plasma sheath blacks out radio. */
        double blackout_flux = 1.5e5;
    };

    /** The heat state of one vehicle. */
    struct ThermalState
    {
        double temp = 288.0;
        /** Ablator left in kg; negative until first set from the spec. */
        double shield = -1.0;
        /** Stagnation heat flux in W/m^2 right now, and the peaks so far. */
        double flux = 0.0, peak_flux = 0.0, peak_temp = 0.0;
        bool shielded = true;
        bool blackout = false;
    };

    /** Sutton-Graves stagnation heat flux in W/m^2 for density `rho` kg/m^3, speed `v` m/s and nose radius `rn` m. */
    double stagnation_flux(double rho, double v, double rn, double k = 1.74e-4);

    /**
     * Advance the heat state by `dt` seconds. `windward` is the cosine between the shield's outward normal and the direction of travel (1 when
     * the shield leads). Returns the damage rate in health fractions per second.
     */
    double step_thermal(const ThermalSpec &spec, ThermalState &state, double rho, double v, double windward, double ambient, double dt);

    /**
     * Hypersonic aerodynamics for a blunt body or a lifting body (the "hypersonic" block). A modified Newtonian flat-plate force acts along
     * the windward axis: F = -axis * q * S * cd_max * c^2, where c is the cosine between the axis and the velocity. A capsule gets drag
     * from that when its shield leads and lift when it is trimmed off the flow; a shuttle belly-first gets lift and drag from its angle of
     * attack. A restoring moment holds the vehicle at its trim angle. Below `blend_lo` m/s the ship's own subsonic model applies alone.
     */
    struct HypersonicSpec
    {
        bool enabled = false;
        double area = 10.0;
        double length = 5.0;
        double cd_max = 1.5;
        double cd_back = 0.8;
        double cd_skin = 0.05;
        /** Windward axis in the ship frame (the belly of a shuttle, the shield of a capsule), and the direction the nose of the trim points. */
        sl::dvec3 axis{0.0, -1.0, 0.0};
        sl::dvec3 trim_dir{0.0, 0.0, -1.0};
        /** Angle in degrees between the axis and the flow at trim. */
        double trim_deg = 50.0;
        /** Moment coefficient per unit of sin(error) and rate damping coefficient. */
        double cm = 0.25;
        double damping = 0.4;
        double blend_lo = 700.0, blend_hi = 1800.0;
    };

    struct AeroResult
    {
        sl::dvec3 force{0.0}, torque{0.0};
        /** 0 below blend_lo to 1 above blend_hi: how far the hypersonic model has taken over from the ship's own aerodynamics. */
        double blend = 0.0;
    };

    /** Force and torque in the ship frame for a body moving at `velocity` (ship frame, m/s, relative to the air) turning at `rate` rad/s. */
    AeroResult hypersonic_aero(const HypersonicSpec &spec, const sl::dvec3 &velocity, double rho, const sl::dvec3 &rate);

    /** A parachute (the "parachutes" list). */
    struct ParachuteSpec
    {
        std::string name = "Parachute";
        /** Where the canopy pulls on the ship, in the ship frame. */
        sl::dvec3 position{0.0};
        /** Drag area (Cd * A) in m^2 once open and while still reefed. */
        double cd_area = 300.0, reef_cd_area = 20.0;
        /** Seconds reefed before the canopy opens fully, and seconds it takes to fill. */
        double reef_time = 5.0, inflation_time = 6.0;
        /** Dynamic pressure in Pa above which it tears away. */
        double max_q = 15000.0;
        /** Deploys itself below this altitude and speed (0 means never automatically). */
        double auto_altitude = 0.0, auto_speed = 0.0;
        /** Canopy size for drawing, in metres. */
        double canopy_radius = 0.0;
        /** Frees itself on touchdown. */
        bool cut_on_landing = true;
        /** An optional canopy model (OBJ/glTF, relative to the addon folder) drawn while the chute is out. Its origin is the apex where the
         *  lines meet the ship and it points along -Z; `model_radius` is the radius of the model as authored, so it is scaled to `canopy_radius`. */
        std::string model;
        double model_radius = 1.0;
    };

    enum class ChuteState
    {
        stowed,
        reefed,
        opening,
        open,
        gone
    };

    struct ChuteRuntime
    {
        ChuteState state = ChuteState::stowed;
        double time = 0.0;
        double fill = 0.0;
    };

    /** Drag area in m^2 of a chute right now. */
    double chute_drag_area(const ParachuteSpec &spec, const ChuteRuntime &runtime);
} // namespace kosmos
