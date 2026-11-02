#include "editor.h"

#include <algorithm>
#include <cmath>

namespace cad
{
    namespace
    {
        bool ray_triangle(const V3 &o, const V3 &d, const V3 &a, const V3 &b, const V3 &c, double &t)
        {
            const V3 e1 = b - a, e2 = c - a, p = cross(d, e2);
            const double det = dot(e1, p);
            if (std::fabs(det) < 1e-12)
                return false;
            const double inv = 1 / det;
            const V3 s = o - a;
            const double u = dot(s, p) * inv;
            if (u < 0 || u > 1)
                return false;
            const V3 q = cross(s, e1);
            const double v = dot(d, q) * inv;
            if (v < 0 || u + v > 1)
                return false;
            t = dot(e2, q) * inv;
            return t > 0;
        }

        double dist_to_segment(double px, double py, double ax, double ay, double bx, double by)
        {
            const double dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
            double t = l2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0;
            t = std::clamp(t, 0.0, 1.0);
            return std::hypot(px - (ax + t * dx), py - (ay + t * dy));
        }

        // Parameter along the line (p, a) of the point nearest to the ray (o, d).
        double line_ray_param(const V3 &p, const V3 &a, const V3 &o, const V3 &d)
        {
            const V3 w = p - o;
            const double aa = dot(a, a), ad = dot(a, d), dd = dot(d, d), aw = dot(a, w), dw = dot(d, w);
            const double den = aa * dd - ad * ad;
            if (std::fabs(den) < 1e-12)
                return 0;
            return (ad * dw - dd * aw) / den;
        }

        bool ray_plane(const V3 &o, const V3 &d, const V3 &p, const V3 &n, V3 &hit)
        {
            const double den = dot(d, n);
            if (std::fabs(den) < 1e-9)
                return false;
            hit = o + d * (dot(p - o, n) / den);
            return true;
        }

        V3 unit_axis(int i) { return i == 0 ? V3(1, 0, 0) : (i == 1 ? V3(0, 1, 0) : V3(0, 0, 1)); }
    } // namespace

    Object *Editor::active_object()
    {
        auto &o = doc.scene.objects;
        return sel.active >= 0 && sel.active < static_cast<int>(o.size()) ? &o[sel.active] : nullptr;
    }
    const Object *Editor::active_object() const { return const_cast<Editor *>(this)->active_object(); }

    std::vector<V3> Editor::world_verts(int object) const
    {
        const int i = object < 0 ? sel.active : object;
        std::vector<V3> out;
        if (i >= 0 && i < static_cast<int>(scene().objects.size()))
        {
            const Object &o = scene().objects[i];
            for (const V3 &v : o.mesh.v)
                out.push_back(o.xf.apply(v));
        }
        return out;
    }

    void Editor::changed()
    {
        ++mesh_gen;
        doc.dirty = true;
    }

    void Editor::set_mode(Mode m)
    {
        mode = m;
        sel.clear_elements();
    }

    void Editor::select_all()
    {
        sel.objects.clear();
        sel.markers.clear();
        for (size_t i = 0; i < scene().objects.size(); ++i)
            sel.objects.insert(static_cast<int>(i));
        for (size_t i = 0; i < scene().markers.size(); ++i)
            sel.markers.insert(static_cast<int>(i));
        if (sel.active < 0 && !scene().objects.empty())
            sel.active = 0;
    }

    std::set<int> Editor::element_verts() const
    {
        std::set<int> out;
        const Object *o = active_object();
        if (!o)
            return out;
        if (mode == Mode::Vertex)
            out = sel.verts;
        else if (mode == Mode::Edge)
            for (const EdgeKey &e : sel.edges)
            {
                out.insert(e.first);
                out.insert(e.second);
            }
        else if (mode == Mode::Face)
            for (int f : sel.faces)
                if (f >= 0 && f < static_cast<int>(o->mesh.f.size()))
                    for (int v : o->mesh.f[f].v)
                        out.insert(v);
        return out;
    }

    // ---- picking -----------------------------------------------------------------------------------------------

