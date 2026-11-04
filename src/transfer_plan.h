#pragma once

namespace kosmos
{
    /**
     * A Hohmann transfer between circular, coplanar orbits about one body: the same estimate the Transfer MFD shows.
     * Angles are in radians and times in seconds.
     */
    struct HohmannPlan
    {
        double dv1 = 0.0, dv2 = 0.0;
        double tof = 0.0;
        /** Where the target must be, ahead of the ship, at departure; and where it is now. */
        double lead = 0.0, phase = 0.0;
        /** Seconds until the next window (the middle of the burn); only meaningful when has_window. */
        double wait = 0.0;
        /** Seconds between windows. */
        double synodic = 0.0;
        /** False when both orbits have the same period, so the phasing never changes. */
        bool has_window = false;
    };

    /** `phase` is the target's angle ahead of the ship, as /target/phase-angle gives it. Returns a plan with has_window false (and zero times) for bad radii or mu. */
    HohmannPlan plan_hohmann(double mu, double r1, double r2, double phase);

    /** Seconds from now to window number `index` (0 is the next one). */
    double window_wait(const HohmannPlan &plan, int index);
}
