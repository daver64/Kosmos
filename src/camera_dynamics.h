#pragma once

#include "sl.h"

namespace kosmos
{
    /** Re-express an orbit camera's offset in a new orthonormal horizon frame. */
    void reframe_orbit_camera(const sl::dvec3 &old_up, const sl::dvec3 &old_east,
                              const sl::dvec3 &new_up, const sl::dvec3 &new_east,
                              double &yaw, double &pitch, double &roll);
    sl::dvec3 orbit_camera_view_up(const sl::dvec3 &up, const sl::dvec3 &east,
                                   const sl::dvec3 &forward, double roll);

    /** How a ship's cockpit eye moves on its springs: the `cockpit.head_motion` block of ship.json. */
    struct HeadMotion
    {
        bool enabled = true;
        /** Scales the movement; 1 is the default and 0 holds the eye still. */
        double strength = 1.0;
        /** Natural frequency of the head on its springs, in Hz, and the damping ratio (1 is critical). */
        double frequency = 3.8;
        double damping = 0.55;
        /** The most the eye moves from the seat, in metres (a soft limit). */
        double max_offset = 0.15;
    };

    /**
     * The Dynamic Camera: the pilot's eye is a mass held to the seat by springs, so it lags behind accelerations
     * and swings back, like a head. Work in the ship frame (nose -Z, up +Y, right +X) with the felt acceleration,
     * which is everything except gravity. Slow, sustained accelerations (a steady 1 g on the ground, a long burn)
     * are washed out so the eye returns to the seat.
     */
    class CameraDynamics
    {
    public:
        void reset();
        /** Moves the eye back towards the seat without any input (paused, or time warped). */
        void relax(double dt, const HeadMotion &motion);
        /** Advances by @p dt seconds under the felt acceleration @p felt_local in m/s^2, ship frame. */
        void step(double dt, const sl::dvec3 &felt_local, const HeadMotion &motion);
        /** The eye's offset from the seat in the ship frame, in metres. */
        sl::dvec3 offset(const HeadMotion &motion) const;

    private:
        sl::dvec3 x_{0.0}, v_{0.0}, baseline_{0.0};
        bool primed_ = false;
    };
} // namespace kosmos