    bool Editor::click(const Camera &cam, int w, int h, double x, double y, bool add)
    {
        V3 ro, rd;
        cam.ray(x, y, w, h, ro, rd);

        // Nearest object under the ray, and the nearest marker under the cursor.
        int hit_obj = -1;
        double best = 1e30;
        for (size_t i = 0; i < scene().objects.size(); ++i)
        {
            const Object &o = scene().objects[i];
            if (!o.visible)
                continue;
            const std::vector<V3> wv = world_verts(static_cast<int>(i));
            for (size_t f = 0; f < o.mesh.f.size(); ++f)
                for (const Tri &t : triangulate_face(o.mesh, static_cast<int>(f)))
                {
                    double d;
                    if (ray_triangle(ro, rd, wv[t[0]], wv[t[1]], wv[t[2]], d) && d < best)
                    {
                        best = d;
                        hit_obj = static_cast<int>(i);
                    }
                }
        }
        int hit_marker = -1;
        double marker_depth = 1e30;
        for (size_t i = 0; i < scene().markers.size(); ++i)
        {
            const Marker &m = scene().markers[i];
            if (!m.placed)
                continue;
            double sx, sy, depth;
            if (!cam.project(m.pos, w, h, sx, sy, depth))
                continue;
            const double reach = std::max(12.0, std::max({m.size.x, m.size.y, m.size.z}) * 0.5 / cam.metres_per_pixel(m.pos, h));
            if (std::hypot(sx - x, sy - y) < reach && depth < marker_depth)
            {
                marker_depth = depth;
                hit_marker = static_cast<int>(i);
            }
        }

        if (mode != Mode::Object && active_object())
        {
            const Object &o = *active_object();
            const std::vector<V3> wv = world_verts(-1);
            if (mode == Mode::Vertex)
            {
                int bi = -1;
                double bd = 10;
                for (size_t i = 0; i < wv.size(); ++i)
                {
                    double sx, sy, dp;
                    if (cam.project(wv[i], w, h, sx, sy, dp) && std::hypot(sx - x, sy - y) < bd)
                    {
                        bd = std::hypot(sx - x, sy - y);
                        bi = static_cast<int>(i);
                    }
                }
                if (bi >= 0)
                {
                    if (!add)
                        sel.verts.clear();
                    if (add && sel.verts.count(bi))
                        sel.verts.erase(bi);
                    else
                        sel.verts.insert(bi);
                    return true;
                }
            }
            else if (mode == Mode::Edge)
            {
                EdgeKey be{-1, -1};
                double bd = 8;
                for (const auto &ef : edge_faces(o.mesh))
                {
                    double ax, ay, bx, by, dp;
                    if (cam.project(wv[ef.first.first], w, h, ax, ay, dp) && cam.project(wv[ef.first.second], w, h, bx, by, dp))
                    {
                        const double d = dist_to_segment(x, y, ax, ay, bx, by);
                        if (d < bd)
                        {
                            bd = d;
                            be = ef.first;
                        }
                    }
                }
                if (be.first >= 0)
                {
                    if (!add)
                        sel.edges.clear();
                    if (add && sel.edges.count(be))
                        sel.edges.erase(be);
                    else
                        sel.edges.insert(be);
                    return true;
                }
            }
            else
            {
                int bf = -1;
                double bt = 1e30;
                for (size_t f = 0; f < o.mesh.f.size(); ++f)
                    for (const Tri &t : triangulate_face(o.mesh, static_cast<int>(f)))
                    {
                        double d;
                        if (ray_triangle(ro, rd, wv[t[0]], wv[t[1]], wv[t[2]], d) && d < bt)
                        {
                            bt = d;
                            bf = static_cast<int>(f);
                        }
                    }
                if (bf >= 0)
                {
                    if (!add)
                        sel.faces.clear();
                    if (add && sel.faces.count(bf))
                        sel.faces.erase(bf);
                    else
                        sel.faces.insert(bf);
                    return true;
                }
            }
            // Missed the active object: fall through so another object can be picked.
        }

        if (hit_marker >= 0)
        {
            if (!add)
            {
                sel.objects.clear();
                sel.markers.clear();
            }
            if (add && sel.markers.count(hit_marker))
                sel.markers.erase(hit_marker);
            else
                sel.markers.insert(hit_marker);
            sel.clear_elements();
            return true;
        }
        if (hit_obj >= 0)
        {
            if (!add)
            {
                sel.objects.clear();
                sel.markers.clear();
            }
            if (add && sel.objects.count(hit_obj))
            {
                sel.objects.erase(hit_obj);
                if (sel.active == hit_obj)
                    sel.active = sel.objects.empty() ? -1 : *sel.objects.begin();
            }
            else
            {
                sel.objects.insert(hit_obj);
                sel.active = hit_obj;
            }
            sel.clear_elements();
            return true;
        }
        if (!add)
            sel.clear();
        return false;
    }

