#pragma once

#include "lights.h"
#include "ordnance.h"
#include "weapon_specs.h"

#include <map>
#include <utility>
#include <vector>

namespace kosmos
{
    /**
     * Energy weapons on ships: beams, pulse streaks, bolts and flak, with the sparks, flares, shield flashes and lights that go with them.
     * Firing is driven by properties: `/ships/ship[N]/weapons/<group>/fire` for any ship, and `/controls/weapons/<group>/fire` as well for the
     * flown one, so keys, scripts, scenarios and link programs all work the same way. Positions are relative to the centre of the body they
     * are on, in world axes, as for Arsenal, which supplies the ray tests and the explosion effects.
     */
    class ShipWeapons
    {
    public:
        ~ShipWeapons();

        /** Forget everything: new scenario. */
        void reset();
        /** Fire, move and age everything by `dt` seconds of simulated time (`real_dt` for the sparks and flashes). */
        void update(ArsenalContext &context, Arsenal &arsenal, double dt, double real_dt);

        /** Draw inside the 3D scene with the eye at the origin: additive streaks, flares and sparks. `quad` is a 2x2 plane and `glow` a soft round texture. */
        void draw(const Universe &universe, const sl::dvec3 &eye_world, sl::Mesh *quad, sl::Bitmap *glow);
        /** Add the lights of live shots to `out`, at most `max_lights` of them, the brightest and nearest first. */
        void collect_lights(const Universe &universe, const sl::dvec3 &eye_world, int max_lights, std::vector<PlacedLight> &out) const;
        /** Free the beam texture; call while the graphics are still up. */
        void release_graphics();

        std::size_t live_beams() const { return beams_.size() + streaks_.size(); }
        std::size_t live_bolts() const { return bolts_.size(); }
        std::size_t live_sparks() const { return sparks_.size(); }
        int shots_fired() const { return shots_; }
        int hits() const { return hits_; }
        int shield_hits() const { return shield_hits_; }

    private:
        struct MountState
        {
            double cooldown = 0.0, beam_time = 0.0, lock = 0.0, effect_timer = 0.0;
        };
        struct Segment
        {
            int body = -1;
            sl::dvec3 a{0.0}, b{0.0};
            const WeaponSpec *spec = nullptr;
            double age = 0.0, life = 0.0;
            bool ended = false;
        };
        struct Bolt
        {
            int body = -1;
            sl::dvec3 position{0.0}, velocity{0.0};
            const WeaponSpec *spec = nullptr;
            Ship *owner = nullptr;
            double age = 0.0, life = 1.0;
            bool burst = false;
        };
        struct Spark
        {
            int body = -1;
            sl::dvec3 position{0.0}, velocity{0.0};
            double age = 0.0, life = 0.5;
            int colour[3] = {255, 255, 255};
            float size = 0.2f;
        };
        struct Flare
        {
            int body = -1;
            sl::dvec3 position{0.0};
            double age = 0.0, life = 0.3, size = 3.0;
            int colour[3] = {255, 255, 255};
            float light = 0.0f, range = 0.0f;
            bool shield = false;
        };

        void fire_mount(ArsenalContext &context, Arsenal &arsenal, Ship &ship, const ShipSpec::WeaponMount &mount, const WeaponSpec &spec, MountState &state, double dt);
        void hit(ArsenalContext &context, Arsenal &arsenal, const WeaponSpec &spec, int body, const sl::dvec3 &point, const Arsenal::RayHit &ray, double damage, bool tick);
        void burst(ArsenalContext &context, Arsenal &arsenal, const WeaponSpec &spec, int body, const sl::dvec3 &point, double scale);
        void add_sparks(const WeaponSpec &spec, int body, const sl::dvec3 &point, const sl::dvec3 &velocity, int count, double speed, const int *colour = nullptr);
        void add_flare(const WeaponSpec &spec, int body, const sl::dvec3 &point, double size, double life, double light, bool shield = false, const int *colour = nullptr);

        std::map<std::pair<const Ship *, int>, MountState> mounts_;
        std::vector<Segment> beams_, streaks_;
        std::vector<Bolt> bolts_;
        std::vector<Spark> sparks_;
        std::vector<Flare> flares_;
        sl::Bitmap *beam_texture_ = nullptr;
        unsigned rng_ = 12345u;
        int shots_ = 0, hits_ = 0, shield_hits_ = 0;

        double random(double low, double high);
        sl::dvec3 random_direction();
    };
} // namespace kosmos
