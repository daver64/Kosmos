// KosmosCAD's drawing: every pixel of the four views, the grids, the gizmo and the overlays is OpenGL (one GtkGLArea, four scissored
// viewports). Nothing here uses cairo. It reads the Editor and never changes it.
#pragma once

#include "editor.h"

#include "image.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace cad
{
    struct Rect { int x = 0, y = 0, w = 0, h = 0; };

    /** Layout of the four views inside a W x H widget (logical pixels, y down). `maximised` is a view index, or -1 for the 2 x 2 grid. */
    Rect view_rect(int view, int W, int H, int maximised, bool uv = false);
    /** The view under a point, or -1. In UV layout view 4 (the UV editor) fills the left half and the perspective view (3) the right. */
    int view_at(double x, double y, int W, int H, int maximised, bool uv = false);

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
        bool uv = false;                // the UV editor layout: view 4 on the left, the perspective view on the right
        int uv_material = 0;            // the material whose image the UV editor shows
        double uv_cx = 0.5, uv_cy = 0.5, uv_zoom = 300; // the UV view's centre (UV units) and pixels per UV unit
        bool checker = false;           // show the checker instead of the texture images
        bool clean = false;             // no grid, overlays or selection tint (render mode)
    };

    class GlView
    {
    public:
        using ImageLoader = std::function<bool(const std::string &path, Image &out)>;
        /** How texture files are read (set by the app: gdk-pixbuf plus the DDS decoder), and the folder relative paths are resolved against. */
        ImageLoader loader;
        std::string base_dir;
        /** Forget cached textures (after a material's texture path or the base folder changes). */
        void drop_textures();
        /** Pixel size of a material's texture, or 0 x 0 when it has none or could not be read. */
        void texture_size(const Editor &ed, int material, int &w, int &h);
        bool init();
        void release();
        /** Draws into the currently bound framebuffer. W, H are logical pixels; `scale` is the device pixel ratio. */
        void render(const Editor &ed, const Camera cams[4], const ViewState &vs, int W, int H, int scale);
        /** Reads the last rendered frame (RGBA, top row first). */
        std::vector<unsigned char> read_pixels(int w, int h) const;

    private:
        struct Range { int material, first, count; };
        struct ObjectBuffers { unsigned vao = 0, tri = 0, line = 0; int tri_count = 0, line_count = 0; std::vector<Range> ranges; };
        struct Tex { unsigned id = 0; int w = 0, h = 0; };
        std::map<std::string, Tex> textures_;
        int u_mode_ = -1;
        unsigned program_ = 0, scratch_vao_ = 0, scratch_vbo_ = 0;
        int u_mvp_ = -1, u_model_ = -1, u_normal_ = -1, u_mul_ = -1, u_point_ = -1;
        std::vector<ObjectBuffers> objects_;
        unsigned built_gen_ = 0;
        size_t built_count_ = 0;
        int H_ = 0;                     // widget height in logical pixels, for the GL viewport origin

        void rebuild(const Editor &ed);
        const Tex *texture_for(const Editor &ed, int material);
        void draw_uv(const Editor &ed, const Rect &r, const ViewState &vs, int scale);
        void free_objects();
        void draw_view(const Editor &ed, const Camera &cam, const Rect &r, int view, const ViewState &vs, int scale);
        void overlay_2d(const Editor &ed, const Camera &cam, const Rect &r, int view, const ViewState &vs);
        void set_matrices(const float mvp[16], const Xform *model);
        void draw_batch(const std::vector<float> &data, unsigned mode, float r, float g, float b, float a = 1, float point = 1);
    };
}