    void Editor::box_select(const Camera &cam, int w, int h, double x0, double y0, double x1, double y1, bool add)
    {
        const double lx = std::min(x0, x1), hx = std::max(x0, x1), ly = std::min(y0, y1), hy = std::max(y0, y1);
        auto inside = [&](const V3 &p) {
            double sx, sy, d;
            return cam.project(p, w, h, sx, sy, d) && sx >= lx && sx <= hx && sy >= ly && sy <= hy;
        };
        if (mode != Mode::Object && active_object())
        {
            const Object &o = *active_object();
            const std::vector<V3> wv = world_verts(-1);
            if (mode == Mode::Vertex)
            {
                if (!add)
                    sel.verts.clear();
                for (size_t i = 0; i < wv.size(); ++i)
                    if (inside(wv[i]))
                        sel.verts.insert(static_cast<int>(i));
            }
            else if (mode == Mode::Edge)
            {
                if (!add)
                    sel.edges.clear();
                for (const auto &ef : edge_faces(o.mesh))
                    if (inside(wv[ef.first.first]) && inside(wv[ef.first.second]))
                        sel.edges.insert(ef.first);
            }
            else
            {
                if (!add)
                    sel.faces.clear();
                for (size_t f = 0; f < o.mesh.f.size(); ++f)
                    if (inside(o.xf.apply(face_centre(o.mesh, static_cast<int>(f)))))
                        sel.faces.insert(static_cast<int>(f));
            }
            return;
        }
        if (!add)
            sel.clear();
        for (size_t i = 0; i < scene().objects.size(); ++i)
            if (scene().objects[i].visible && inside(scene().objects[i].xf.pos))
            {
                sel.objects.insert(static_cast<int>(i));
                sel.active = static_cast<int>(i);
            }
        for (size_t i = 0; i < scene().markers.size(); ++i)
            if (scene().markers[i].placed && inside(scene().markers[i].pos))
                sel.markers.insert(static_cast<int>(i));
    }

    // ---- gizmo -------------------------------------------------------------------------------------------------

    bool Editor::pivot(V3 &out) const
    {
        const std::set<int> ev = element_verts();
        if (mode != Mode::Object && !ev.empty())
        {
            const Object &o = *active_object();
            V3 sum;
            for (int v : ev)
                sum += o.xf.apply(o.mesh.v[v]);
            out = sum / static_cast<double>(ev.size());
            return true;
        }
        V3 sum;
        int n = 0;
        for (int i : sel.objects)
            if (i < static_cast<int>(scene().objects.size()))
            {
                sum += scene().objects[i].xf.pos;
                ++n;
            }
        for (int i : sel.markers)
            if (i < static_cast<int>(scene().markers.size()))
            {
                sum += scene().markers[i].pos;
                ++n;
            }
        if (!n)
            return false;
        out = sum / static_cast<double>(n);
        return true;
    }

    int Editor::pick_gizmo(const Camera &cam, int w, int h, double x, double y) const
    {
        V3 p;
        if (tool == Tool::Select || !pivot(p))
            return -1;
        double cx, cy, d;
        if (!cam.project(p, w, h, cx, cy, d))
            return -1;
        if (std::hypot(cx - x, cy - y) < 10)
            return 3;
        const double len = kGizmoPixels * cam.metres_per_pixel(p, h);
        int best = -1;
        double bd = 8;
        for (int a = 0; a < 3; ++a)
        {
            double ex, ey;
            if (!cam.project(p + unit_axis(a) * len, w, h, ex, ey, d) || std::hypot(ex - cx, ey - cy) < 6)
                continue;
            const double dd = dist_to_segment(x, y, cx, cy, ex, ey);
            if (dd < bd)
            {
                bd = dd;
                best = a;
            }
        }
        return best;
    }

