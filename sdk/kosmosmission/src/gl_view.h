// KosmosMission's GL drawing: polylines and filled triangles in one GtkGLArea, nothing from cairo. The views (an orbit/trajectory plot and a porkchop
// grid) are built by the app as lists of coloured vertices in plot units; this only draws them.
#pragma once

#include <vector>

namespace kmview
{
    struct Vertex
    {
        float x, y, r, g, b;
    };

    struct Scene
    {
        std::vector<Vertex> lines; // pairs of vertices
        std::vector<Vertex> tris;  // triples of vertices
        float scale_x = 1, scale_y = 1, offset_x = 0, offset_y = 0;
    };

    class Renderer
    {
      public:
        bool init();
        void release();
        /** Draws `scene` into the bound framebuffer of w x h pixels. */
        void draw(const Scene &scene, int w, int h);

      private:
        unsigned program_ = 0, vao_ = 0, vbo_ = 0;
        int u_scale_ = -1, u_offset_ = -1;
    };
} // namespace kmview
