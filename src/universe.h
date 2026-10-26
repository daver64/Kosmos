#pragma once

#include "flight.h"
#include "proptree.h"

#include <string>
#include <vector>

namespace kosmos
{
    enum class BodyKind
    {
        star,
        planet,
        moon,
        asteroid,
        comet
    };

    /** Static description of a celestial body. Its index in Universe::bodies is its FlightWorld body id. */
    struct Body
    {
        std::string name;
        BodyKind kind = BodyKind::planet;
        int parent = -1;
        double gm = 0.0;
        double radius = 1.0;
        /** Spin about +Y in rad/s. */
        double spin = 0.0;
        /** Angle of the body-fixed frame about +Y at time zero; a body with terrain does not spin, so this fixes its orientation. */
        double rotation_offset = 0.0;
        /** Radius of the sphere of influence in metres (infinite for the star). */
        double soi = 1.0e30;
        double atmosphere_density = 0.0;
        double atmosphere_scale_height = 8500.0;
        float atmosphere_colour[3] = {0.5f, 0.6f, 0.9f};
        double cloud_cover = 0.0;
        double cloud_height = 15000.0;
        float cloud_colour[3] = {1.0f, 1.0f, 1.0f};
        float colour[3] = {0.6f, 0.6f, 0.6f};
        /** Name of a procedural terrain recipe for the body, empty for a plain sphere. */
        std::string terrain;
        /** Optional surface image (equirectangular, any size up to 16384 x 8192 or more) relative to data/; empty looks for data/textures/<name>.jpg|png. */
        std::string texture;
        /** Optional glTF mesh relative to data/ drawn instead of the sphere; unit mean radius, scaled by `radius`. Physics stays a sphere. */
        std::string mesh;
        /** A procedurally generated rock: drawn but not labelled or listed on the map. */
        bool minor = false;
        /** Axial tilt in radians, applied to the drawn body and its rings only. */
        double tilt = 0.0;
        /** Ring annulus radii about the body's equator in metres, zero for no rings. */
        double ring_inner = 0.0;
        double ring_outer = 0.0;
        float ring_colour[3] = {0.8f, 0.75f, 0.65f};
        sl::OrbitElements orbit;
    };

    /**
     * The bodies of a star system on Kepler rails inside a FlightWorld. Ships are created in the same world, so they
     * feel every body's gravity and inherit its motion.
     */
    class Universe
    {
    public:
        sl::FlightWorld world;
        std::vector<Body> bodies;
        std::string name;

        /** Load bodies from a JSON file such as data/sol.json. Parents must precede their children. */
        bool load(const std::string &file, std::string &error);
        /** Load from JSON text (generated systems come through here); `file` is only used in error messages. */
        bool load_text(const std::string &text, const std::string &file, std::string &error);
        /** Name of the body with launch terrain: the "home" key of the system JSON, "Earth" by default. */
        std::string home = "Earth";
        /** Body id of the home planet, falling back to the first body with a terrain recipe; -1 if none. */
        int home_id() const;
        int find(const std::string &name) const;
        /**
         * Where a ship arriving from another star should orbit: an Earth-like planet if there is one, else a Mars- or Venus-like
         * rocky planet, else any rocky planet, else a moon of a gas giant, else the gas giant itself. Returns the body id and
         * sets `altitude` (metres above the surface); -1 if the system has nothing but the star.
         */
        int arrival_body(double &altitude) const;
        /** The body whose sphere of influence holds the point: the smallest one that contains it. */
        int soi_body(const sl::dvec3 &position) const;
        /** Absolute position of a body in metres. */
        sl::dvec3 position(int id) const { return world.body(id)->position; }
        double time() const { return world.time(); }
        /** Rotation angle of a body about +Y in radians at the current time. */
        double rotation_angle(int id) const;
        /** Altitude of a point above a body's mean radius. */
        double altitude(int id, const sl::dvec3 &position) const;
        /** Publish /universe/time and /universe/bodies/body[i]/... into the property tree. */
        void publish(sl::PropertyTree &tree);
    };
} // namespace kosmos