    void Editor::begin_drag(const Camera &cam, int w, int h, double x, double y, int axis)
    {
        V3 p;
        if (axis < 0 || !pivot(p))
            return;
        doc.checkpoint();
        dragging_ = true;
        drag_axis_ = axis;
        drag_x0_ = x;
        drag_y0_ = y;
        snap_ = Snapshot();
        snap_.pivot = p;
        const bool elements = mode != Mode::Object && !element_verts().empty();
        if (elements)
            snap_.world_verts = world_verts(-1);
        else
        {
            for (int i : sel.objects)
                if (i < static_cast<int>(scene().objects.size()))
                    snap_.objects.push_back({i, scene().objects[i].xf});
            for (int i : sel.markers)
                if (i < static_cast<int>(scene().markers.size()))
                    snap_.markers.push_back({i, scene().markers[i]});
        }
        V3 ro, rd;
        cam.ray(x, y, w, h, ro, rd);
        V3 r, u, f;
        cam.basis(r, u, f);
        if (tool == Tool::Move)
        {
            if (axis < 3)
                drag_start_ = unit_axis(axis) * line_ray_param(p, unit_axis(axis), ro, rd);
            else if (!ray_plane(ro, rd, p, f, drag_start_))
                drag_start_ = p;
        }
    }

    void Editor::drag(const Camera &cam, int w, int h, double x, double y)
    {
        if (!dragging_)
            return;
        const V3 p = snap_.pivot;
        V3 ro, rd, r, u, f;
        cam.ray(x, y, w, h, ro, rd);
        cam.basis(r, u, f);
        const double screen = (x - drag_x0_) - (y - drag_y0_);

        M3 R;
        bool rotate = false;
        V3 delta, scale_axis;
        double s = 1;
        if (tool == Tool::Move)
        {
            if (drag_axis_ < 3)
                delta = unit_axis(drag_axis_) * (line_ray_param(p, unit_axis(drag_axis_), ro, rd) - dot(drag_start_, unit_axis(drag_axis_)));
            else
            {
                V3 hit;
                if (ray_plane(ro, rd, p, f, hit))
                    delta = hit - drag_start_;
            }
        }
        else if (tool == Tool::Rotate)
        {
            rotate = true;
            const V3 axis = drag_axis_ < 3 ? unit_axis(drag_axis_) : -f;
            R = axis_angle(axis, screen * 0.5 * kPi / 180);
        }
        else if (tool == Tool::Scale)
        {
            s = std::exp(screen * 0.01);
            if (drag_axis_ < 3)
                scale_axis = unit_axis(drag_axis_);
        }

        auto point = [&](const V3 &q) -> V3 {
            if (tool == Tool::Move)
                return q + delta;
            if (rotate)
                return p + R * (q - p);
            if (tool == Tool::Scale)
            {
                const V3 d = q - p;
                return drag_axis_ < 3 ? p + d + scale_axis * ((s - 1) * dot(d, scale_axis)) : p + d * s;
            }
            return q;
        };

        if (!snap_.world_verts.empty())
        {
            Object *o = active_object();
            if (!o)
                return;
            for (int v : element_verts())
                if (v < static_cast<int>(o->mesh.v.size()))
                    o->mesh.v[v] = o->xf.inverse(point(snap_.world_verts[v]));
            touch_mesh();
            return;
        }
        for (const auto &[i, xf] : snap_.objects)
        {
            Xform n = xf;
            n.pos = point(xf.pos);
            if (rotate)
                n.rot = matrix_euler(R * euler_matrix(xf.rot));
            if (tool == Tool::Scale)
            {
                if (drag_axis_ < 3)
                    n.scale[drag_axis_] *= s;
                else
                    n.scale = xf.scale * s;
            }
            scene().objects[i].xf = n;
        }
        for (const auto &[i, m] : snap_.markers)
        {
            Marker n = m;
            n.pos = point(m.pos);
            if (rotate)
                n.rot = matrix_euler(R * euler_matrix(m.rot));
            if (tool == Tool::Scale)
            {
                if (drag_axis_ < 3)
                    n.size[drag_axis_] *= s;
                else
                    n.size = m.size * s;
            }
            scene().markers[i] = n;
        }
        doc.dirty = true;
    }

