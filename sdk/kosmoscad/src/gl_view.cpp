#include "gl_view.h"

#include <epoxy/gl.h>

#include <algorithm>
#include <cmath>

namespace cad
{
    namespace
    {
        const char *vertex_source = R"(#version 150 core
in vec3 aPos;
in vec3 aNormal;
in vec3 aColour;
uniform mat4 uMVP;
uniform mat4 uModel;
uniform mat3 uNormal;
uniform float uPoint;
out vec3 vColour;
out vec3 vNormal;
void main()
{
    gl_Position = uMVP * (uModel * vec4(aPos, 1.0));
    gl_PointSize = uPoint;
    vColour = aColour;
    vNormal = uNormal * aNormal;
}
)";
        const char *fragment_source = R"(#version 150 core
in vec3 vColour;
in vec3 vNormal;
uniform vec4 uMul;
out vec4 fragment;
void main()
{
    float lit = 1.0;
    if (dot(vNormal, vNormal) > 0.01)
        lit = 0.30 + 0.70 * abs(dot(normalize(vNormal), normalize(vec3(0.4, 0.8, 0.5))));
    fragment = vec4(vColour * lit * uMul.rgb, uMul.a);
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

        using Batch = std::vector<float>;

        void push(Batch &d, const V3 &p, const V3 &c, const V3 &n = {})
        {
            const float v[9] = {float(p.x), float(p.y), float(p.z), float(n.x), float(n.y), float(n.z), float(c.x), float(c.y), float(c.z)};
            d.insert(d.end(), v, v + 9);
        }
        void line(Batch &d, const V3 &a, const V3 &b, const V3 &c)
        {
            push(d, a, c);
            push(d, b, c);
        }

        // A screen-space line as a quad, so overlays can be thicker than GL's one-pixel lines.
        void thick(Batch &d, double ax, double ay, double bx, double by, double width, const V3 &c)
        {
            double dx = bx - ax, dy = by - ay;
            const double l = std::hypot(dx, dy);
            if (l < 1e-6)
                return;
            dx /= l;
            dy /= l;
            const double nx = -dy * width / 2, ny = dx * width / 2;
            const V3 p0{ax + nx, ay + ny, 0}, p1{ax - nx, ay - ny, 0}, p2{bx - nx, by - ny, 0}, p3{bx + nx, by + ny, 0};
            push(d, p0, c); push(d, p1, c); push(d, p2, c);
            push(d, p0, c); push(d, p2, c); push(d, p3, c);
        }
        void square(Batch &d, double cx, double cy, double r, const V3 &c)
        {
            const V3 p0{cx - r, cy - r, 0}, p1{cx + r, cy - r, 0}, p2{cx + r, cy + r, 0}, p3{cx - r, cy + r, 0};
            push(d, p0, c); push(d, p1, c); push(d, p2, c);
            push(d, p0, c); push(d, p2, c); push(d, p3, c);
        }
        void arrow_head(Batch &d, double cx, double cy, double ex, double ey, double size, const V3 &c)
        {
            double dx = ex - cx, dy = ey - cy;
            const double l = std::hypot(dx, dy);
            if (l < 1e-6)
                return;
            dx /= l;
            dy /= l;
            push(d, {ex + dx * size, ey + dy * size, 0}, c);
            push(d, {ex - dy * size * 0.5, ey + dx * size * 0.5, 0}, c);
            push(d, {ex + dy * size * 0.5, ey - dx * size * 0.5, 0}, c);
        }

        V3 axis_colour(int a) { return a == 0 ? V3(0.95, 0.3, 0.3) : (a == 1 ? V3(0.4, 0.9, 0.4) : V3(0.4, 0.55, 1.0)); }

        void model_matrix(const Xform &x, float out[16], float normal[9])
        {
            const V3 c0 = x.apply_dir({x.scale.x, 0, 0}), c1 = x.apply_dir({0, x.scale.y, 0}), c2 = x.apply_dir({0, 0, x.scale.z});
            const V3 cols[3] = {c0, c1, c2};
            for (int c = 0; c < 3; ++c)
            {
                out[c * 4 + 0] = float(cols[c].x);
                out[c * 4 + 1] = float(cols[c].y);
                out[c * 4 + 2] = float(cols[c].z);
                out[c * 4 + 3] = 0;
            }
            out[12] = float(x.pos.x);
            out[13] = float(x.pos.y);
            out[14] = float(x.pos.z);
            out[15] = 1;
            const V3 r0 = x.apply_dir({1, 0, 0}), r1 = x.apply_dir({0, 1, 0}), r2 = x.apply_dir({0, 0, 1});
            const V3 rc[3] = {r0, r1, r2};
            for (int c = 0; c < 3; ++c)
            {
                normal[c * 3 + 0] = float(rc[c].x);
                normal[c * 3 + 1] = float(rc[c].y);
                normal[c * 3 + 2] = float(rc[c].z);
            }
        }

        // Nice step (1, 2, 5 x 10^n metres) so grid lines are at least `min_px` apart at `px_per_metre`.
        double grid_step(double px_per_metre, double min_px)
        {
            const double base = std::pow(10.0, std::floor(std::log10(min_px / px_per_metre)));
            for (double m : {1.0, 2.0, 5.0, 10.0})
                if (base * m * px_per_metre >= min_px)
                    return base * m;
            return base * 10;
        }

        void grid_lines(const Camera &cam, int w, int h, Batch &d)
        {
            int ia = 0, ib = 2; // the plane's two axes: Top and Persp use the ground (X, Z)
            if (cam.kind == Camera::Front) { ia = 0; ib = 1; }
            else if (cam.kind == Camera::Side) { ia = 2; ib = 1; }
            const double ppm = cam.ortho() ? cam.scale : 40.0 / cam.dist;
            const double step = grid_step(ppm, 14);
            double R = cam.ortho() ? std::max(w, h) / 2.0 / cam.scale + step : std::min(cam.dist * 2.5, step * 60);
            R = std::min(R, step * 200);
            const double ca = cam.target[ia], cb = cam.target[ib];
            const V3 minor{0.20, 0.21, 0.24}, major{0.30, 0.32, 0.37};
            auto at = [&](double a, double b) {
                V3 p;
                p[ia] = a;
                p[ib] = b;
                return p;
            };
            for (int i = static_cast<int>(std::floor((ca - R) / step)); i <= static_cast<int>(std::ceil((ca + R) / step)); ++i)
                if (i != 0)
                    line(d, at(i * step, cb - R), at(i * step, cb + R), i % 10 == 0 ? major : minor);
            for (int i = static_cast<int>(std::floor((cb - R) / step)); i <= static_cast<int>(std::ceil((cb + R) / step)); ++i)
                if (i != 0)
                    line(d, at(ca - R, i * step), at(ca + R, i * step), i % 10 == 0 ? major : minor);
            line(d, at(ca - R, 0), at(ca + R, 0), axis_colour(ia) * 0.8);
            line(d, at(0, cb - R), at(0, cb + R), axis_colour(ib) * 0.8);
        }

        void marker_lines(const Marker &m, Batch &d, const V3 &col)
        {
            const KindInfo *k = find_kind(m.kind);
            const Shape sh = k ? k->shape : Shape::Box;
            const Xform xf{m.pos, m.rot, m.size};
            auto ln = [&](const V3 &a, const V3 &b) { line(d, xf.apply(a), xf.apply(b), col); };
            if (sh == Shape::Box)
            {
                for (int i = 0; i < 4; ++i)
                {
                    const double x = (i & 1) ? 0.5 : -0.5, y = (i & 2) ? 0.5 : -0.5;
                    ln({x, y, -0.5}, {x, y, 0.5});
                    ln({x, -0.5, y}, {x, 0.5, y});
                    ln({-0.5, x, y}, {0.5, x, y});
                }
            }
            else if (sh == Shape::Diamond)
            {
                const V3 pts[6] = {{0.5, 0, 0}, {-0.5, 0, 0}, {0, 0.5, 0}, {0, -0.5, 0}, {0, 0, 0.5}, {0, 0, -0.5}};
                for (int i = 0; i < 6; ++i)
                    for (int j = i + 1; j < 6; ++j)
                        if (i / 2 != j / 2)
                            ln(pts[i], pts[j]);
            }
            else
            {
                const bool z = sh == Shape::CylZ;
                const int n = 16;
                for (int i = 0; i < n; ++i)
                {
                    const double a0 = 2 * kPi * i / n, a1 = 2 * kPi * (i + 1) / n;
                    for (double t : {-0.5, 0.5})
                    {
                        const V3 p0 = z ? V3(0.5 * std::cos(a0), 0.5 * std::sin(a0), t) : V3(0.5 * std::cos(a0), t, 0.5 * std::sin(a0));
                        const V3 p1 = z ? V3(0.5 * std::cos(a1), 0.5 * std::sin(a1), t) : V3(0.5 * std::cos(a1), t, 0.5 * std::sin(a1));
                        ln(p0, p1);
                    }
                    if (i % 4 == 0)
                    {
                        const double c = 0.5 * std::cos(a0), s = 0.5 * std::sin(a0);
                        z ? ln({c, s, -0.5}, {c, s, 0.5}) : ln({c, -0.5, s}, {c, 0.5, s});
                    }
                }
            }
            if (k && k->has_direction)
            {
                const V3 dir = marker_direction(m);
                const double len = std::max({m.size.x, m.size.y, m.size.z}) * 1.2 + 0.6;
                const V3 e = m.pos + dir * len;
                const V3 up = std::fabs(dir.y) < 0.9 ? V3(0, 1, 0) : V3(1, 0, 0);
                const V3 side = normalized(cross(dir, up)) * (len * 0.12), back = e - dir * (len * 0.22);
                line(d, m.pos, e, col);
                line(d, e, back + side, col);
                line(d, e, back - side, col);
            }
        }
    } // namespace

