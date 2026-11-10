#pragma once

#include "lights.h"
#include "nav.h"
#include "universe.h"

#include "clutter.h"
#include "terrain.h"
#include "terrain_mesh.h"

#include "model3d.h"

#include <filesystem>
#include <cmath>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <vector>

namespace kosmos
{
    /**
     * One prop of a base, placed in the base's own frame: x to the right of the base heading, z along it, in metres.
     * `type` names a builder from the props library; the numeric parameters and colours are optional.
     */
    struct BaseObject
    {
        std::string type; // "model" draws `model` instead of a builder
        /**
         * type "model": an OBJ/glTF file, relative to the folder of the base file (or the user's bases folder). Its frame
         * is the props library's: +Z forward, +Y up and +X to the left, so it sits at the object's place unturned.
         */
        std::string model;
        /** type "building": the name of an addon building (see Addon::buildings), which is spawned as a fixed, enterable ship at this place. */
        std::string building;
        double x = 0.0, z = 0.0, y = 0.0; // y lifts the prop, e.g. onto a pad
        double heading = 0.0; // degrees, clockwise from the base heading
        double scale = 1.0;
        int count = 1;                // copies, for rows of lamps, fences and tanks
        double step_x = 0.0, step_z = 0.0;
        double size = 0.0, width = 0.0, depth = 0.0, height = 0.0, length = 0.0, radius = 0.0; // 0 keeps the builder default
        int number = 0;               // floors, engines, panels
        unsigned seed = 1;
        bool has_colour = false, has_colour2 = false;
        sl::Colour colour{255, 255, 255, 255}, colour2{255, 255, 255, 255};
    };

    /** A radio aid at a place in the base frame; `heading` is relative to the base heading. type: vor, ndb, ils or vtol. */
    struct BaseNavaid
    {
        std::string type, id, name;
        double x = 0.0, z = 0.0;
        double frequency = 0.0;
        double heading = 0.0;
        double glide = 3.0;
        double range = 0.0; // 0 keeps the default for the type
    };

    /** An ILS serving one landing direction of a runway. */
    struct RunwayIls
    {
        double frequency = 0.0;
        bool reverse = false; // lands towards -z instead of +z
    };

    /** A strip centred on (x, z) in the base frame, `length` long along the heading. The ground under it is levelled. */
    struct Runway
    {
        std::string name = "Runway";
        double x = 0.0, z = 0.0;
        double heading = 0.0; // relative to the base heading
        double length = 1500.0, width = 45.0;
        std::vector<RunwayIls> ils;
    };

    /**
     * A built-up site on the surface of a body: the terrain is flattened to one height around it and the objects stand
     * on that. Beacons are registered for the pad (a VTOL beacon and a VOR) unless disabled.
     */
    /** A fixed camera at a base. Position is in the base frame (x right, z forward, y up); it follows the flown ship when `track` is set. */
    struct BaseCamera
    {
        std::string name;
        double x = 0.0, y = 5.0, z = 0.0;
        double fov = 50.0;
        bool track = true;
        double yaw = 0.0, pitch = 0.0; // degrees from the base heading, when not tracking
    };

    struct Base
    {
        std::string id, name, body;
        /** Four-letter code shown on the nav displays; made up from the id when the file has none. */
        std::string icao;
        std::string source_dir; // folder of the file it was read from; model paths are relative to it
        double latitude = 0.0, longitude = 0.0;
        double heading = 0.0;       // degrees, 0 = north, 90 = east
        double flat_radius = 300.0; // metres of level ground around the centre
        double blend = 200.0;       // metres over which the ground returns to the natural terrain
        double elevation = 0.0;     // metres above the datum, used when the body has no terrain of its own
        std::vector<BaseNavaid> navaids;
        std::vector<Runway> runways;
        double pad_x = 0.0, pad_z = 0.0, pad_heading = 0.0, pad_deck = 0.0; // where a landed ship stands, in the base frame
        std::vector<BaseObject> objects;
        std::vector<LightSpec> lights;
        std::vector<BaseCamera> cameras;
        /** Meshes whose surface becomes the ground (STL, OBJ or glTF), placed in the base frame. */
        std::vector<TerrainMeshSpec> terrain_meshes;

