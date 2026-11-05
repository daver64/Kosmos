#include "gl_view.h"

#include <epoxy/gl.h>

#include <cstdio>

namespace kmview
{
    namespace
    {
        const char *const kVertex = R"(#version 150
in vec2 pos;
in vec3 col;
uniform vec2 scale;
uniform vec2 offset;
out vec3 c;
void main() { gl_Position = vec4((pos + offset) * scale, 0.0, 1.0); c = col; })";
        const char *const kFragment = R"(#version 150
in vec3 c;
out vec4 frag;
void main() { frag = vec4(c, 1.0); })";

        unsigned compile(unsigned type, const char *src)
        {
            const unsigned s = glCreateShader(type);
            glShaderSource(s, 1, &src, nullptr);
            glCompileShader(s);
            int ok = 0;
            glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
            if (!ok)
            {
                char log[512];
                glGetShaderInfoLog(s, sizeof log, nullptr, log);
                std::fprintf(stderr, "kosmosmission: shader error: %s\n", log);
            }
            return s;
        }
    } // namespace

    bool Renderer::init()
    {
        const unsigned vs = compile(GL_VERTEX_SHADER, kVertex), fs = compile(GL_FRAGMENT_SHADER, kFragment);
        program_ = glCreateProgram();
        glAttachShader(program_, vs);
        glAttachShader(program_, fs);
        glBindAttribLocation(program_, 0, "pos");
        glBindAttribLocation(program_, 1, "col");
        glLinkProgram(program_);
        glDeleteShader(vs);
        glDeleteShader(fs);
        int ok = 0;
        glGetProgramiv(program_, GL_LINK_STATUS, &ok);
        if (!ok)
            return false;
        u_scale_ = glGetUniformLocation(program_, "scale");
        u_offset_ = glGetUniformLocation(program_, "offset");
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void *>(0));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void *>(2 * sizeof(float)));
        glBindVertexArray(0);
        return true;
    }

    void Renderer::release()
    {
        if (vbo_)
            glDeleteBuffers(1, &vbo_);
        if (vao_)
            glDeleteVertexArrays(1, &vao_);
        if (program_)
            glDeleteProgram(program_);
        vbo_ = vao_ = program_ = 0;
    }

    void Renderer::draw(const Scene &scene, int w, int h)
    {
        glViewport(0, 0, w, h);
        glClearColor(0.04f, 0.05f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        if (!program_)
            return;
        glUseProgram(program_);
        glUniform2f(u_scale_, scene.scale_x, scene.scale_y);
        glUniform2f(u_offset_, scene.offset_x, scene.offset_y);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        if (!scene.tris.empty())
        {
            glBufferData(GL_ARRAY_BUFFER, scene.tris.size() * sizeof(Vertex), scene.tris.data(), GL_STREAM_DRAW);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<int>(scene.tris.size()));
        }
        if (!scene.lines.empty())
        {
            glBufferData(GL_ARRAY_BUFFER, scene.lines.size() * sizeof(Vertex), scene.lines.data(), GL_STREAM_DRAW);
            glDrawArrays(GL_LINES, 0, static_cast<int>(scene.lines.size()));
        }
        glBindVertexArray(0);
    }
} // namespace kmview
