#pragma once

#include <algorithm>
#include <cmath>

namespace kosmos
{
    /** What the plasma around a re-entering vehicle looks like, in metres and 0 to 1 brightness. */
    struct PlasmaLook
    {
        /** Overall brightness: 0 is nothing to see. */
        float brilliance = 0.0f;
        /** How hot the glow is, from 0 (dull red-orange) to 1 (white-violet). */
        float heat = 0.0f;
        /** Half-width of the sheath and the half-length of the wake behind the vehicle. */
        float width = 0.0f, length = 0.0f;
        /** Radius of the bright shock cap ahead of the nose. */
        float cap = 0.0f;
    };

    /**
     * The look of re-entry plasma from the stagnation heat flux (W/m^2) and the shape of the vehicle: `radius` is its body radius and
     * `nose_radius` the radius of its nose or heat shield, in metres. A small slim warhead leaves a thin faint streak; a big blunt
     * vehicle wears a broad glowing ball with a short fat wake; the middle sizes fall in between.
     */
    inline PlasmaLook plasma_look(double flux, double radius, double nose_radius)
    {
        PlasmaLook look;
        radius = std::max(radius, 0.05);
        // Air starts to glow about 20 kW/m^2 and is fully ablaze by a few MW/m^2.
        const double onset = std::clamp((flux - 2.0e4) / 1.5e6, 0.0, 1.0);
        if (onset <= 0.0)
            return look;
        const double glow = onset * onset * (3.0 - 2.0 * onset);
        // Size scale: 0 for a warhead (0.5 m) up to 1 for something the size of the Shuttle-A (4 m and more).
        const double big = std::clamp(std::log(radius / 0.5) / std::log(8.0), 0.0, 1.0);
        const double blunt = std::clamp(nose_radius / radius, 0.2, 1.2) / 1.2;
        const double mass = 0.2 + 0.8 * (0.7 * big + 0.3 * blunt);
        look.brilliance = static_cast<float>(std::clamp(glow * (0.2 + 0.8 * mass * mass), 0.0, 1.0));
        look.heat = static_cast<float>(std::clamp(0.15 + 0.85 * glow, 0.0, 1.0));
        look.width = static_cast<float>(radius * (0.9 + 1.0 * mass));
        look.length = static_cast<float>(radius * (8.0 + 22.0 * (1.0 - mass)) * (0.5 + 0.5 * glow));
        look.cap = static_cast<float>(radius * (0.6 + 0.9 * mass) * (0.5 + 0.5 * glow));
        return look;
    }
}
