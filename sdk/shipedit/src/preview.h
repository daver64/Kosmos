// ShipEdit's 3D preview: a small software renderer on cairo (shaded triangles, painter's order), so it needs no OpenGL and can also draw to a PNG.
#pragma once

#include "model.h"

#include <cairo.h>

#include <map>
#include <memory>
#include <vector>

namespace shipedit
{
    struct V3
    {
        double x = 0, y = 0, z = 0;
    };

    /** One part's triangles (3 points each) in the ship frame, with a colour. */
    struct Soup
    {
        std::vector<V3> p;
        double r = 0.6, g = 0.65, b = 0.72;
    };

    struct Item
    {
        Soup soup;
        int stage, part;
        std::vector<float> light; // per triangle
    };

    /** The ship's parts placed in the ship frame, kept between frames until the stages or the thinning change. */
    struct Geometry
    {
        std::vector<Item> items;
        double zmin = 0, zmax = 0, rmax = 0;
        std::size_t triangles = 0;
        unsigned generation = 0; // changes whenever the triangles do
    };

    /** The orthographic view: ship frame to screen pixels, as the preview's own renderers use it. */
    struct Projection
    {
        double cy, sy, cp, sp, scale, cx, cyc, centre_z, depth_range;
    };

    /** What is selected in the tree, so the preview can highlight it. */
    struct Selection
    {
        enum Kind { none, ship, stage_node, part, engine, gear } kind = none;
        int stage = -1, item = -1;
    };

    struct PreviewView
    {
        double yaw = 1.25, pitch = 0.22, zoom = 1.0;
        double pan_x = 0, pan_y = 0;
        bool interactive = false; // while dragging, big models are thinned harder so the view stays smooth
        bool thin_when_dragging = true;
        std::size_t triangle_budget = 150000; // models with more triangles than this are thinned
    };

    /** Meshes read from disk once; a file that fails is remembered with its message. */
    class MeshCache
    {
    public:
        const Mesh *get(const fs::path &file, std::string &error);
        void clear()
        {
            cache_.clear();
            geometry.reset();
            geometry_key.clear();
        }
        /** The ship's triangles placed in the ship frame, kept between frames until the stages or the thinning change. */
        std::shared_ptr<Geometry> geometry;
        std::string geometry_key;

    private:
        struct Entry
        {
            Mesh mesh;
            bool ok = false;
            std::string error;
        };
        std::map<std::string, Entry> cache_;
    };

    /** The ship's triangles, rebuilt only when something they depend on has changed; null when there are no stages. */
    std::shared_ptr<const Geometry> prepare_geometry(const Document &doc, const PreviewView &view, MeshCache &meshes);
    Projection projection(const Geometry &geo, int width, int height, const PreviewView &view);

    /** Draws the preview; with `solids` false only the overlay (stage outlines, engines, labels) goes on, over a GL-drawn ship. */
    void draw_preview(cairo_t *cr, int width, int height, const Document &doc, const Selection &sel, const PreviewView &view, MeshCache &meshes, bool solids = true);
} // namespace shipedit
