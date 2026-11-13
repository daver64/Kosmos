#include "preview.h"

#include <algorithm>
#include <cmath>

namespace shipedit
{
    namespace
    {
        constexpr double pi = 3.14159265358979323846;

        V3 read_vec(const json &j, V3 fallback = {})
        {
            if (j.is_array() && j.size() >= 3 && j[0].is_number() && j[1].is_number() && j[2].is_number())
                return {j[0].get<double>(), j[1].get<double>(), j[2].get<double>()};
            return fallback;
        }

        V3 rotate_xyz(V3 v, V3 deg)
        {
            const double ax = deg.x * pi / 180.0, ay = deg.y * pi / 180.0, az = deg.z * pi / 180.0;
            V3 r = v;
            r = {r.x, r.y * std::cos(ax) - r.z * std::sin(ax), r.y * std::sin(ax) + r.z * std::cos(ax)};
            r = {r.x * std::cos(ay) + r.z * std::sin(ay), r.y, -r.x * std::sin(ay) + r.z * std::cos(ay)};
            r = {r.x * std::cos(az) - r.y * std::sin(az), r.x * std::sin(az) + r.y * std::cos(az), r.z};
            return r;
        }

        struct Camera
        {
            double cy, sy, cp, sp, scale, cx, cyc;
            V3 centre;
            // Ship frame: X right, Y up, Z back (the nose is -Z). The view turns about Y, then tips about X.
            V3 view(V3 p) const
            {
                p = {p.x - centre.x, p.y - centre.y, p.z - centre.z};
                const double x1 = p.x * cy + p.z * sy;
                const double z1 = -p.x * sy + p.z * cy;
                return {x1, p.y * cp - z1 * sp, p.y * sp + z1 * cp};
            }
            void screen(const V3 &v, double &sx, double &sy_) const
            {
                sx = cx + v.x * scale;
                sy_ = cyc - v.y * scale;
            }
        };

        struct Tri
        {
            double sx[3], sy[3], depth;
            double r, g, b;
        };

        void cylinder(Soup &s, double z, double length, double r_tail, double r_nose, int segments = 20)
        {
            const double z0 = z + length * 0.5, z1 = z - length * 0.5;
            for (int i = 0; i < segments; ++i)
            {
                const double a0 = 2 * pi * i / segments, a1 = 2 * pi * (i + 1) / segments;
                const V3 t0{r_tail * std::cos(a0), r_tail * std::sin(a0), z0}, t1{r_tail * std::cos(a1), r_tail * std::sin(a1), z0};
                const V3 n0{r_nose * std::cos(a0), r_nose * std::sin(a0), z1}, n1{r_nose * std::cos(a1), r_nose * std::sin(a1), z1};
                s.p.insert(s.p.end(), {t0, t1, n1, t0, n1, n0});
                s.p.insert(s.p.end(), {V3{0, 0, z1}, n0, n1});
            }
        }

        void fins(Soup &s, const json &part, double z)
        {
            const double r = part.value("radius", 1.0), span = part.value("span", 1.0), len = part.value("length", 1.0);
            const int n = std::max(1, part.value("count", 4));
            for (int k = 0; k < n; ++k)
            {
                const double a = 2 * pi * k / n + pi * 0.25;
                const double c = std::cos(a), sn = std::sin(a);
                const V3 a0{r * c, r * sn, z + len * 0.5}, a1{r * c, r * sn, z - len * 0.5};
                const V3 b0{(r + span) * c, (r + span) * sn, z + len * 0.5};
                s.p.insert(s.p.end(), {a0, a1, b0});
            }
        }

