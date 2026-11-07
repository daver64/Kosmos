#pragma once

#include "flight.h"

#include <vector>

namespace kosmos
{
    struct LandingPlaneChange
    {
        bool valid = false;
        sl::dvec3 delta_v{0.0};
        double angle = 0.0;
    };

    /**
     * The minimum instantaneous plane rotation at position that puts target_direction in the orbital plane.
     * Position, velocity and target_direction use the same inertial frame. The caller must predict the target
     * at the intended arrival time; this calculation does not account for finite burns or terrain.
     */
    LandingPlaneChange landing_plane_change(const sl::dvec3 &position, const sl::dvec3 &velocity,
                                            const sl::dvec3 &target_direction);

    struct LandingPlaneWindow
    {
        bool valid = false;
        double wait = 0.0;
        sl::dvec3 departure_position{0.0};
        sl::dvec3 departure_velocity{0.0};
        sl::dvec3 arrival_target{0.0};
        LandingPlaneChange change;
    };

    struct LandingPlaneBurn
    {
        bool valid = false;
        bool sufficient_fuel = false;
        double fuel = 0.0;
        double duration = 0.0;
        double impulse_center = 0.0;
    };

    /**
     * Estimate a constant-thrust plane burn with a shared propellant supply. The center is the time
     * at which an instantaneous burn approximates its integrated delta-v; reserve is left untouched.
     * This does not predict the subsequent descent or account for gravity and attitude errors.
     */
    LandingPlaneBurn landing_plane_burn(double delta_v, double mass, double fuel, double reserve,
                                       double thrust, double flow);

    /** Throttle for the remaining delta-v over a positive, already-warped simulation interval. */
    double landing_plane_burn_level(double remaining, double acceleration, double interval);

    struct LandingCandidate
    {
        LandingPlaneWindow window;
        LandingPlaneBurn burn;
        double lead_angle = 0.0;
        double ignition_wait = 0.0;
    };

    /**
     * Enumerate windows for all lead angles, ordered by plane-burn fuel and then ignition time.
     * Rejects candidates without the requested alignment time, fuel reserve, or burn-duration limit.
     * The results require a full trajectory forecast before use; they do not prove a landing is possible.
     * Invalid inputs and a valid search with no candidates both produce an empty vector.
     */
    std::vector<LandingCandidate> landing_candidates(double mu, const sl::dvec3 &position,
                                                     const sl::dvec3 &velocity, const sl::dvec3 &target_direction,
                                                     const sl::dvec3 &spin_axis, double spin_rate,
                                                     double flight_time, double max_wait,
                                                     const std::vector<double> &lead_angles,
                                                     double mass, double fuel, double reserve,
                                                     double thrust, double flow,
                                                     double alignment_time, double max_burn_duration);

    /**
     * Find candidate departure windows in chronological order. Each uses the same two-body and
     * instantaneous-burn assumptions as landing_plane_window; a candidate is not a feasibility check.
     * An empty result means invalid input or no sampled suitable crossing.
     */
    std::vector<LandingPlaneWindow> landing_plane_windows(double mu, const sl::dvec3 &position,
                                                         const sl::dvec3 &velocity, const sl::dvec3 &target_direction,
                                                         const sl::dvec3 &spin_axis, double spin_rate,
                                                         double flight_time, double lead_angle, double max_wait);

    /**
     * Find the lowest-delta-v candidate within max_wait at which the target, rotated to the arrival time,
     * is lead_angle radians ahead of the ship. Assumes a two-body coast and an instantaneous plane change.
     * Returns no window if the input is invalid or no sampled bracket contains a suitable crossing.
     */
    LandingPlaneWindow landing_plane_window(double mu, const sl::dvec3 &position, const sl::dvec3 &velocity,
                                            const sl::dvec3 &target_direction, const sl::dvec3 &spin_axis,
                                            double spin_rate, double flight_time, double lead_angle, double max_wait);
}