        // Filled in once the terrain exists.
        sl::dvec3 direction{0.0, 1.0, 0.0};
        /** North pole of the body in world axes, which sets the base's east and north. */
        sl::dvec3 pole{0.0, 1.0, 0.0};
        double height = 0.0;
    };

    inline constexpr double base_draw_range = 15000.0;

    inline bool base_in_draw_range(const Base &base, const sl::dvec3 &body_centre, double radius,
                                   const sl::dvec3 &eye, double range)
    {
        const sl::dvec3 position = body_centre + base.direction * (radius + base.height);
        const double distance = glm::length(position - eye);
        const double limit = range + 2.0 * base.flat_radius + base.blend;
        return !std::isfinite(distance) || !std::isfinite(limit) || distance <= limit;
    }

    /** Read "bases" from a JSON file. Appends to out; on failure returns false and sets error. */
    bool load_bases(const std::string &file, std::vector<Base> &out, std::string &error);
    /** Parse "bases" JSON text; `file` is only used in error messages. */
    bool parse_bases(const std::string &text, const std::string &file, std::vector<Base> &out, std::string &error);
    /** The bases as the JSON text load_bases reads back. */
    std::string bases_to_json(const std::vector<Base> &bases);
    /** Write bases to `file`, creating folders as needed. */
    bool save_bases(const std::string &file, const std::vector<Base> &bases, std::string &error);
    /** Position of a world point in a base's frame (x right, z forward, metres). */
    void base_local_xz(const Universe &universe, const Base &base, const sl::dvec3 &world_point, double &x, double &z);
    /** Where the user's models live; model paths that are not found beside the base file are looked for here. */
    void set_base_model_folder(const std::string &folder);
    /** The data folder; model paths such as parts/nasa_vab/nasa_vab.glb are looked for under it last, so shipped models can be placed in a base. */
    void set_stock_model_folder(const std::string &folder);
    /** The shipped models (parts/<name>/<name>.glb under the data folder) as paths relative to it. */
    std::vector<std::string> stock_base_models();
    /** True when a resolved path lies inside the data folder, where files must not be edited in place. */
    bool is_stock_model(const std::string &path);
    /** The file a model object refers to, or an empty string when it is not found. */
    std::string resolve_base_model(const Base &base, const std::string &model);
    /** A builder-made object's mesh in its own frame (scale not applied), or false for "model" and unknown types. */
    bool base_object_mesh(const BaseObject &object, sl::MeshData &mesh);
    /**
     * Every copy of an object with its place, heading and scale baked in, in the frame of a "model" object at the base
     * origin (+Z forward, +X left); a model object at x = z = 0 then shows exactly what the original did.
     */
    bool bake_base_object(const BaseObject &object, sl::MeshData &mesh);
    /** The prop types a base can hold. */
    const std::vector<std::string> &base_object_types();
    /** Load every .json file in a folder. Errors are appended to errors. */
    std::vector<Base> load_bases_in(const std::string &folder, std::vector<std::string> &errors);

    /** Unit direction of a base on its body, in the world frame. */
    sl::dvec3 base_direction(const Universe &universe, int body, const Base &base);

    /** Register a base's radio aids: its navaids and one ILS per listed runway direction. */
    void register_base_beacons(NavSystem &nav, const Universe &universe, int body, const Base &base);

    /** Where a ship starts on a runway, a little past the threshold, and its compass heading in degrees. */
    void base_runway_start(const Base &base, const Runway &runway, bool reverse, double radius, sl::dvec3 &direction, double &heading, double along = 40.0);