        double shade(const V3 *v)
        {
            const V3 e1{v[1].x - v[0].x, v[1].y - v[0].y, v[1].z - v[0].z}, e2{v[2].x - v[0].x, v[2].y - v[0].y, v[2].z - v[0].z};
            const V3 n{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
            const double len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
            if (len < 1.0e-12)
                return 0.4;
            const double d = std::abs((n.x * 0.35 + n.y * 0.6 + n.z * 0.72) / len);
            return 0.35 + 0.65 * std::min(1.0, d);
        }

        void text(cairo_t *cr, double x, double y, const std::string &s, double r, double g, double b)
        {
            cairo_set_source_rgb(cr, r, g, b);
            cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
            cairo_set_font_size(cr, 12);
            cairo_move_to(cr, x, y);
            cairo_show_text(cr, s.c_str());
        }
    } // namespace

    const Mesh *MeshCache::get(const fs::path &file, std::string &error)
    {
        if (file.empty())
            return nullptr;
        const std::string key = file.string();
        auto it = cache_.find(key);
        if (it == cache_.end())
        {
            Entry e;
            e.ok = load_mesh(file, e.mesh, e.error);
            it = cache_.emplace(key, std::move(e)).first;
        }
        if (!it->second.ok)
        {
            error = it->second.error;
            return nullptr;
        }
        return &it->second.mesh;
    }

    std::shared_ptr<const Geometry> prepare_geometry(const Document &doc, const PreviewView &view, MeshCache &meshes)
    {
        const json &root = doc.root();
        if (!root.contains("stages") || !root["stages"].is_array() || root["stages"].empty())
            return nullptr;
        const json &stages = root["stages"];
        std::string key = stages.dump() + '|' + doc.resolve("x").string() + (view.interactive && view.thin_when_dragging ? "|i" : "|f") + std::to_string(view.triangle_budget);
        if (!meshes.geometry || meshes.geometry_key != key)
        {
            static unsigned counter = 0;
            auto geo = std::make_shared<Geometry>();
            geo->generation = ++counter;
            std::vector<Item> &items = geo->items;
        double zmin = 1e30, zmax = -1e30, rmax = 0.1;
        for (const json &st : stages)
        {
            const double z = st.value("z", 0.0), half = st.value("length", 1.0) * 0.5;
            zmin = std::min(zmin, z - half);
            zmax = std::max(zmax, z + half);
            rmax = std::max(rmax, st.value("radius", 1.0));
        }
        std::size_t model_tris = 0;
        std::vector<const Mesh *> loaded;
        for (const json &st : stages)
        {
            const json parts = st.value("parts", json::array());
            for (const json &part : parts)
            {
                const Mesh *mesh = nullptr;
                if (part.contains("model") && part["model"].is_string())
                {
                    std::string error;
                    mesh = meshes.get(doc.resolve(part["model"].get<std::string>()), error);
                }
                loaded.push_back(mesh);
                if (mesh)
                    model_tris += mesh->triangles.size() / 3;
            }
        }
        const std::size_t stride = std::max<std::size_t>(1, model_tris / (view.interactive && view.thin_when_dragging ? view.triangle_budget / 6 : view.triangle_budget));

        std::size_t part_counter = 0;
        for (std::size_t s = 0; s < stages.size(); ++s)
        {
            const json &st = stages[s];
            const json parts = st.value("parts", json::array());
            for (std::size_t p = 0; p < parts.size(); ++p, ++part_counter)
            {
                const json &part = parts[p];
                Soup soup;
                if (part.contains("model"))
                {
                    const Mesh *mesh = loaded[part_counter];
                    if (!mesh)
                        continue;
                    const V3 offset = read_vec(part.value("offset", json()));
                    const V3 rot = read_vec(part.value("rotate", json()));
                    V3 scale{1, 1, 1};
                    if (part.contains("scale"))
                    {
                        if (part["scale"].is_number())
                            scale = {part["scale"].get<double>(), part["scale"].get<double>(), part["scale"].get<double>()};
                        else
                            scale = read_vec(part["scale"], scale);
                    }
                    soup.r = 0.55;
                    soup.g = 0.62;
                    soup.b = 0.72;
                    const std::size_t count = mesh->triangles.size() / 3;
                    for (std::size_t t = 0; t < count; t += stride)
                        for (std::size_t k = 0; k < 3; ++k)
                        {
                            const std::size_t vi = static_cast<std::size_t>(mesh->triangles[t * 3 + k]) * 3;
                            V3 v{mesh->xyz[vi] * scale.x, mesh->xyz[vi + 1] * scale.y, mesh->xyz[vi + 2] * scale.z};
                            v = rotate_xyz(v, rot);
                            soup.p.push_back({v.x + offset.x, v.y + offset.y, v.z + offset.z});
                        }
                }
                else if (part.contains("prop") && part["prop"].is_string())
                {
                    if (part.contains("colour") && part["colour"].is_array() && part["colour"].size() >= 3)
                    {
                        soup.r = part["colour"][0].get<double>() / 255.0;
                        soup.g = part["colour"][1].get<double>() / 255.0;
                        soup.b = part["colour"][2].get<double>() / 255.0;
                    }
                    const std::string prop = part["prop"].get<std::string>();
                    // A prop's z is its aft end; it runs forward (-Z) by its length.
                    const double len = part.value("length", 1.0), r = part.value("radius", 1.0), z = part.value("z", 0.0) - len * 0.5;
                    if (prop == "fins")
                        fins(soup, part, z);
                    else if (prop == "engines")
                        cylinder(soup, z, len, r * 0.6, r * 0.45);
                    else
                        cylinder(soup, z, len, r, part.value("top_radius", r));
                    const V3 offset = read_vec(part.value("offset", json()));
                    for (V3 &v : soup.p)
                        v = {v.x + offset.x, v.y + offset.y, v.z + offset.z};
                }
                else
                    continue;
                items.push_back({std::move(soup), static_cast<int>(s), static_cast<int>(p), {}});
            }
        }

        // Frame what is really there: the meshes can reach past the stage envelopes.
        for (Item &it : items)
            for (const V3 &v : it.soup.p)
            {
                zmin = std::min(zmin, v.z);
                zmax = std::max(zmax, v.z);
                rmax = std::max(rmax, std::max(std::abs(v.x), std::abs(v.y)));
            }
        // Each triangle's light depends only on its place in the ship, so it is worked out here and not on every frame.
        for (Item &it : items)
            for (std::size_t i = 0; i + 2 < it.soup.p.size(); i += 3)
                it.light.push_back(static_cast<float>(shade(&it.soup.p[i])));
        for (const Item &it : items)
            geo->triangles += it.soup.p.size() / 3;
        geo->zmin = zmin;
        geo->zmax = zmax;
        geo->rmax = rmax;
        meshes.geometry = geo;
        meshes.geometry_key = key;
        }
        return meshes.geometry;
    }

