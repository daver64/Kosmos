// ShipEdit's 3D preview: a small software renderer on cairo (shaded triangles, painter's order), so it needs no OpenGL and can also draw to a PNG.
#pragma once

#include "model.h"

#include <cairo.h>

#include <map>
#include <memory>

namespace shipedit
{
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
    };

    /** Meshes read from disk once; a file that fails is remembered with its message. */
    class MeshCache
    {
    public:
        const Mesh *get(const fs::path &file, std::string &error);
        void clear() { cache_.clear(); }

    private:
        struct Entry
        {
            Mesh mesh;
            bool ok = false;
            std::string error;
        };
        std::map<std::string, Entry> cache_;
    };

    void draw_preview(cairo_t *cr, int width, int height, const Document &doc, const Selection &sel, const PreviewView &view, MeshCache &meshes);
} // namespace shipedit