    /**
     * Where a base camera stands, in world coordinates, and the way it looks. With `target` set and the camera tracking,
     * it looks at that world point; otherwise along its own yaw and pitch.
     */
    void base_camera_pose(const Universe &universe, const Base &base, const BaseCamera &camera, const sl::dvec3 *target, sl::dvec3 &eye, sl::dvec3 &forward, sl::dvec3 &up);

    /** Ground position of a point in a base's frame (x right, z forward): direction of the point from the body centre. */
    sl::dvec3 base_point(const Base &base, double radius, double x, double z);
    /** Where a landed ship stands on a base: direction and compass heading in degrees. */
    void base_pad(const Base &base, double radius, sl::dvec3 &direction, double &heading);

    /** Metres the pad's concrete stands above the levelled ground at the base's pad spot (0 if it has no paved pad there). */
    double base_pad_deck(const Base &base);

    /** A level capsule: ground at `height` within `flat` metres of the segment a-b (unit directions), easing out over `blend`. */
    struct FlattenSite
    {
        sl::dvec3 a, b;
        double height, flat, blend;
    };

    /** Ground levelling for a base: its circle plus a strip under each runway. */
    std::vector<FlattenSite> flatten_sites(const Base &base, double radius);

    /** Wraps a terrain source and levels the ground around each base. */
    class FlattenSource final : public sl::TerrainSource
    {
    public:
        using Site = FlattenSite;

        FlattenSource(std::shared_ptr<sl::TerrainSource> source, double radius, std::vector<Site> sites);

        double height(double x, double z) const override { return source_->height(x, z); }
        double height_on_sphere(const sl::dvec3 &direction, double radius) const override;
        double height_on_sphere_lod(const sl::dvec3 &direction, double radius, double cell_size) const override;
        double min_height() const override { return min_; }
        double max_height() const override { return max_; }
        bool take_dirty_region(double &x0, double &z0, double &x1, double &z1) override { return source_->take_dirty_region(x0, z0, x1, z1); }

    private:
        double apply(const sl::dvec3 &direction, double natural) const;

        std::shared_ptr<sl::TerrainSource> source_;
        double radius_, min_, max_;
        std::vector<Site> sites_;
    };

    /** How dark it is at a base, 0 in full day to 1 at night, from the Sun's height over its horizon. */
    double base_night(const Universe &universe, const Base &base);
    /**
     * The lamps of every base within `range` metres of `eye`, in the camera-relative space the props are drawn in. Lamps
     * come on at dusk unless marked `always`; a lamp that aims at a ship picks the nearest of `ships` (world positions).
     */
    void collect_base_lights(const Universe &universe, const std::vector<Base> &bases, const sl::dvec3 &eye, double range, double time,
                             const std::vector<sl::dvec3> &ships, std::vector<PlacedLight> &out);

    /** Builds and draws the props of the bases. Meshes are made on first use and kept. */
    class BaseRenderer
    {
    public:
        ~BaseRenderer();
        /**
         * Draw every base object within `range` metres of `eye`. `body_position` is the centre of the body a base is on;
         * the camera-relative translation is done in double precision. Call between begin_surface_3d and the end.
         */
        void draw(const Universe &universe, const std::vector<Base> &bases, const sl::dvec3 &eye, double range, const sl::Material &material);
        void release();
        /** Game root, used to find the shared tarmac texture. */
        std::string root;

    private:
        sl::Bitmap *tarmac();
        sl::Bitmap *tarmac_ = nullptr;
        bool tarmac_tried_ = false;
        std::set<sl::Mesh *> paved_;
        sl::Mesh *mesh_for(const BaseObject &object);
        sl::Model *model_for(const Base &base, const BaseObject &object);
        void reload_changed_models();
        struct LoadedModel
        {
            sl::Model *model = nullptr;
            std::string path;
            std::filesystem::file_time_type stamp{};
        };
        std::map<std::string, sl::Mesh *> meshes_;
        std::map<std::string, LoadedModel> models_;
        double last_poll_ = 0.0;
    };
}