    void Editor::end_drag()
    {
        dragging_ = false;
        snap_ = Snapshot();
    }

    // ---- commands ----------------------------------------------------------------------------------------------

    int Editor::add_mesh(const std::string &name, Mesh mesh, const V3 &at)
    {
        doc.checkpoint();
        Object o;
        o.name = unique_object_name(scene(), name);
        o.mesh = std::move(mesh);
        o.xf.pos = at;
        scene().objects.push_back(std::move(o));
        sel.clear();
        sel.active = static_cast<int>(scene().objects.size()) - 1;
        sel.objects.insert(sel.active);
        touch_mesh();
        return sel.active;
    }

    int Editor::add_shape(ShapeKind kind, double a, double b, double c, int segments, const V3 &at)
    {
        segments = std::clamp(segments, 3, 128);
        switch (kind)
        {
        case ShapeKind::Box: return add_mesh("box", make_box(a, b, c), at);
        case ShapeKind::Cylinder: return add_mesh("cylinder", make_cylinder(a, b, segments), at);
        case ShapeKind::Cone: return add_mesh("cone", make_cone(a, b, c, segments), at);
        case ShapeKind::Sphere: return add_mesh("sphere", make_sphere(a, segments, std::max(segments / 2, 2)), at);
        case ShapeKind::Torus: return add_mesh("torus", make_torus(a, b, segments, std::max(segments / 2, 3)), at);
        case ShapeKind::Plane: return add_mesh("plane", make_plane(a, b), at);
        }
        return -1;
    }

    int Editor::add_marker(const std::string &kind, const V3 &at)
    {
        doc.checkpoint();
        scene().markers.push_back(new_marker(kind, at));
        sel.objects.clear();
        sel.markers = {static_cast<int>(scene().markers.size()) - 1};
        doc.dirty = true;
        return *sel.markers.begin();
    }

    void Editor::delete_selection()
    {
        if (mode == Mode::Face && !sel.faces.empty())
        {
            delete_selected_faces();
            return;
        }
        if (sel.objects.empty() && sel.markers.empty())
            return;
        doc.checkpoint();
        for (auto it = sel.markers.rbegin(); it != sel.markers.rend(); ++it)
            scene().markers.erase(scene().markers.begin() + *it);
        for (auto it = sel.objects.rbegin(); it != sel.objects.rend(); ++it)
            scene().objects.erase(scene().objects.begin() + *it);
        sel.clear();
        touch_mesh();
    }

    void Editor::duplicate_selection()
    {
        if (sel.objects.empty() && sel.markers.empty())
            return;
        doc.checkpoint();
        std::set<int> objs, marks;
        for (int i : std::set<int>(sel.objects))
        {
            Object o = scene().objects[i];
            o.name = unique_object_name(scene(), o.name);
            o.source.clear();
            o.part = nullptr;
            scene().objects.push_back(o);
            objs.insert(static_cast<int>(scene().objects.size()) - 1);
        }
        for (int i : std::set<int>(sel.markers))
        {
            Marker m = scene().markers[i];
            m.source.clear();
            scene().markers.push_back(m);
            marks.insert(static_cast<int>(scene().markers.size()) - 1);
        }
        sel.clear();
        sel.objects = objs;
        sel.markers = marks;
        sel.active = objs.empty() ? -1 : *objs.begin();
        touch_mesh();
    }

    void Editor::apply_transform()
    {
        if (sel.objects.empty())
            return;
        doc.checkpoint();
        for (int i : sel.objects)
        {
            Object &o = scene().objects[i];
            apply_xform(o.mesh, Xform{{}, o.xf.rot, o.xf.scale});
            o.xf.rot = {};
            o.xf.scale = {1, 1, 1};
        }
        touch_mesh();
    }

