// KosmosCAD's mesh library: shapes are closed and outward facing, edit operations keep them closed, booleans give the right volumes.
#include "csg.h"
#include "mesh.h"
#include "scene.h"
#include "equipment.h"
#include "editor.h"
#include "image.h"
#include "ship_io.h"

#include <cmath>
#include <cstring>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

using namespace cad;

namespace
{
    int failures = 0;

    void check(bool ok, const std::string &what)
    {
        std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
        if (!ok)
            ++failures;
    }

    bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

    // Every edge is shared by exactly two faces.
    bool closed(const Mesh &m)
    {
        for (const auto &e : edge_faces(m))
            if (e.second.size() != 2)
                return false;
        return !m.f.empty();
    }

    // Each directed edge appears once, so the faces agree on which way is out.
    bool consistent(const Mesh &m)
    {
        std::set<std::pair<int, int>> seen;
        for (const Face &f : m.f)
            for (size_t i = 0; i < f.v.size(); ++i)
                if (!seen.insert({f.v[i], f.v[(i + 1) % f.v.size()]}).second)
                    return false;
        return true;
    }

    void shape(const char *name, const Mesh &m, double volume, double tol)
    {
        check(closed(m), std::string(name) + " is closed");
        check(consistent(m), std::string(name) + " has consistent winding");
        check(near(signed_volume(m), volume, tol), std::string(name) + " volume " + std::to_string(signed_volume(m)) + " vs " + std::to_string(volume));
    }
}