    Rect view_rect(int view, int W, int H, int maximised)
    {
        if (maximised >= 0)
            return view == maximised ? Rect{0, 0, W, H} : Rect{};
        const int gap = 2, cw = (W - gap) / 2, ch = (H - gap) / 2;
        const int col = view % 2, row = view / 2;
        return {col * (cw + gap), row * (ch + gap), col ? W - cw - gap : cw, row ? H - ch - gap : ch};
    }

    int view_at(double x, double y, int W, int H, int maximised)
    {
        for (int i = 0; i < 4; ++i)
        {
            const Rect r = view_rect(i, W, H, maximised);
            if (r.w > 0 && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h)
                return i;
        }
        return -1;
    }

    bool GlView::init()
    {
        const unsigned vs = compile(GL_VERTEX_SHADER, vertex_source), fs = compile(GL_FRAGMENT_SHADER, fragment_source);
        if (!vs || !fs)
            return false;
        program_ = glCreateProgram();
        glAttachShader(program_, vs);
        glAttachShader(program_, fs);
        glBindAttribLocation(program_, 0, "aPos");
        glBindAttribLocation(program_, 1, "aNormal");
        glBindAttribLocation(program_, 2, "aColour");
        glLinkProgram(program_);
        glDeleteShader(vs);
        glDeleteShader(fs);
        int ok = 0;
        glGetProgramiv(program_, GL_LINK_STATUS, &ok);
        if (!ok)
            return false;
        u_mvp_ = glGetUniformLocation(program_, "uMVP");
        u_model_ = glGetUniformLocation(program_, "uModel");
        u_normal_ = glGetUniformLocation(program_, "uNormal");
        u_mul_ = glGetUniformLocation(program_, "uMul");
        u_point_ = glGetUniformLocation(program_, "uPoint");
        glGenVertexArrays(1, &scratch_vao_);
        glGenBuffers(1, &scratch_vbo_);
        return true;
    }