    void Editor::set_origin_to_centre()
    {
        if (sel.objects.empty())
            return;
        doc.checkpoint();
        for (int i : sel.objects)
        {
            Object &o = scene().objects[i];
            V3 lo, hi;
            cad::bounds(o.mesh, lo, hi);
            const V3 c = (lo + hi) * 0.5;
            for (V3 &v : o.mesh.v)
                v -= c;
            o.xf.pos = o.xf.apply(c);
        }
        touch_mesh();
    }

    void Editor::mirror_object(int axis)
    {
        Object *o = active_object();
        if (!o)
            return;
        doc.checkpoint();
        mirror(o->mesh, axis);
        sel.clear_elements();
        touch_mesh();
    }

    void Editor::boolean(BoolOp op)
    {
        if (sel.objects.size() < 2 || sel.active < 0 || !sel.objects.count(sel.active))
            return;
        doc.checkpoint();
        Object &a = scene().objects[sel.active];
        Mesh result = world_mesh(a);
        std::vector<int> others;
        for (int i : sel.objects)
            if (i != sel.active)
                others.push_back(i);
        for (int i : others)
        {
            const Mesh b = world_mesh(scene().objects[i]);
            result = op == BoolOp::Union ? csg_union(result, b) : (op == BoolOp::Subtract ? csg_subtract(result, b) : csg_intersect(result, b));
        }
        a.mesh = std::move(result);
        a.xf = Xform();
        int active = sel.active;
        for (auto it = others.rbegin(); it != others.rend(); ++it)
        {
            scene().objects.erase(scene().objects.begin() + *it);
            if (*it < active)
                --active;
        }
        sel.clear();
        sel.active = active;
        sel.objects.insert(active);
        touch_mesh();
    }

    void Editor::extrude(double distance)
    {
        Object *o = active_object();
        if (!o || sel.faces.empty())
            return;
        doc.checkpoint();
        const std::vector<int> made = extrude_faces(o->mesh, sel.faces, distance);
        sel.faces = std::set<int>(made.begin(), made.end());
        touch_mesh();
    }

    void Editor::inset(double amount)
    {
        Object *o = active_object();
        if (!o || sel.faces.empty())
            return;
        doc.checkpoint();
        const std::vector<int> made = inset_faces(o->mesh, sel.faces, amount);
        sel.faces = std::set<int>(made.begin(), made.end());
        touch_mesh();
    }

    void Editor::delete_selected_faces()
    {
        Object *o = active_object();
        if (!o || sel.faces.empty())
            return;
        doc.checkpoint();
        delete_faces(o->mesh, sel.faces);
        remove_unused_vertices(o->mesh);
        sel.clear_elements();
        touch_mesh();
    }

    void Editor::flip_selected_faces()
    {
        Object *o = active_object();
        if (!o)
            return;
        doc.checkpoint();
        std::set<int> faces = sel.faces;
        if (faces.empty())
            for (size_t i = 0; i < o->mesh.f.size(); ++i)
                faces.insert(static_cast<int>(i));
        flip_faces(o->mesh, faces);
        touch_mesh();
    }

    void Editor::split_selected_edges()
    {
        Object *o = active_object();
        if (!o || sel.edges.empty())
            return;
        doc.checkpoint();
        std::set<int> made;
        for (const EdgeKey &e : std::set<EdgeKey>(sel.edges))
        {
            const int v = split_edge(o->mesh, e.first, e.second);
            if (v >= 0)
                made.insert(v);
        }
        sel.clear_elements();
        sel.verts = made;
        mode = Mode::Vertex;
        touch_mesh();
    }

    void Editor::weld_selected(double eps)
    {
        Object *o = active_object();
        if (!o)
            return;
        doc.checkpoint();
        weld(o->mesh, eps);
        sel.clear_elements();
        touch_mesh();
    }

    void Editor::assign_material(int slot)
    {
        if (slot < 0 || slot >= static_cast<int>(scene().materials.size()))
            return;
        doc.checkpoint();
        Object *o = active_object();
        if (mode == Mode::Face && o && !sel.faces.empty())
        {
            for (int f : sel.faces)
                if (f < static_cast<int>(o->mesh.f.size()))
                    o->mesh.f[f].mat = slot;
        }
        else
            for (int i : sel.objects)
                for (Face &f : scene().objects[i].mesh.f)
                    f.mat = slot;
        touch_mesh();
    }

