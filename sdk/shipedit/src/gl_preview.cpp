#include "gl_preview.h"

#include <epoxy/gl.h>

#include <algorithm>

namespace shipedit
{
    namespace
    {
        // The ship frame is X right, Y up, Z back; the view turns about Y then tips about X (as Camera::view in preview.cpp), and the
        // result goes straight to pixels, so the two renderers agree. A nearer point has a larger view z, and GL_LESS keeps the smaller depth.
        const char *vertex_source = R"(#version 150 core
in vec3 aPosition;
in vec3 aColour;
uniform vec4 uTurn;     // cos yaw, sin yaw, cos pitch, sin pitch
uniform vec4 uScreen;   // scale, cx, cy (pixels), centre z
uniform vec3 uSize;     // width, height, depth range
uniform int uBackground;
out vec3 vColour;
out float vHeight;
void main()
{
    if (uBackground != 0)
    {
        vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
        gl_Position = vec4(p * 2.0 - 1.0, 0.999, 1.0);
        vHeight = 1.0 - p.y;
        vColour = vec3(0.0);
        return;
    }
    vec3 p = aPosition - vec3(0.0, 0.0, uScreen.w);
    float x1 = p.x * uTurn.x + p.z * uTurn.y;
    float z1 = -p.x * uTurn.y + p.z * uTurn.x;
    vec3 v = vec3(x1, p.y * uTurn.z - z1 * uTurn.w, p.y * uTurn.w + z1 * uTurn.z);
    float sx = uScreen.y + v.x * uScreen.x;
    float sy = uScreen.z - v.y * uScreen.x;
    gl_Position = vec4(sx / uSize.x * 2.0 - 1.0, 1.0 - sy / uSize.y * 2.0, -v.z / uSize.z, 1.0);
    vColour = aColour;
    vHeight = 0.0;
}
)";
        const char *fragment_source = R"(#version 150 core
in vec3 vColour;
in float vHeight;
uniform vec3 uTint;     // brightness factor, then the highlight lift
uniform int uBackground;
out vec4 fragment;
void main()
{
    if (uBackground != 0)
    {
        fragment = vec4(mix(vec3(0.05, 0.06, 0.09), vec3(0.10, 0.12, 0.16), vHeight), 1.0);
        return;
    }
    fragment = vec4(min(vec3(1.0), vColour * uTint.x + uTint.y * vec3(0.6, 0.45, 0.0)), 1.0);
}
)";

        unsigned compile(unsigned kind, const char *source)
        {
            const unsigned shader = glCreateShader(kind);
            glShaderSource(shader, 1, &source, nullptr);
            glCompileShader(shader);
            int ok = 0;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
            if (!ok)
            {
                glDeleteShader(shader);
                return 0;
            }
            return shader;
        }
    } // namespace

    GlPreview::~GlPreview() = default;

    bool GlPreview::init()
    {
        const unsigned vs = compile(GL_VERTEX_SHADER, vertex_source), fs = compile(GL_FRAGMENT_SHADER, fragment_source);
        if (!vs || !fs)
            return false;
        program_ = glCreateProgram();
        glAttachShader(program_, vs);
        glAttachShader(program_, fs);
        glBindAttribLocation(program_, 0, "aPosition");
        glBindAttribLocation(program_, 1, "aColour");
        glLinkProgram(program_);
        glDeleteShader(vs);
        glDeleteShader(fs);
        int ok = 0;
        glGetProgramiv(program_, GL_LINK_STATUS, &ok);
        if (!ok)
            return false;
        glGenVertexArrays(1, &vao_);
        glGenVertexArrays(1, &bg_vao_);
        glGenBuffers(1, &vbo_);
        return true;
    }

    void GlPreview::release()
    {
        if (fbo_)
            glDeleteFramebuffers(1, &fbo_);
        if (colour_rb_)
            glDeleteRenderbuffers(1, &colour_rb_);
        if (depth_rb_)
            glDeleteRenderbuffers(1, &depth_rb_);
        if (vbo_)
            glDeleteBuffers(1, &vbo_);
        if (vao_)
            glDeleteVertexArrays(1, &vao_);
        if (bg_vao_)
            glDeleteVertexArrays(1, &bg_vao_);
        if (program_)
            glDeleteProgram(program_);
        fbo_ = colour_rb_ = depth_rb_ = vbo_ = vao_ = bg_vao_ = program_ = 0;
        target_w_ = target_h_ = 0;
        uploaded_ = 0;
    }

    void GlPreview::upload(const Geometry &geo)
    {
        std::vector<float> data;
        data.reserve(geo.triangles * 18);
        ranges_.clear();
        for (const Item &it : geo.items)
        {
            const int first = static_cast<int>(data.size() / 6);
            for (std::size_t i = 0; i < it.soup.p.size(); ++i)
            {
                const double light = it.light[i / 3];
                data.insert(data.end(), {static_cast<float>(it.soup.p[i].x), static_cast<float>(it.soup.p[i].y), static_cast<float>(it.soup.p[i].z),
                                         static_cast<float>(it.soup.r * light), static_cast<float>(it.soup.g * light), static_cast<float>(it.soup.b * light)});
            }
            ranges_.push_back({first, static_cast<int>(it.soup.p.size()), it.stage, it.part});
        }
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.size() * sizeof(float)), data.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void *>(3 * sizeof(float)));
        uploaded_ = geo.generation;
    }

    // Smooth edges come from a multisampled target that is resolved into the widget's framebuffer.
    void GlPreview::size_targets(int pixel_width, int pixel_height)
    {
        if (fbo_ && target_w_ == pixel_width && target_h_ == pixel_height)
            return;
        if (!fbo_)
        {
            glGenFramebuffers(1, &fbo_);
            glGenRenderbuffers(1, &colour_rb_);
            glGenRenderbuffers(1, &depth_rb_);
        }
        int samples = 0;
        glGetIntegerv(GL_MAX_SAMPLES, &samples);
        samples = std::min(samples, 4);
        glBindRenderbuffer(GL_RENDERBUFFER, colour_rb_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, pixel_width, pixel_height);
        glBindRenderbuffer(GL_RENDERBUFFER, depth_rb_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, pixel_width, pixel_height);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, colour_rb_);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_rb_);
        target_w_ = pixel_width;
        target_h_ = pixel_height;
    }

    void GlPreview::draw(const Geometry &geo, const Projection &pj, const Selection &sel, int width, int height, int scale)
    {
        if (!program_)
            return;
        if (uploaded_ != geo.generation)
            upload(geo);
        const int pw = width * scale, ph = height * scale;
        int previous = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
        size_targets(pw, ph);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        const bool multisampled = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        if (!multisampled)
            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned>(previous));

        glViewport(0, 0, pw, ph);
        glDisable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glClearDepth(1.0);
        glClear(GL_DEPTH_BUFFER_BIT);
        glUseProgram(program_);
        glUniform4f(glGetUniformLocation(program_, "uTurn"), static_cast<float>(pj.cy), static_cast<float>(pj.sy), static_cast<float>(pj.cp), static_cast<float>(pj.sp));
        glUniform4f(glGetUniformLocation(program_, "uScreen"), static_cast<float>(pj.scale), static_cast<float>(pj.cx), static_cast<float>(pj.cyc), static_cast<float>(pj.centre_z));
        glUniform3f(glGetUniformLocation(program_, "uSize"), static_cast<float>(width), static_cast<float>(height), static_cast<float>(pj.depth_range));
        const int background = glGetUniformLocation(program_, "uBackground"), tint = glGetUniformLocation(program_, "uTint");

        glBindVertexArray(bg_vao_);
        glUniform1i(background, 1);
        glDepthMask(GL_FALSE);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glDepthMask(GL_TRUE);
        glUniform1i(background, 0);
        glBindVertexArray(vao_);
        for (const Range &r : ranges_)
        {
            const bool stage_selected = sel.stage == r.stage;
            const float brightness = sel.kind == Selection::none || stage_selected ? 1.0f : 0.55f;
            const bool highlight = sel.kind == Selection::part && stage_selected && sel.item == r.part;
            glUniform3f(tint, brightness, highlight ? 0.25f : 0.0f, 0.0f);
            glDrawArrays(GL_TRIANGLES, r.first, r.count);
        }

        if (multisampled)
        {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<unsigned>(previous));
            glBlitFramebuffer(0, 0, pw, ph, 0, 0, pw, ph, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned>(previous));
        }
        glBindVertexArray(0);
        glUseProgram(0);
    }
} // namespace shipedit
