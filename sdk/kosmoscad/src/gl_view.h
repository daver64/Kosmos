// KosmosCAD's drawing: every pixel of the four views, the grids, the gizmo and the overlays is OpenGL (one GtkGLArea, four scissored
// viewports). Nothing here uses cairo. It reads the Editor and never changes it.
#pragma once

#include "editor.h"

#include <vector>

namespace cad
{
    struct Rect { int x = 0, y = 0, w = 0, h = 0; };

    /** Layout of the four views inside a W x H widget (logical pixels, y down). `maximised` is a view index, or -1 for the 2 x 2 grid. */
    Rect view_rect(int view, int W, int H, int maximised);
    /** The view under a point, or -1. */
    int view_at(double x, double y, int W, int H, int maximised);

    struct ViewState
    {
        int active = 3;                 // highlighted view
        int maximised = -1;
        int hover_axis = -1;            // gizmo handle under the cursor
        bool grid = true, edges = true;
        bool box = false;               // box-select rectangle in `active` view coordinates
        double bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
        bool sketching = false;
        double cursor_x = 0, cursor_y = 0;
    };

    class GlView
    {
    public:
        bool init();
        void release();
        /** Draws into the currently bound framebuffer. W, H are logical pixels; `scale` is the device pixel ratio. */
        void render(const Editor &ed, const Camera cams[4], const ViewState &vs, int W, int H, int scale);
        /** Reads the last rendered frame (RGBA, top row first). */
        std::vector<unsigned char> read_pixels(int w, int h) const;

    private:
        struct ObjectBuffers { unsigned vao = 0, tri = 0, line = 0; int tri_count = 0, line_count = 0; };
        unsigned program_ = 0, scratch_vao_ = 0, scratch_vbo_ = 0;
        int u_mvp_ = -1, u_model_ = -1, u_normal_ = -1, u_mul_ = -1, u_point_ = -1;
        std::vector<ObjectBuffers> objects_;
        unsigned built_gen_ = 0;
        size_t built_count_ = 0;
        int H_ = 0;                     // widget height in logical pixels, for the GL viewport origin

        void rebuild(const Editor &ed);
        void free_objects();
        void draw_view(const Editor &ed, const Camera &cam, const Rect &r, int view, const ViewState &vs, int scale);
        void overlay_2d(const Editor &ed, const Camera &cam, const Rect &r, int view, const ViewState &vs);
        void set_matrices(const float mvp[16], const Xform *model);
        void draw_batch(const std::vector<float> &data, unsigned mode, float r, float g, float b, float a = 1, float point = 1);
    };
}