    void Editor::project_uvs(UvMode uv_mode, int axis)
    {
        doc.checkpoint();
        if (mode == Mode::Face && active_object() && !sel.faces.empty())
            project_uv(active_object()->mesh, sel.faces, uv_mode, axis);
        else
            for (int i : sel.objects)
                project_uv(scene().objects[i].mesh, {}, uv_mode, axis);
        touch_mesh();
    }

    int Editor::new_material(const std::string &name)
    {
        doc.checkpoint();
        Material m;
        m.name = unique_material_name(scene(), name);
        scene().materials.push_back(m);
        doc.dirty = true;
        return static_cast<int>(scene().materials.size()) - 1;
    }

    V3 Editor::sketch_point(const V2 &p, double n) const
    {
        switch (sketch_kind)
        {
        case Camera::Front: return {-p.u, p.v, n};
        case Camera::Side: return {n, p.v, -p.u};
        default: return {p.u, n, -p.v}; // Top
        }
    }

    void Editor::sketch_extrude(double depth, double far_scale)
    {
        if (sketch.size() < 3)
            return;
        doc.checkpoint();
        V2 centre{0, 0};
        for (const V2 &p : sketch)
        {
            centre.u += p.u / sketch.size();
            centre.v += p.v / sketch.size();
        }
        Mesh m;
        if (std::fabs(far_scale - 1) < 1e-9)
            m = make_extrusion(sketch, depth);
        else
        {
            std::vector<V2> far;
            for (const V2 &p : sketch)
                far.push_back({centre.u + (p.u - centre.u) * far_scale, centre.v + (p.v - centre.v) * far_scale});
            m = make_loft({sketch, far}, {-depth / 2, depth / 2});
        }
        // The builders work in x, y with z as the depth; carry that into the sketch's plane and keep the faces outward.
        for (V3 &v : m.v)
            v = sketch_point({v.x, v.y}, v.z);
        if (signed_volume(m) < 0)
            flip_faces(m, [&] {
                std::set<int> all;
                for (size_t i = 0; i < m.f.size(); ++i)
                    all.insert(static_cast<int>(i));
                return all;
            }());
        sketch.clear();
        Object o;
        o.name = unique_object_name(scene(), "sketch");
        o.mesh = std::move(m);
        scene().objects.push_back(std::move(o));
        sel.clear();
        sel.active = static_cast<int>(scene().objects.size()) - 1;
        sel.objects.insert(sel.active);
        touch_mesh();
    }

    void Editor::sketch_lathe(int segments)
    {
        if (sketch.size() < 2)
            return;
        doc.checkpoint();
        std::vector<V2> profile;
        for (const V2 &p : sketch)
            profile.push_back({std::fabs(p.u), p.v});
        Mesh m = make_lathe(profile, std::clamp(segments, 3, 128));
        if (signed_volume(m) < 0)
        {
            std::set<int> all;
            for (size_t i = 0; i < m.f.size(); ++i)
                all.insert(static_cast<int>(i));
            flip_faces(m, all);
        }
        sketch.clear();
        Object o;
        o.name = unique_object_name(scene(), "lathe");
        o.mesh = std::move(m);
        scene().objects.push_back(std::move(o));
        sel.clear();
        sel.active = static_cast<int>(scene().objects.size()) - 1;
        sel.objects.insert(sel.active);
        touch_mesh();
    }

    bool Editor::bounds(V3 &lo, V3 &hi) const
    {
        bool any = false;
        auto grow = [&](const V3 &p) {
            if (!any)
                lo = hi = p;
            any = true;
            for (int i = 0; i < 3; ++i)
            {
                lo[i] = std::min(lo[i], p[i]);
                hi[i] = std::max(hi[i], p[i]);
            }
        };
        for (size_t i = 0; i < scene().objects.size(); ++i)
            if (scene().objects[i].visible)
                for (const V3 &v : world_verts(static_cast<int>(i)))
                    grow(v);
        for (const Marker &m : scene().markers)
            if (m.placed)
            {
                grow(m.pos - m.size * 0.5);
                grow(m.pos + m.size * 0.5);
            }
        return any;
    }
}