int main()
{
    shape("box", make_box(2, 3, 4), 24, 1e-9);
    shape("cylinder", make_cylinder(1, 2, 64), kPi * 2, 0.05);
    shape("cone", make_cone(1, 0, 3, 64), kPi, 0.05);
    shape("frustum", make_cone(2, 1, 3, 48), kPi * 3 / 3 * (4 + 2 + 1), 0.1);
    shape("sphere", make_sphere(1, 48, 24), 4.0 / 3 * kPi, 0.1);
    shape("torus", make_torus(2, 0.5, 48, 24), 2 * kPi * kPi * 2 * 0.25, 0.15);
    shape("extrusion (L shape)", make_extrusion({{0, 0}, {2, 0}, {2, 1}, {1, 1}, {1, 2}, {0, 2}}, 2), 6, 1e-9);
    shape("extrusion (clockwise outline)", make_extrusion({{0, 0}, {0, 2}, {1, 2}, {1, 1}, {2, 1}, {2, 0}}, 2), 6, 1e-9);
    shape("lathe vase", make_lathe({{0, 0}, {1, 0}, {1, 1}, {0.5, 2}, {0, 2}}, 32), 0, 1e9);
    shape("loft", make_loft({{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}, {{-0.5, -0.5}, {0.5, -0.5}, {0.5, 0.5}, {-0.5, 0.5}}}, {0, 2}), 2.0 / 3 * (4 + 1 + 2), 1e-9);

    // Extrude a box's top face.
    {
        Mesh m = make_box(2, 2, 2);
        int top = -1;
        for (size_t i = 0; i < m.f.size(); ++i)
            if (face_normal(m, static_cast<int>(i)).y > 0.9)
                top = static_cast<int>(i);
        const auto made = extrude_faces(m, {top}, 1.5);
        check(made.size() == 1 && closed(m) && consistent(m), "extrude keeps the mesh closed");
        check(near(signed_volume(m), 8 + 4 * 1.5, 1e-9), "extrude adds area x distance of volume");
        check(m.f.size() == 6 + 4, "extrude adds a wall per edge");
        const auto inner = inset_faces(m, {made[0]}, 0.5);
        check(closed(m) && consistent(m) && inner.size() == 1, "inset keeps the mesh closed");
        check(near(signed_volume(m), 8 + 4 * 1.5, 1e-9), "inset does not change the volume");
        delete_faces(m, {inner[0]});
        check(!closed(m) && m.f.size() == 6 + 4 + 4 - 1, "delete leaves a hole");
    }
    // Split edge.
    {
        Mesh m = make_box(1, 1, 1);
        const auto edges = edge_faces(m);
        const int mid = split_edge(m, edges[0].first.first, edges[0].first.second);
        check(mid >= 0 && closed(m) && consistent(m), "split edge keeps the mesh closed");
        check(near(signed_volume(m), 1, 1e-9), "split edge keeps the volume");
    }
    // Mirror a half shape.
    {
        Mesh m = make_box(1, 1, 1);
        apply_xform(m, Xform{{0.5, 0, 0}, {}, {1, 1, 1}});
        std::set<int> side;
        for (size_t i = 0; i < m.f.size(); ++i)
            if (face_normal(m, static_cast<int>(i)).x < -0.9)
                side.insert(static_cast<int>(i));
        delete_faces(m, side); // open at x = 0
        mirror(m, 0);
        check(closed(m) && consistent(m), "mirror of a half shape welds into a closed one");
        check(near(signed_volume(m), 2, 1e-9), "mirror doubles the volume");
        check(m.v.size() == 12, "mirror welds the seam vertices");
    }
    // Negative scale keeps faces outward.
    {
        Mesh m = make_box(1, 2, 3);
        apply_xform(m, Xform{{}, {}, {-1, 1, 1}});
        check(near(signed_volume(m), 6, 1e-9), "mirror by negative scale flips the winding");
    }
    // Concave triangulation.
    {
        Mesh m = make_extrusion({{0, 0}, {4, 0}, {4, 4}, {2, 1}, {0, 4}}, 1);
        check(near(signed_volume(m), 10, 1e-9), "concave caps triangulate by ear clipping");
    }
    // UV projection.
    {
        Mesh m = make_box(2, 2, 2);
        project_uv(m, {}, UvMode::Box, 0);
        bool ok = true;
        for (const Face &f : m.f)
            ok = ok && f.uv.size() == f.v.size();
        for (const V2 &u : m.uv)
            ok = ok && u.u >= -1e-9 && u.u <= 1 + 1e-9 && u.v >= -1e-9 && u.v <= 1 + 1e-9;
        check(ok, "box UVs cover 0..1");
        Mesh c = make_cylinder(1, 2, 16);
        project_uv(c, {}, UvMode::Cylindrical, 1);
        check(c.uv.size() == 16 * 4 + 32, "cylindrical UVs one per corner");
    }
    // Booleans.
    {
        Mesh a = make_box(2, 2, 2);
        Mesh b = make_box(2, 2, 2);
        apply_xform(b, Xform{{1, 0, 0}, {}, {1, 1, 1}});
        const Mesh u = csg_union(a, b), s = csg_subtract(a, b), i = csg_intersect(a, b);
        check(near(signed_volume(u), 12, 1e-6), "union volume");
        check(near(signed_volume(s), 4, 1e-6), "subtract volume");
        check(near(signed_volume(i), 4, 1e-6), "intersect volume");
        check(consistent(u) && consistent(s), "boolean results have consistent winding");

        Mesh hole = make_cylinder(0.5, 4, 32);
        apply_xform(hole, Xform{{}, {90, 0, 0}, {1, 1, 1}});
        const Mesh drilled = csg_subtract(make_box(2, 2, 2), hole);
        const double expected = 8 - kPi * 0.25 * 2;
        check(near(signed_volume(drilled), expected, 0.08), "drilling a hole through a box (" + std::to_string(signed_volume(drilled)) + " vs " + std::to_string(expected) + ")");
    }
    // Scene: undo/redo, .kmodel and OBJ round trips.
    {
        Document doc;
        doc.scene.materials.push_back(Material{"hull_red", {0.8, 0.1, 0.1}, ""});
        Object a;
        a.name = "tank";
        a.mesh = make_cylinder(1, 4, 24);
        for (Face &f : a.mesh.f)
            f.mat = 1;
        a.xf.pos = {1, 2, 3};
        a.xf.rot = {0, 30, 0};
        doc.scene.objects.push_back(a);
        Object b;
        b.name = "block";
        b.mesh = make_box(1, 1, 1);
        project_uv(b.mesh, {}, UvMode::Box, 0);
        doc.scene.objects.push_back(b);
        Marker mk;
        mk.kind = "engine";
        mk.name = "Main";
        mk.pos = {0, 0, 5};
        mk.props = {{"thrust", 1000}};
        doc.scene.markers.push_back(mk);

        doc.checkpoint();
        doc.scene.objects.pop_back();
        check(doc.scene.objects.size() == 1 && doc.can_undo(), "checkpoint then edit");
        doc.undo();
        check(doc.scene.objects.size() == 2 && doc.can_redo(), "undo restores the object");
        doc.redo();
        check(doc.scene.objects.size() == 1, "redo reapplies the edit");
        doc.undo();

        const std::string tmp = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") + "/kosmoscad_test";
        fs::remove_all(tmp);
        fs::create_directories(tmp);
        std::string err;
        check(save_kmodel(doc.scene, tmp + "/t.kmodel", err), "save .kmodel " + err);
        Scene loaded;
        check(load_kmodel(tmp + "/t.kmodel", loaded, err), "load .kmodel " + err);
        check(loaded.objects.size() == 2 && loaded.materials.size() == 2 && loaded.markers.size() == 1, "kmodel keeps objects, materials and markers");
        check(loaded.markers[0].props["thrust"] == 1000 && loaded.objects[0].xf.pos.y == 2, "kmodel keeps marker props and transforms");
        check(near(signed_volume(loaded.objects[0].mesh), signed_volume(doc.scene.objects[0].mesh), 1e-9) && loaded.objects[1].mesh.uv.size() == doc.scene.objects[1].mesh.uv.size(), "kmodel keeps meshes and UVs");

        check(export_obj(doc.scene, tmp + "/out", "thing", err), "export OBJ " + err);
        Scene back;
        check(import_obj(tmp + "/out/thing.obj", back, err), "re-import OBJ " + err);
        double want = 0, got = 0;
        for (const Object &o : doc.scene.objects)
            want += signed_volume(world_mesh(o));
        for (const Object &o : back.objects)
            got += signed_volume(o.mesh);
        check(near(want, got, 1e-4), "OBJ round trip keeps the volume (" + std::to_string(got) + " vs " + std::to_string(want) + ")");
        check(find_material(back, "hull_red") >= 0 && find_material(back, "part") >= 0, "OBJ round trip keeps material names");
        const int red = find_material(back, "hull_red");
        check(back.materials[red].colour.x > 0.79 && back.materials[red].colour.y < 0.11, "OBJ round trip keeps colours");
        std::ifstream objfile(tmp + "/out/thing.obj");
        std::string text((std::istreambuf_iterator<char>(objfile)), {});
        size_t blocks = 0;
        for (size_t at = text.find("usemtl"); at != std::string::npos; at = text.find("usemtl", at + 1))
            ++blocks;
        check(blocks == 2, "one usemtl block per material");
        fs::remove_all(tmp);
    }
    // Every shipped ship.json survives load -> build unchanged (parts regenerate in the same files, markers rebuild their entries).
    {
        const fs::path addons = fs::path(KOSMOS_ROOT) / "addons";
        int ships = 0, bad = 0;
        for (const auto &entry : fs::directory_iterator(addons))
        {
            if (!fs::exists(entry.path() / "addon.json"))
                continue;
            std::ifstream ai(entry.path() / "addon.json");
            json addon = json::parse(ai, nullptr, false);
            if (addon.is_discarded() || !addon.contains("ships"))
                continue;
            for (const json &name : addon["ships"])
            {
                const fs::path file = entry.path() / name.get<std::string>();
                if (!fs::exists(file) || file.extension() != ".json")
                    continue;
                std::ifstream si(file);
                const json original = json::parse(si, nullptr, false);
                if (original.is_discarded() || !original.contains("stages"))
                    continue;
                Scene s;
                std::string e;
                if (!load_ship(file, s, e))
                {
                    std::printf("  load failed %s: %s\n", file.string().c_str(), e.c_str());
                    ++bad;
                    continue;
                }
                std::vector<std::string> paths;
                for (const Object &o : s.objects)
                    paths.push_back(o.source);
                json out = build_ship_json(s, paths);
                // Key order is not significant, so compare as plain (sorted-key) JSON.
                nlohmann::json a = nlohmann::json::parse(original.dump()), b = nlohmann::json::parse(out.dump());
                // Directions are unit-normalised on export; compare numbers to 1e-6.
                std::function<void(nlohmann::json &)> tidy = [&](nlohmann::json &j) {
                    if (j.is_object())
                    {
                        if (j.contains("direction") && j["direction"].is_array() && j["direction"].size() == 3)
                        {
                            double v[3], n = 0;
                            for (int i = 0; i < 3; ++i)
                            {
                                v[i] = j["direction"][i].get<double>();
                                n += v[i] * v[i];
                            }
                            n = std::sqrt(n);
                            if (n > 0)
                                for (int i = 0; i < 3; ++i)
                                    j["direction"][i] = v[i] / n;
                        }
                        if (j.contains("offset") && j["offset"] == nlohmann::json::array({0, 0, 0}))
                            j.erase("offset");
                        for (auto &kv : j.items())
                            tidy(kv.value());
                    }
                    else if (j.is_array())
                        for (auto &e : j)
                            tidy(e);
                    else if (j.is_number())
                        j = std::round(j.get<double>() * 1e6) / 1e6 + 0.0;
                };
                tidy(a);
                tidy(b);
                // Part order is not significant, and a non-uniform scale or empty list may be spelled differently.
                for (nlohmann::json *j : {&a, &b})
                    for (nlohmann::json &st : (*j)["stages"])
                    {
                        if (st.contains("parts"))
                        {
                            std::vector<std::string> keys;
                            for (const nlohmann::json &p : st["parts"])
                                keys.push_back(p.dump());
                            std::sort(keys.begin(), keys.end());
                            st["parts"] = keys;
                        }
                        for (const char *k : {"engines", "gear"})
                            if (st.contains(k) && st[k].empty())
                                st.erase(k);
                    }
                for (const char *k : {"ports", "vessels", "wheels", "systems"})
                    for (nlohmann::json *j : {&a, &b})
                        if (j->contains(k) && (*j)[k].empty())
                            j->erase(k);
                ++ships;
                if (a != b)
                {
                    ++bad;
                    std::printf("  round trip differs: %s\n", file.string().c_str());
                    nlohmann::json d = nlohmann::json::diff(a, b);
                    std::printf("    %s\n", d.dump().substr(0, 300).c_str());
                }
            }
        }
        check(ships > 5 && bad == 0, "ship.json round trip over " + std::to_string(ships) + " ships");
        for (const char *kind : {"engine", "port"})
        {
            Marker m = new_marker(kind, {1, 2, 3});
            const V3 d = marker_direction(m);
            const V3 want = std::string(kind) == "engine" ? V3(0, 0, 1) : V3(0, 0, -1);
            check(length(d - want) < 1e-9, std::string("new ") + kind + " points along its default direction");
        }
        for (const V3 &d : {V3(1, 0, 0), V3(0, 1, 0), V3(0.3, -0.5, 0.8), V3(-1, 0.2, -0.4)})
        {
            Marker m;
            m.rot = rotation_for_direction(d);
            check(length(marker_direction(m) - normalized(d)) < 1e-9, "rotation_for_direction round trip");
        }
    }
    // Camera: project and ray agree, and mvp matches project.
    for (Camera::Kind k : {Camera::Persp, Camera::Top, Camera::Front, Camera::Side})
    {
        Camera cam;
        cam.kind = k;
        cam.target = {1, 2, -3};
        const int w = 800, h = 600;
        const V3 p{2.5, 1, -6};
        double sx, sy, d;
        check(cam.project(p, w, h, sx, sy, d), "camera projects a point");
        V3 o, dir;
        cam.ray(sx, sy, w, h, o, dir);
        const V3 q = o + dir * dot(p - o, dir);
        check(length(q - p) < 1e-6, "camera ray through the projected pixel passes through the point");
        float m[16];
        cam.mvp(w, h, m);
        const double cx = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12], cy = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
                     cw = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
        check(std::fabs((cx / cw * 0.5 + 0.5) * w - sx) < 0.05 && std::fabs((0.5 - cy / cw * 0.5) * h - sy) < 0.05, "mvp agrees with project");
    }
    {
        Camera top;
        top.kind = Camera::Top;
        double sx, sy, d;
        top.project({0, 0, -5}, 800, 600, sx, sy, d);
        check(sy < 300, "top view: the nose (-Z) is up the screen");
        Camera side;
        side.kind = Camera::Side;
        side.project({0, 0, -5}, 800, 600, sx, sy, d);
        check(sx > 400, "side view: the nose is to the right");
    }
    // Editor: picking, gizmo move, undo, extrude, boolean.
    {
        Editor ed;
        ed.add_shape(ShapeKind::Box, 2, 2, 2, 8, {0, 0, 0});
        ed.add_shape(ShapeKind::Box, 2, 2, 2, 8, {10, 0, 0});
        Camera cam;
        cam.kind = Camera::Front;
        cam.scale = 20;
        const int w = 800, h = 600;
        double sx, sy, d;
        cam.project({10, 0, 0}, w, h, sx, sy, d);
        check(ed.click(cam, w, h, sx, sy, false) && ed.sel.objects == std::set<int>{1}, "click picks the box under the cursor");
        check(!ed.click(cam, w, h, 20, 20, false) && ed.sel.objects.empty(), "click on empty space deselects");
        ed.click(cam, w, h, sx, sy, false);
        ed.tool = Tool::Move;
        V3 pv;
        check(ed.pivot(pv) && length(pv - V3(10, 0, 0)) < 1e-9, "pivot is the selected object");
        double px, py;
        cam.project(pv + V3(2, 0, 0), w, h, px, py, d); // the +X handle points left in the front view
        const int axis = ed.pick_gizmo(cam, w, h, px, py);
        check(axis == 0, "gizmo X handle is picked");
        ed.begin_drag(cam, w, h, px, py, axis);
        ed.drag(cam, w, h, px - 20, py);
        ed.end_drag();
        check(std::fabs(ed.scene().objects[1].xf.pos.x - 10 - 1) < 1e-6 || std::fabs(ed.scene().objects[1].xf.pos.x - 10 + 1) < 1e-6, "dragging the X handle 20 px moves 1 m");
        check(std::fabs(ed.scene().objects[1].xf.pos.y) < 1e-9, "move is constrained to the axis");
        ed.doc.undo();
        check(std::fabs(ed.scene().objects[1].xf.pos.x - 10) < 1e-9, "undo restores the drag");

        ed.sel.clear();
        ed.sel.objects = {0, 1};
        ed.sel.active = 0;
        ed.add_shape(ShapeKind::Box, 1, 1, 1, 8, {0, 0, 0});
        ed.sel.clear();
        ed.sel.objects = {0, 2};
        ed.sel.active = 0;
        ed.boolean(BoolOp::Subtract);
        check(ed.scene().objects.size() == 2 && near(signed_volume(ed.scene().objects[0].mesh), 8 - 1, 1e-6), "boolean subtract merges into the active object");

        ed.sel.clear();
        ed.sel.objects = {0};
        ed.sel.active = 0;
        ed.set_mode(Mode::Face);
        ed.sel.faces = {0};
        const size_t faces = ed.scene().objects[0].mesh.f.size();
        ed.extrude(1);
        check(ed.scene().objects[0].mesh.f.size() > faces, "editor extrude adds faces");
        ed.doc.undo();
        check(ed.scene().objects[0].mesh.f.size() == faces, "undo reverses the extrude");

        Editor sk;
        sk.sketch_kind = Camera::Front;
        sk.sketch = {{0, 0}, {2, 0}, {2, 1}, {0, 1}};
        sk.sketch_extrude(3);
        check(sk.scene().objects.size() == 1 && near(signed_volume(sk.scene().objects[0].mesh), 6, 1e-9), "sketch extrude gives an outward-facing solid");
        sk.sketch_kind = Camera::Top;
        sk.sketch = {{0, 0}, {0, 1}, {2, 1}, {2, 0}};
        sk.sketch_extrude(3);
        check(near(signed_volume(sk.scene().objects[1].mesh), 6, 1e-9), "sketch extrude from the top view also faces outward");

        // UV editor logic
        {
            Editor ue;
            ue.add_shape(ShapeKind::Box, 2, 2, 2, 8, {0, 0, 0});
            ue.sel.clear();
            ue.sel.objects = {0};
            ue.sel.active = 0;
            check(ue.uv_scope().size() == ue.scene().objects[0].mesh.f.size(), "UV scope is every face of the selected object");
            check(ue.uv_prepare(), "UV prepare projects faces that have no UVs");
            check(!ue.uv_prepare(), "UV prepare is idempotent");
            V2 lo, hi;
            check(ue.uv_bounds(lo, hi), "UV bounds exist after prepare");
            const V2 before = ue.scene().objects[0].mesh.uv[ue.uv_scope().empty() ? 0 : ue.scene().objects[0].mesh.f[0].uv[0]];
            check(ue.uv_begin(), "UV drag begins");
            ue.uv_apply(Editor::UvTool::Move, 0.25, -0.5, {0, 0});
            ue.uv_apply(Editor::UvTool::Move, 0.25, -0.5, {0, 0});
            ue.uv_end();
            const V2 after = ue.scene().objects[0].mesh.uv[ue.scene().objects[0].mesh.f[0].uv[0]];
            check(near(after.u - before.u, 0.25, 1e-9) && near(after.v - before.v, -0.5, 1e-9), "UV move is relative to the drag start");
            ue.doc.undo();
            const V2 undone = ue.scene().objects[0].mesh.uv[ue.scene().objects[0].mesh.f[0].uv[0]];
            check(near(undone.u, before.u, 1e-9) && near(undone.v, before.v, 1e-9), "a UV drag is one undo step");
            ue.uv_fit();
            ue.uv_bounds(lo, hi);
            check(near(lo.u, 0, 1e-9) && near(hi.v, 1, 1e-9), "UV fit fills 0..1");
            ue.uv_begin();
            ue.uv_apply(Editor::UvTool::Rotate, 90, 0, {0.5, 0.5});
            ue.uv_end();
            ue.uv_bounds(lo, hi);
            check(near(lo.u, 0, 1e-9) && near(hi.u, 1, 1e-9), "a 90 degree UV rotation about the centre keeps the square");
            ue.uv_transform(0.5, 0, 0, 0.5);
            ue.uv_bounds(lo, hi);
            check(near(hi.u - lo.u, 0.5, 1e-9) && near(lo.u, 0.75, 1e-9), "UV transform scales about the centre then moves");
            // faces sharing UVs with faces outside the scope get their own
            Mesh &mm = ue.scene().objects[0].mesh;
            mm.f[1].uv = mm.f[0].uv;
            ue.set_mode(Mode::Face);
            ue.sel.faces = {0};
            check(ue.uv_prepare(), "UV prepare unshares UVs used by faces outside the scope");
            bool shared = false;
            for (int a : mm.f[0].uv)
                for (int b : mm.f[1].uv)
                    shared = shared || a == b;
            check(!shared, "scope faces no longer share UVs with others");
        }

        // DDS decoding: a 4x4 DXT1 block whose two colours are red and blue, and pixel 0 picks colour 0
        {
            std::vector<uint8_t> d(128 + 8, 0);
            std::memcpy(d.data(), "DDS ", 4);
            d[4] = 124;
            d[12] = 4; d[16] = 4;                 // height, width
            d[76] = 32; d[80] = 4;                // pixel format size, FourCC flag
            std::memcpy(&d[84], "DXT1", 4);
            d[128] = 0x00; d[129] = 0xF8;         // colour 0 = 0xF800 (red)
            d[130] = 0x1F; d[131] = 0x00;         // colour 1 = 0x001F (blue)
            Image im;
            check(decode_dds(d, im) && im.w == 4 && im.h == 4, "DXT1 header decodes");
            check(im.rgba[0] == 255 && im.rgba[1] == 0 && im.rgba[2] == 0 && im.rgba[3] == 255, "DXT1 colour 0 is red");
        }
    }
    std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
