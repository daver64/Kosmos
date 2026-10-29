#pragma once

#include "universe.h"

#include <functional>

namespace kosmos
{
    /**
     * The astronaut's suit. Where the air is not breathable it keeps oxygen, battery and pressure and carries a small
     * manoeuvring pack; in air like Earth's it is a HUD and nothing drains.
     */
    struct Suit
    {
        /** Ambient air is thin, dense or otherwise unbreathable, so the life-support systems are in use. */
        bool environment = false;
        bool jetpack = false;
        /** Levels from 0 to 1. */
        double oxygen = 1.0, battery = 1.0, pressure = 1.0, propellant = 1.0;
        double oxygen_seconds = 6.0 * 3600.0, battery_seconds = 12.0 * 3600.0;
        bool failed = false;
        /** Bodily health, 1 unhurt to 0 dead; weapons and blasts take it, a ship or base mends it. */
        double health = 1.0;
        double ambient_density = 0.0;
        double thrusting = 0.0;
    };

    /** What the player asked for in one frame; the turns are radians. */
    struct EvaInput
    {
        double forward = 0.0, right = 0.0, up = 0.0;
        bool jump = false, run = false, crouch = false;
        double yaw = 0.0, pitch = 0.0, roll = 0.0;
    };

    /**
     * A person out of the ship: walking on the ground in a surface body's gravity, or floating free. Positions are relative to
     * the centre of `body` in world axes; the velocity is inertial, so someone stepping out of a ship in orbit stays in that orbit.
     */
    class Astronaut
    {
    public:
        bool active = false;
        int body = -1;
        sl::dvec3 position{0.0}, velocity{0.0};
        /** View orientation: looks along -Z with +Y up. */
        sl::dquat orientation{1.0, 0.0, 0.0, 0.0};
        bool grounded = false;
        /** Manoeuvring pack switched on; it only fires in the air, in places where the suit is a life-support suit. */
        bool pack = false;
        /** Keep the view upright (on or near the ground, or in air); otherwise it turns freely with roll. */
        bool upright = true;
        double height = 0.0;
        double speed = 0.0;
        Suit suit;
        /** Index of the ship stepped out of. */
        int home_ship = -1;
        /** Index of the building whose interior this person is walking in, or -1 when outdoors. */
        int building = -1;
        static constexpr double eye_height = 1.7;

        /** Step out at `world_position` (a point on or beside the ship) moving with `world_velocity`; `look` is the way to face; `landed` puts you on the ground below. */
        void leave(const Universe &universe, int body_id, const sl::dvec3 &world_position, const sl::dvec3 &world_velocity,
                   const sl::dvec3 &look, const sl::Terrain *terrain, bool landed);
        /**
         * Moves a standing or falling person from `from` to `to` (both relative to the body centre) and returns where they end up
         * after solid things (ships, buildings, trees and rocks) have pushed them out. `height` is the height above the ground.
         */
        using Solids = std::function<sl::dvec3(const sl::dvec3 &from, const sl::dvec3 &to, double height)>;
        void step(const Universe &universe, const sl::Terrain *terrain, const EvaInput &input, double dt, const Solids &solids = nullptr);
        /** Stand still while `dt` seconds of warped time go by: the planet carries you round and the suit uses its supplies. */
        void carry(const Universe &universe, const sl::Terrain *terrain, double dt);
        /** 0 standing to 1 crouched, eased. */
        double crouch = 0.0;
        /** Top up oxygen, battery, pressure and propellant, as at an airlock or a base. */
        void recharge(double dt);

        sl::dvec3 eye_world(const Universe &universe) const;
        sl::dvec3 forward() const { return orientation * sl::dvec3(0.0, 0.0, -1.0); }
        sl::dvec3 view_up() const { return orientation * sl::dvec3(0.0, 1.0, 0.0); }
        sl::dvec3 local_up() const;

    private:
        void turn(const EvaInput &input);
        void update_suit(const Universe &universe, double dt);
    };

    /** The suit's heads-up display: vitals, altitude and speed, and where the nearest ship is. */
    void draw_suit_hud(const Astronaut &astronaut, const Universe &universe, int screen_w, int screen_h, double ship_distance, double ship_speed, const char *ship_name);
} // namespace kosmos
