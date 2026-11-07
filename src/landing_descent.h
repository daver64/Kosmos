#pragma once

#include "flight.h"

namespace kosmos
{
    struct LandingDescentCommand
    {
        bool valid = false;
        sl::dvec3 acceleration{0.0};
    };

    /**
     * Powered-descent acceleration in an inertial, body-centred frame. The target direction is its
     * current body-fixed surface direction in world axes; the destination is propagated through the
     * remaining time. This is a guidance command, not a trajectory or fuel feasibility prediction.
     */
    LandingDescentCommand landing_descent_command(const sl::dvec3 &position, const sl::dvec3 &velocity,
                                                 const sl::dvec3 &target_direction, const sl::dvec3 &spin_axis,
                                                 double spin_rate, double mu, double target_radius, double remaining);

    struct LandingApproachCommand
    {
        bool valid = false;
        sl::dvec3 direction{0.0};
        double level = 0.0, distance = 0.0;
    };

    /** The local target approach used by both the live controller and touchdown forecasts. */
    LandingApproachCommand landing_approach_command(const sl::dvec3 &position, const sl::dvec3 &surface_velocity,
                                                   const sl::dvec3 &target_direction, double altitude,
                                                   double vertical_speed, double gravity, double acceleration);

    struct LandingTrajectoryInput
    {
        sl::dvec3 position{0.0}, velocity{0.0}, target_direction{0.0}, spin_axis{0.0};
        sl::dvec3 plane_delta_v{0.0};
        double spin_rate = 0.0, mu = 0.0, radius = 0.0, target_elevation = 0.0;
        double mass = 0.0, fuel = 0.0, reserve = 0.0, thrust = 0.0, flow = 0.0;
        double drag_area = 0.0, atmosphere_density = 0.0, atmosphere_scale_height = 0.0;
        double sea_level_thrust_ratio = 1.0, torque_accel = 0.0, max_rate = 0.0;
        bool lifting_surfaces = false;
    };

    struct LandingTrajectoryPrediction
    {
        bool supported = false;
        bool captured = false;
        double fuel = 0.0, time = 0.0, distance = 0.0, altitude = 0.0;
    };

    /**
     * Bounded point-mass screening of a finite plane burn and powered descent to local-capture
     * conditions. Includes rotating air, drag and an approximate attitude controller, but assumes
     * spherical gravity and level terrain. Atmospheric lifting surfaces are rejected rather than
     * ignored. The result cannot certify a landing.
     */
    LandingTrajectoryPrediction landing_trajectory_predict(const LandingTrajectoryInput &input);
}