    void GlView::free_objects()
    {
        for (ObjectBuffers &o : objects_)
        {
            if (o.vao)
                glDeleteVertexArrays(1, &o.vao);
            if (o.tri)
                glDeleteBuffers(1, &o.tri);
            if (o.line)
                glDeleteBuffers(1, &o.line);
        }
        objects_.clear();
    }

    void GlView::release()
    {
        free_objects();
        if (scratch_vbo_)
            glDeleteBuffers(1, &scratch_vbo_);
        if (scratch_vao_)
            glDeleteVertexArrays(1, &scratch_vao_);
        if (program_)
            glDeleteProgram(program_);
        program_ = scratch_vao_ = scratch_vbo_ = 0;
        built_gen_ = 0;
    }

    static void attribs()
    {
        for (int i = 0; i < 3; ++i)
        {
            glEnableVertexAttribArray(i);
            glVertexAttribPointer(i, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), reinterpret_cast<void *>(i * 3 * sizeof(float)));
        }
    }

    void GlView::rebuild(const Editor &ed)
    {
        free_objects();
        for (const Object &o : ed.scene().objects)
        {
            ObjectBuffers b;
            Batch tris, lines;
            // Smooth objects average the normals of the faces around each vertex; flat ones use the face normal.
            std::vector<V3> vn;
            if (o.smooth)
            {
                vn.assign(o.mesh.v.size(), V3());
                for (size_t f = 0; f < o.mesh.f.size(); ++f)
                {
                    const V3 n = face_normal(o.mesh, static_cast<int>(f));
                    for (int v : o.mesh.f[f].v)
                        vn[v] += n;
                }
                for (V3 &n : vn)
                    n = normalized(n);
            }
            for (size_t f = 0; f < o.mesh.f.size(); ++f)
            {
                const Face &face = o.mesh.f[f];
                const int mat = std::clamp(face.mat, 0, static_cast<int>(ed.scene().materials.size()) - 1);
                const V3 col = ed.scene().materials[mat].colour;
                const V3 fn = face_normal(o.mesh, static_cast<int>(f));
                for (const Tri &t : triangulate_face(o.mesh, static_cast<int>(f)))
                    for (int k = 0; k < 3; ++k)
                        push(tris, o.mesh.v[t[k]], col, o.smooth ? vn[t[k]] : fn);
            }
            for (const auto &ef : edge_faces(o.mesh))
                line(lines, o.mesh.v[ef.first.first], o.mesh.v[ef.first.second], V3(1, 1, 1));
            b.tri_count = static_cast<int>(tris.size() / 9);
            b.line_count = static_cast<int>(lines.size() / 9);
            glGenVertexArrays(1, &b.vao);
            glGenBuffers(1, &b.tri);
            glGenBuffers(1, &b.line);
            glBindBuffer(GL_ARRAY_BUFFER, b.tri);
            glBufferData(GL_ARRAY_BUFFER, tris.size() * sizeof(float), tris.data(), GL_STATIC_DRAW);
            glBindBuffer(GL_ARRAY_BUFFER, b.line);
            glBufferData(GL_ARRAY_BUFFER, lines.size() * sizeof(float), lines.data(), GL_STATIC_DRAW);
            objects_.push_back(b);
        }
        built_gen_ = ed.mesh_gen;
        built_count_ = ed.scene().objects.size();
    }

    void GlView::set_matrices(const float mvp[16], const Xform *model)
    {
        glUniformMatrix4fv(u_mvp_, 1, GL_FALSE, mvp);
        float m[16], n[9];
        model_matrix(model ? *model : Xform(), m, n);
        glUniformMatrix4fv(u_model_, 1, GL_FALSE, m);
        glUniformMatrix3fv(u_normal_, 1, GL_FALSE, n);
    }

    void GlView::draw_batch(const Batch &data, unsigned mode, float r, float g, float b, float a, float point)
    {
        if (data.empty())
            return;
        glUniform4f(u_mul_, r, g, b, a);
        glUniform1f(u_point_, point);
        glBindVertexArray(scratch_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, scratch_vbo_);
        glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_STREAM_DRAW);
        attribs();
        glDrawArrays(mode, 0, static_cast<int>(data.size() / 9));
    }

    void GlView::draw_view(const Editor &ed, const Camera &cam, const Rect &r, int view, const ViewState &vs, int scale)
    {
        const int dx = r.x * scale, dy = (H_ - r.y - r.h) * scale, dw = r.w * scale, dh = r.h * scale;
        glViewport(dx, dy, dw, dh);
        glScissor(dx, dy, dw, dh);
        if (cam.ortho())
            glClearColor(0.12f, 0.13f, 0.15f, 1);
        else
            glClearColor(0.17f, 0.20f, 0.26f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float mvp[16];
        cam.mvp(r.w, r.h, mvp);
        set_matrices(mvp, nullptr);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);

        if (vs.grid)
        {
            Batch g;
            grid_lines(cam, r.w, r.h, g);
            draw_batch(g, GL_LINES, 1, 1, 1);
        }

        const bool wire = cam.wire;
        for (size_t i = 0; i < ed.scene().objects.size() && i < objects_.size(); ++i)
        {
            const Object &o = ed.scene().objects[i];
            if (!o.visible)
                continue;
            const bool selected = ed.sel.objects.count(static_cast<int>(i)) > 0;
            set_matrices(mvp, &o.xf);
            const ObjectBuffers &b = objects_[i];
            glBindVertexArray(b.vao);
            if (!wire)
            {
                glEnable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(1, 1);
                glBindBuffer(GL_ARRAY_BUFFER, b.tri);
                attribs();
                if (selected)
                    glUniform4f(u_mul_, 1.25f, 1.1f, 0.85f, 1);
                else
                    glUniform4f(u_mul_, 1, 1, 1, 1);
                glUniform1f(u_point_, 1);
                glDrawArrays(GL_TRIANGLES, 0, b.tri_count);
                glDisable(GL_POLYGON_OFFSET_FILL);
            }
            if (wire || vs.edges)
            {
                glBindBuffer(GL_ARRAY_BUFFER, b.line);
                attribs();
                if (selected)
                    glUniform4f(u_mul_, 1.0f, 0.62f, 0.12f, 1);
                else if (wire)
                    glUniform4f(u_mul_, 0.72f, 0.80f, 0.88f, 1);
                else
                    glUniform4f(u_mul_, 0.10f, 0.11f, 0.13f, 1);
                glDrawArrays(GL_LINES, 0, b.line_count);
            }
        }

        // Equipment markers, then the sub-object overlay, on top of the solids.
        set_matrices(mvp, nullptr);
        Batch ml;
        for (size_t i = 0; i < ed.scene().markers.size(); ++i)
        {
            const Marker &m = ed.scene().markers[i];
            if (!m.placed)
                continue;
            const KindInfo *k = find_kind(m.kind);
            const bool sel = ed.sel.markers.count(static_cast<int>(i)) > 0;
            marker_lines(m, ml, sel ? V3(1.0, 0.62, 0.12) : (k ? k->colour : V3(0.9, 0.9, 0.2)));
        }
        glDisable(GL_DEPTH_TEST);
        draw_batch(ml, GL_LINES, 1, 1, 1);

        if (ed.mode != Mode::Object && ed.active_object())
        {
            const Object &o = *ed.active_object();
            const std::vector<V3> wv = ed.world_verts(-1);
            Batch pts, sel_pts, sel_lines, sel_faces;
            const V3 orange(1.0, 0.62, 0.12), blue(0.35, 0.65, 1.0);
            if (ed.mode == Mode::Vertex)
                for (size_t i = 0; i < wv.size(); ++i)
                    push(ed.sel.verts.count(static_cast<int>(i)) ? sel_pts : pts, wv[i], ed.sel.verts.count(static_cast<int>(i)) ? orange : blue);
            if (ed.mode == Mode::Edge)
                for (const EdgeKey &e : ed.sel.edges)
                    if (e.second < static_cast<int>(wv.size()))
                        line(sel_lines, wv[e.first], wv[e.second], orange);
            if (ed.mode == Mode::Face)
                for (int f : ed.sel.faces)
                    if (f < static_cast<int>(o.mesh.f.size()))
                        for (const Tri &t : triangulate_face(o.mesh, f))
                            for (int k = 0; k < 3; ++k)
                                push(sel_faces, wv[t[k]], orange);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            draw_batch(sel_faces, GL_TRIANGLES, 1, 1, 1, 0.45f);
            glLineWidth(1);
            draw_batch(sel_lines, GL_LINES, 1, 1, 1);
            draw_batch(pts, GL_POINTS, 1, 1, 1, 1, 5);
            draw_batch(sel_pts, GL_POINTS, 1, 1, 1, 1, 8);
            glDisable(GL_BLEND);
        }

        overlay_2d(ed, cam, r, view, vs);
    }

    // Gizmo, sketch, box-select and the active-view frame, drawn in pixel coordinates inside the view.
    void GlView::overlay_2d(const Editor &ed, const Camera &cam, const Rect &r, int view, const ViewState &vs)
    {
        float m[16] = {0};
        m[0] = 2.0f / r.w;
        m[5] = -2.0f / r.h;
        m[10] = 1;
        m[12] = -1;
        m[13] = 1;
        m[15] = 1;
        set_matrices(m, nullptr);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        Batch d;

        V3 p;
        if (ed.tool != Tool::Select && ed.pivot(p))
        {
            double cx, cy, dep;
            if (cam.project(p, r.w, r.h, cx, cy, dep))
            {
                const double len = Editor::kGizmoPixels * cam.metres_per_pixel(p, r.h);
                for (int a = 0; a < 3; ++a)
                {
                    V3 axis;
                    axis[a] = 1;
                    double ex, ey;
                    if (!cam.project(p + axis * len, r.w, r.h, ex, ey, dep) || std::hypot(ex - cx, ey - cy) < 6)
                        continue;
                    const V3 c = (vs.hover_axis == a && view == vs.active) ? V3(1, 0.95, 0.3) : axis_colour(a);
                    thick(d, cx, cy, ex, ey, 3, c);
                    if (ed.tool == Tool::Move)
                        arrow_head(d, cx, cy, ex, ey, 12, c);
                    else if (ed.tool == Tool::Scale)
                        square(d, ex, ey, 5, c);
                    else
                    {
                        const double ang = std::atan2(ey - cy, ex - cx);
                        for (int k = 0; k < 12; ++k)
                        {
                            const double a0 = ang + k * kPi / 6, a1 = ang + (k + 1) * kPi / 6;
                            thick(d, ex + 7 * std::cos(a0), ey + 7 * std::sin(a0), ex + 7 * std::cos(a1), ey + 7 * std::sin(a1), 2, c);
                        }
                    }
                }
                square(d, cx, cy, 5, (vs.hover_axis == 3 && view == vs.active) ? V3(1, 0.95, 0.3) : V3(0.9, 0.9, 0.95));
            }
        }

        if (vs.sketching && view == vs.active && ed.sketch_kind == cam.kind)
        {
            double px = 0, py = 0;
            bool have = false;
            const V3 col(0.4, 1.0, 0.7);
            for (const V2 &s : ed.sketch)
            {
                double sx, sy, dep;
                if (!cam.project(ed.sketch_point(s), r.w, r.h, sx, sy, dep))
                    continue;
                square(d, sx, sy, 4, col);
                if (have)
                    thick(d, px, py, sx, sy, 2, col);
                px = sx;
                py = sy;
                have = true;
            }
            if (have)
                thick(d, px, py, vs.cursor_x, vs.cursor_y, 1.5, col * 0.7);
        }

        if (vs.box && view == vs.active)
        {
            const V3 c(0.9, 0.9, 1.0);
            thick(d, vs.bx0, vs.by0, vs.bx1, vs.by0, 1.5, c);
            thick(d, vs.bx1, vs.by0, vs.bx1, vs.by1, 1.5, c);
            thick(d, vs.bx1, vs.by1, vs.bx0, vs.by1, 1.5, c);
            thick(d, vs.bx0, vs.by1, vs.bx0, vs.by0, 1.5, c);
        }

        if (vs.maximised < 0)
        {
            const V3 c = view == vs.active ? V3(0.95, 0.62, 0.15) : V3(0.3, 0.32, 0.36);
            const double t = view == vs.active ? 2 : 1;
            thick(d, 0, t / 2, r.w, t / 2, t, c);
            thick(d, r.w - t / 2, 0, r.w - t / 2, r.h, t, c);
            thick(d, r.w, r.h - t / 2, 0, r.h - t / 2, t, c);
            thick(d, t / 2, r.h, t / 2, 0, t, c);
        }
        draw_batch(d, GL_TRIANGLES, 1, 1, 1);
        glDisable(GL_BLEND);
    }

    void GlView::render(const Editor &ed, const Camera cams[4], const ViewState &vs, int W, int H, int scale)
    {
        if (!program_)
            return;
        H_ = H;
        if (built_gen_ != ed.mesh_gen || built_count_ != ed.scene().objects.size())
            rebuild(ed);
        glUseProgram(program_);
        glEnable(GL_SCISSOR_TEST);
        glViewport(0, 0, W * scale, H * scale);
        glScissor(0, 0, W * scale, H * scale);
        glClearColor(0.05f, 0.05f, 0.06f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        for (int i = 0; i < 4; ++i)
        {
            const Rect r = view_rect(i, W, H, vs.maximised);
            if (r.w > 0 && r.h > 0)
                draw_view(ed, cams[i], r, i, vs, scale);
        }
        glDisable(GL_SCISSOR_TEST);
    }

    std::vector<unsigned char> GlView::read_pixels(int w, int h) const
    {
        std::vector<unsigned char> raw(static_cast<size_t>(w) * h * 4), out(raw.size());
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, raw.data());
        for (int y = 0; y < h; ++y)
            std::copy_n(raw.begin() + static_cast<size_t>(h - 1 - y) * w * 4, static_cast<size_t>(w) * 4, out.begin() + static_cast<size_t>(y) * w * 4);
        return out;
    }
}
