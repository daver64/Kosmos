// The ship's solid surfaces drawn by the graphics card: the same orthographic view and shading as the cairo preview, without the per-triangle CPU work.
// The overlay (stage outlines, engines, labels) is still drawn by cairo on top. Every call needs the GL context current.
#pragma once

#include "preview.h"

namespace shipedit
{
    class GlPreview
    {
    public:
        ~GlPreview();
        /** Builds the shader and buffers; false when this context cannot do it. */
        bool init();
        /** Frees the GL objects while the context is still current. */
        void release();
        /** Draws into the framebuffer that is bound now, `width` x `height` in widget units (`scale` device pixels each). */
        void draw(const Geometry &geo, const Projection &pj, const Selection &sel, int width, int height, int scale);

    private:
        struct Range
        {
            int first, count, stage, part;
        };
        void upload(const Geometry &geo);
        void size_targets(int pixel_width, int pixel_height);

        unsigned program_ = 0, vao_ = 0, bg_vao_ = 0, vbo_ = 0, fbo_ = 0, colour_rb_ = 0, depth_rb_ = 0;
        int target_w_ = 0, target_h_ = 0;
        unsigned uploaded_ = 0;
        std::vector<Range> ranges_;
    };
} // namespace shipedit
