#pragma once

#include "terrain.h"

#include <memory>
#include <string>
#include <vector>

namespace kosmos
{
    struct Base;

    /** A mesh placed on the ground of a base whose surface becomes the terrain there ("terrain_meshes" in a base file). */
    struct TerrainMeshSpec
    {
        /** An .stl, .obj, .glb or .gltf; found beside the base file, in the model folders, or from the game folder. */
        std::string file;
        /** Where the middle of the mesh stands in the base frame (metres, x right, z forward) and its heading in degrees. */
        double x = 0.0, z = 0.0, heading = 0.0;
        /** Metres per mesh unit, sideways and for heights (vscale multiplies scale for heights). */
        double scale = 1.0, vscale = 1.0;
        /** Metres the lowest point of the mesh stands above the base's ground level. */
        double y = 0.0;
        /** Metres inside the mesh's rim over which it eases into the natural ground. */
        double blend = 20.0;
        /** Which mesh axis is up: "z" (STL, CAD) or "y" (glTF, OBJ). Empty picks z for STL and y otherwise. */
        std::string up;
        /**
         * Surface colour: "auto" uses the mesh's own textures or vertex colours (or `image`) when it has them and the
         * terrain's colouring otherwise; "mesh" insists on the mesh's own; "terrain" keeps the terrain's colouring.
         */
        std::string texture = "auto";
        /** A picture draped over the mesh seen from above (for meshes with no texture of their own, such as STL). */
        std::string image;
    };

    /** A mesh rasterised to a regular grid of heights (and colours), in metres around its middle. */
    struct MeshGrid
    {
        int width = 0, height = 0;
        /** Metres between samples. */
        double cell = 1.0;
        /** Heights above the lowest point; NaN where the mesh has no surface. */
        std::vector<float> heights;
        /** 3 bytes per sample, empty if the mesh has no colour. */
        std::vector<unsigned char> colours;
        double max_height = 0.0;
    };

    /** Read and rasterise the mesh. `path` is the resolved file. The topmost surface wins where the mesh overlaps itself. */
    bool load_mesh_grid(const TerrainMeshSpec &spec, const std::string &path, const std::string &image_path, MeshGrid &out, std::string &error);

    /** Wraps a terrain source and replaces the ground inside each placed mesh by the mesh's surface. */
    class MeshSource final : public sl::TerrainSource
    {
    public:
        struct Placed
        {
            sl::dvec3 origin;                 // unit direction of the base centre
            sl::dvec3 right, forward;         // base axes at the centre
            double x, z, heading, blend;      // as in the spec
            double datum;                     // height of the mesh's lowest point
            bool use_colour;
            std::shared_ptr<MeshGrid> grid;
            std::vector<std::vector<float>> mips; // coarser levels of grid->heights, halving each time
        };

        MeshSource(std::shared_ptr<sl::TerrainSource> source, double radius, std::vector<Placed> meshes);

        double height(double x, double z) const override { return source_->height(x, z); }
        double height_on_sphere(const sl::dvec3 &direction, double radius) const override;
        double height_on_sphere_lod(const sl::dvec3 &direction, double radius, double cell_size) const override;
        double min_height() const override { return min_; }
        double max_height() const override { return max_; }
        bool take_dirty_region(double &x0, double &z0, double &x1, double &z1) override { return source_->take_dirty_region(x0, z0, x1, z1); }

        /** The mesh's own colour at `direction`, blended with `rgb` (the terrain's colour) over the rim; false outside every mesh that has colour. */
        bool colour(const sl::dvec3 &direction, float rgb[3]) const;
        /** Whether a spherical cap could intersect a coloured mesh's projected footprint. */
        bool colour_may_overlap(const sl::dvec3 &centre, double angular_radius) const;
        /** Centre (unit direction) and radius in metres of each mesh's footprint, for keeping clutter off it. */
        std::vector<std::pair<sl::dvec3, double>> footprints() const;

        /** Build the placement of a mesh from its grid. */
        static Placed place(const Base &base, double radius, const TerrainMeshSpec &spec, std::shared_ptr<MeshGrid> grid, double datum);

    private:
        double apply(const sl::dvec3 &direction, double natural, double cell_size) const;

        std::shared_ptr<sl::TerrainSource> source_;
        double radius_, min_, max_;
        std::vector<Placed> meshes_;
    };
}
