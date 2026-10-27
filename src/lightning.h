#pragma once

#include "sl.h"
#include "audio.h"

#include <cstdint>
#include <vector>

namespace kosmos
{
    /**
     * Lightning for a thunderstorm: strikes at random bearings and distances, a flash that lights the sky, a jagged bolt from the
     * cloud base to the ground, and thunder that arrives after the speed of sound has covered the distance.
     * Local coordinates are x, y across the ground and z up, with the camera at the origin.
     */
    class Lightning
    {
    public:
        ~Lightning();

        /** Advance by `dt` real seconds. `thunder` is 0 to 1 (0 stops new strikes); `sound_volume` is 0 to 1. */
        void update(double dt, double thunder, double sound_volume);

        /** Sky flash from 0 to 1. */
        double flash() const { return flash_; }

        /**
         * The bolt as camera-facing quads in local coordinates (empty when none is showing). `ground_z` and `cloud_z` are the heights of the
         * ground and cloud base relative to the camera. Returns the vertex count (four per quad), at most `max_quads * 4`.
         */
        int bolt_vertices(double ground_z, double cloud_z, sl::Vertex3D *out, int max_quads) const;

        /** Free the thunder samples (before audio shuts down). */
        void release();

        static constexpr int bolt_quads = 160;

    private:
        struct Strike
        {
            double bearing = 0.0, distance = 2000.0, age = 0.0, seed = 0.0;
            bool thunder_pending = true;
        };

        sl::Sample *thunder_sample(int kind);

        std::vector<Strike> strikes_;
        double countdown_ = 3.0, flash_ = 0.0, clock_ = 0.0;
        std::uint32_t rng_ = 0x9e3779b9u;
        double volume_ = 1.0;
        sl::Sample *samples_[3] = {nullptr, nullptr, nullptr};
        double random();
    };
}