    Projection projection(const Geometry &geo, int width, int height, const PreviewView &view)
    {
        Projection p;
        p.cy = std::cos(view.yaw);
        p.sy = std::sin(view.yaw);
        p.cp = std::cos(view.pitch);
        p.sp = std::sin(view.pitch);
        p.centre_z = (geo.zmin + geo.zmax) * 0.5;
        const double extent = std::max(geo.zmax - geo.zmin, geo.rmax * 2.0);
        p.scale = std::min(width, height) * 0.8 / std::max(extent, 1.0e-3) * view.zoom;
        p.cx = width * 0.5 + view.pan_x;
        p.cyc = height * 0.5 + view.pan_y;
        p.depth_range = (geo.zmax - geo.zmin + geo.rmax * 2.0) * 1.5 + 1.0;
        return p;
    }

    void draw_preview(cairo_t *cr, int width, int height, const Document &doc, const Selection &sel, const PreviewView &view, MeshCache &meshes, bool solids, bool annotations, bool arranging)
    {
        if (solids)
        {
            cairo_pattern_t *bg = cairo_pattern_create_linear(0, 0, 0, height);
            cairo_pattern_add_color_stop_rgb(bg, 0, 0.05, 0.06, 0.09);
            cairo_pattern_add_color_stop_rgb(bg, 1, 0.10, 0.12, 0.16);
            cairo_set_source(cr, bg);
            cairo_paint(cr);
            cairo_pattern_destroy(bg);
        }

        const json &root = doc.root();
        if (!root.contains("stages") || !root["stages"].is_array() || root["stages"].empty())
        {
            text(cr, 20, 30, "No stages. Add one with + Stage.", 0.8, 0.8, 0.8);
            return;
        }
        const json &stages = root["stages"];

        const std::shared_ptr<const Geometry> geo_ptr = prepare_geometry(doc, view, meshes);
        const Geometry &geo = *geo_ptr;
        const double zmin = geo.zmin, zmax = geo.zmax, rmax = geo.rmax;
        const double extent = std::max(zmax - zmin, rmax * 2.0);
        const Projection pj = projection(geo, width, height, view);
        Camera cam;
        std::vector<Tri> tris;
        cam.cy = pj.cy;
        cam.sy = pj.sy;
        cam.cp = pj.cp;
        cam.sp = pj.sp;
        cam.centre = {0, 0, pj.centre_z};
        cam.scale = pj.scale;
        cam.cx = pj.cx;
        cam.cyc = pj.cyc;

        if (solids)
        {
        tris.reserve(geo.triangles);
        for (const Item &it : geo.items)
        {
            const bool stage_selected = sel.stage == it.stage;
            const double tint = sel.kind == Selection::none || stage_selected ? 1.0 : 0.55;
            const bool highlight = sel.kind == Selection::part && stage_selected && sel.item == it.part;
            const double lift = highlight ? 0.25 : 0.0;
            for (std::size_t i = 0; i + 2 < it.soup.p.size(); i += 3)
            {
                Tri t;
                double depth = 0;
                for (int k = 0; k < 3; ++k)
                {
                    const V3 v = cam.view(it.soup.p[i + k]);
                    cam.screen(v, t.sx[k], t.sy[k]);
                    depth += v.z;
                }
                t.depth = depth / 3.0;
                const double light = it.light[i / 3];
                t.r = std::min(1.0, it.soup.r * light * tint + lift * 0.6);
                t.g = std::min(1.0, it.soup.g * light * tint + lift * 0.45);
                t.b = std::min(1.0, it.soup.b * light * tint);
                tris.push_back(t);
            }
        }
        std::sort(tris.begin(), tris.end(), [](const Tri &a, const Tri &b) { return a.depth < b.depth; });
        cairo_set_line_width(cr, 0.4);
        // Seams between smoothed triangles show the background through. A first pass without smoothing closes them; when still, a second
        // smoothed pass over it softens the edges. That is much cheaper than stroking every triangle.
        for (int pass = 0; pass < (view.interactive ? 1 : 2); ++pass)
        {
            cairo_set_antialias(cr, pass == 0 ? CAIRO_ANTIALIAS_NONE : CAIRO_ANTIALIAS_DEFAULT);
            for (const Tri &t : tris)
            {
                const double minx = std::min({t.sx[0], t.sx[1], t.sx[2]}), maxx = std::max({t.sx[0], t.sx[1], t.sx[2]});
                const double miny = std::min({t.sy[0], t.sy[1], t.sy[2]}), maxy = std::max({t.sy[0], t.sy[1], t.sy[2]});
                if (maxx < 0 || minx > width || maxy < 0 || miny > height)
                    continue;
                // A triangle under a pixel only needs the unsmoothed pass.
                if (pass == 1 && maxx - minx < 1.0 && maxy - miny < 1.0)
                    continue;
                cairo_move_to(cr, t.sx[0], t.sy[0]);
                cairo_line_to(cr, t.sx[1], t.sy[1]);
                cairo_line_to(cr, t.sx[2], t.sy[2]);
                cairo_close_path(cr);
                cairo_set_source_rgb(cr, t.r, t.g, t.b);
                cairo_fill(cr);
            }
        }
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
        }

        if (!annotations)
            return;

        auto line3 = [&](V3 a, V3 b) {
            double ax, ay, bx, by;
            cam.screen(cam.view(a), ax, ay);
            cam.screen(cam.view(b), bx, by);
            cairo_move_to(cr, ax, ay);
            cairo_line_to(cr, bx, by);
        };
        for (std::size_t s = 0; s < stages.size(); ++s)
        {
            const json &st = stages[s];
            const bool picked = sel.stage == static_cast<int>(s);
            const double z = st.value("z", 0.0), half = st.value("length", 1.0) * 0.5, r = st.value("radius", 1.0);
            cairo_set_line_width(cr, picked ? 1.6 : 0.8);
            if (picked)
                cairo_set_source_rgba(cr, 1.0, 0.75, 0.2, 0.95);
            else
                cairo_set_source_rgba(cr, 0.4, 0.8, 1.0, 0.45);
            const int n = 24;
            for (double zz : {z - half, z + half})
                for (int i = 0; i < n; ++i)
                    line3({r * std::cos(2 * pi * i / n), r * std::sin(2 * pi * i / n), zz}, {r * std::cos(2 * pi * (i + 1) / n), r * std::sin(2 * pi * (i + 1) / n), zz});
            for (int k = 0; k < 4; ++k)
                line3({r * std::cos(k * pi / 2), r * std::sin(k * pi / 2), z - half}, {r * std::cos(k * pi / 2), r * std::sin(k * pi / 2), z + half});
            cairo_stroke(cr);

            double lx, ly;
            cam.screen(cam.view({r, 0, z}), lx, ly);
            text(cr, lx + 6, ly, std::to_string(s + 1) + " " + st.value("name", std::string("Stage")), picked ? 1.0 : 0.6, picked ? 0.8 : 0.85, picked ? 0.3 : 1.0);

            const json engines = st.value("engines", json::array());
            for (std::size_t e = 0; e < engines.size(); ++e)
            {
                const V3 p = read_vec(engines[e].value("position", json()));
                const V3 d = read_vec(engines[e].value("direction", json()), {0, 0, -1});
                const double dl = std::max(1.0e-6, std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z));
                const double plume = std::clamp(engines[e].value("plume", 1.0) * 0.1, 0.5, 6.0) * std::max(0.4, r * 0.5);
                const bool ep = sel.kind == Selection::engine && sel.stage == static_cast<int>(s) && sel.item == static_cast<int>(e);
                cairo_set_line_width(cr, ep ? 3.0 : 1.5);
                cairo_set_source_rgba(cr, 1.0, ep ? 0.8 : 0.45, 0.2, 0.95);
                line3(p, {p.x - d.x / dl * plume, p.y - d.y / dl * plume, p.z - d.z / dl * plume});
                cairo_stroke(cr);
                double px, py;
                cam.screen(cam.view(p), px, py);
                cairo_arc(cr, px, py, ep ? 4.5 : 3.0, 0, 2 * pi);
                cairo_fill(cr);
            }
            const json gear = st.value("gear", json::array());
            for (std::size_t g = 0; g < gear.size(); ++g)
            {
                const bool gp = sel.kind == Selection::gear && sel.stage == static_cast<int>(s) && sel.item == static_cast<int>(g);
                double px, py;
                cam.screen(cam.view(read_vec(gear[g])), px, py);
                cairo_set_source_rgba(cr, 0.4, 1.0, 0.5, 0.95);
                cairo_arc(cr, px, py, gp ? 5.0 : 3.0, 0, 2 * pi);
                cairo_fill(cr);
            }
        }

        double nx, ny;
        cam.screen(cam.view({0, 0, zmin - extent * 0.08}), nx, ny);
        text(cr, nx - 14, ny - 6, "nose", 0.9, 0.9, 0.6);
        text(cr, 12, height - 10, arranging ? "Drag a stage to move it   Middle/right or Shift+drag: pan   Wheel: zoom" :
                                             "Drag: orbit   Middle/right or Shift+drag: pan   Wheel: zoom   Ctrl+drag: zoom", 0.55, 0.6, 0.7);
    }
} // namespace shipedit
