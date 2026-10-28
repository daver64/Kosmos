#pragma once

#include "equipment.h"
#include "eva.h"
#include "sim.h"

#include "clutter.h"
#include "particles3d.h"

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace kosmos
{
    /** What the arsenal needs each frame from the app: the simulation, the person on foot and the ground under them. */
    struct ArsenalContext
    {
        Sim *sim = nullptr;
        Astronaut *eva = nullptr;
        std::function<const sl::Terrain *(int body)> terrain;
    };

    /**
     * Everything that is carried, fired, thrown or dropped: a person's pack and the item in their hands, racks of ordnance on ships,
     * bolts and falling bombs in flight, blast waves, and the particle effects they make. Positions are relative to the centre of
     * the body they are on, in world axes, as for Astronaut.
     */
    class Arsenal
    {
    public:
        Arsenal();
        ~Arsenal();

        /** Forget everything: new scenario. */
        void reset();

        // ---- A person's equipment
        /** Fill the pack with the standard kit and the locker of the ship or building `home` (null for the kit alone); the first item is held. */
        void equip_person(const Sim &sim, const Ship *home);
        /** Adds @p count of equipment @p id to the pack; false when there is no such equipment. */
        bool give(const Sim &sim, const std::string &id, int count);
        const std::vector<Stack> &pack() const { return pack_; }
        int selected() const { return selected_; }
        /** Hold the item in pack slot `slot`, or nothing when out of range or already held. */
        void select(int slot);
        void cycle(int direction);
        const EquipmentSpec *held(const Sim &sim) const;
        /** Pull the trigger of the held item: `pressed` on the frame the button goes down, `down` while it is held. */
        void use_held(ArsenalContext &context, bool pressed, bool down);
        int held_count() const;
        std::size_t survey_points() const { return survey_.size(); }

        // ---- Ships' racks
        /** How many items are left at a ship's hardpoint `index`. */
        int rack_count(const Ship &ship, std::size_t index);
        int rack_total(const Ship &ship);
        /** Release the next item of a hardpoint (or of the first loaded one when `index` is negative). Returns whether anything fell. */
        bool release(ArsenalContext &context, Ship &ship, int index = -1);

        // ---- The world
        /** Advance rounds, blast waves and effects by `dt` seconds of simulated time (`real_dt` for the visuals). */
        void update(ArsenalContext &context, double dt, double real_dt);
        std::size_t rounds_in_flight() const { return rounds_.size(); }
        std::size_t live_effects() const { return effects_.size(); }
        /** Whether blasts are heard (needs the audio device to be open). */
        void set_sound(bool on) { sound_on_ = on; }
        /** Total detonations since reset. */
        int detonations() const { return detonations_; }

        // ---- Drawing (call inside the 3D scene, with the eye at the origin)
        void draw_world(const Universe &universe, const sl::dvec3 &eye_world, const sl::Mesh *unit_sphere);
        /** The held item's stand-in model, in the eye's frame: `eye_to_world` is the view orientation. */
        void draw_held(const Sim &sim, const glm::mat4 &eye_to_world);
        /** 2D overlay: crosshair, ammo, instrument readout, damage and flash. */
        void draw_hud(const Sim &sim, const Astronaut &eva, int screen_w, int screen_h, const sl::dvec3 &eye_world);
        /** Brightness of a nuclear-flash white-out at the eye, 0 to 1. */
        double flash(const Universe &universe, const sl::dvec3 &eye_world) const;

    private:
        struct Round
        {
            const EquipmentSpec *spec = nullptr;
            int body = -1;
            sl::dvec3 position{0.0}, velocity{0.0};
            sl::dquat orientation{1.0, 0.0, 0.0, 0.0};
            double age = 0.0;
            double fuse = 0.0;
            bool bolt = false, stuck = false;
            const Ship *owner = nullptr;
            bool by_person = false;
        };
        struct Shock
        {
            const EquipmentSpec *spec = nullptr;
            int body = -1;
            sl::dvec3 centre{0.0};
            double radius = 0.0, speed = 340.0;
            std::set<const Ship *> hit;
            bool hit_person = false;
        };
        struct Active
        {
            sl::Effect3D effect;
            int body;
            bool cloud = false;
        };
        struct Scar
        {
            int body;
            sl::dvec3 position;
            double radius;
        };
        struct Rack
        {
            const Ship *ship = nullptr;
            std::string name;
            std::vector<int> counts;
            double synced_mass = -1.0;
        };
        struct Hit
        {
            enum Kind
            {
                none,
                ground,
                ship
            };
            Kind kind = none;
            sl::dvec3 point{0.0};
            Ship *target = nullptr;
        };

        Rack &rack_of(const Ship &ship);
        void sync_payload(const Sim &sim, Ship &ship, Rack &rack);
        Hit trace(ArsenalContext &context, int body, const sl::dvec3 &from, const sl::dvec3 &to, const Ship *ignore) const;
        void impact(ArsenalContext &context, const EquipmentSpec &spec, int body, const sl::dvec3 &point, const Hit &hit, bool by_person);
        void detonate(ArsenalContext &context, const Round &round, const sl::dvec3 &point);
        void add_effect(const Universe &universe, sl::Effect3DKind kind, int body, const sl::dvec3 &position, double scale, const sl::dvec3 &direction = sl::dvec3(0.0));
        void damage_person(ArsenalContext &context, double amount);
        void instrument(ArsenalContext &context, const EquipmentSpec &spec, bool pressed);
        const sl::Mesh *round_mesh(const std::string &prop, bool box);
        bool anchor_for(int body, const sl::dvec3 &position);

        std::vector<Stack> pack_;
        int selected_ = -1;
        double cooldown_ = 0.0, recoil_ = 0.0;
        std::vector<Round> rounds_;
        std::vector<Shock> shocks_;
        std::vector<Active> effects_;
        std::vector<Scar> scars_;
        std::vector<Rack> racks_;
        int detonations_ = 0;

        // Blasts are heard after the sound has travelled from the burst to where the eye was last drawn.
        struct Bang
        {
            double delay = 0.0, volume = 1.0;
            int kind = 0;
        };
        std::vector<Bang> bangs_;
        sl::Sample *bang_samples_[2] = {nullptr, nullptr};
        sl::Sample *nuke_sample_ = nullptr; // data/reference/nuke.mp3, decoded when an atom bomb is released so it is ready at the flash
        bool nuke_tried_ = false;
        sl::dvec3 eye_world_{0.0};
        bool have_eye_ = false, sound_on_ = false;

        // Particles live in coordinates relative to `anchor_` (itself body-relative) so floats stay precise.
        sl::ParticleSystem3D particles_{9000};
        int particle_body_ = -1;
        sl::dvec3 anchor_{0.0};

        std::vector<std::string> readout_;
        struct Sample
        {
            std::string text;
            int body = -1;
            sl::dvec3 position{0.0};
        };
        std::vector<Sample> survey_;
        double hurt_ = 0.0;
        std::string last_event_;
        double last_event_time_ = 0.0;

        struct Meshes;
        std::unique_ptr<Meshes> meshes_;
    };
} // namespace kosmos
