#include "mesh.h"

#include <map>
#include <unordered_map>

namespace cad
{
    void Mesh::append(const Mesh &o)
    {
        const int vo = static_cast<int>(v.size()), uo = static_cast<int>(uv.size());
        v.insert(v.end(), o.v.begin(), o.v.end());
        uv.insert(uv.end(), o.uv.begin(), o.uv.end());
        for (Face face : o.f)
        {
            for (int &i : face.v)
                i += vo;
            for (int &i : face.uv)
                i += uo;
            f.push_back(std::move(face));
        }
    }

    V3 face_normal(const Mesh &m, int f)
    {
        const auto &idx = m.f[f].v;
        V3 n;
        for (size_t i = 0; i < idx.size(); ++i)
        {
            const V3 &a = m.v[idx[i]], &b = m.v[idx[(i + 1) % idx.size()]];
            n.x += (a.y - b.y) * (a.z + b.z);
            n.y += (a.z - b.z) * (a.x + b.x);
            n.z += (a.x - b.x) * (a.y + b.y);
        }
        return normalized(n);
    }

    V3 face_centre(const Mesh &m, int f)
    {
        V3 c;
        for (int i : m.f[f].v)
            c += m.v[i];
        return m.f[f].v.empty() ? c : c / static_cast<double>(m.f[f].v.size());
    }

    EdgeKey edge_key(int a, int b) { return a < b ? EdgeKey{a, b} : EdgeKey{b, a}; }

    std::vector<std::pair<EdgeKey, std::vector<int>>> edge_faces(const Mesh &m)
    {
        std::map<EdgeKey, std::vector<int>> table;
        for (size_t fi = 0; fi < m.f.size(); ++fi)
        {
            const auto &idx = m.f[fi].v;
            for (size_t i = 0; i < idx.size(); ++i)
            {
                auto &list = table[edge_key(idx[i], idx[(i + 1) % idx.size()])];
                if (list.empty() || list.back() != static_cast<int>(fi))
                    list.push_back(static_cast<int>(fi));
            }
        }
        return {table.begin(), table.end()};
    }

    std::vector<Tri> triangulate_face(const Mesh &m, int f)
    {
        const auto &idx = m.f[f].v;
        const int n = static_cast<int>(idx.size());
        std::vector<Tri> out;
        if (n < 3)
            return out;
        if (n == 3)
            return {Tri{idx[0], idx[1], idx[2]}};
        const V3 nrm = face_normal(m, f);
        const double ax = std::fabs(nrm.x), ay = std::fabs(nrm.y), az = std::fabs(nrm.z);
        // Drop the dominant axis; keep the winding positive in the remaining plane.
        auto project = [&](int vi) -> std::pair<double, double>
        {
            const V3 &p = m.v[vi];
            if (ax >= ay && ax >= az)
                return nrm.x > 0 ? std::make_pair(p.y, p.z) : std::make_pair(p.z, p.y);
            if (ay >= az)
                return nrm.y > 0 ? std::make_pair(p.z, p.x) : std::make_pair(p.x, p.z);
            return nrm.z > 0 ? std::make_pair(p.x, p.y) : std::make_pair(p.y, p.x);
        };
        std::vector<int> poly(idx.begin(), idx.end());
        auto cross2 = [&](int a, int b, int c)
        {
            const auto pa = project(a), pb = project(b), pc = project(c);
            return (pb.first - pa.first) * (pc.second - pa.second) - (pb.second - pa.second) * (pc.first - pa.first);
        };
        auto inside = [&](int a, int b, int c, int p)
        {
            return cross2(a, b, p) >= -1e-12 && cross2(b, c, p) >= -1e-12 && cross2(c, a, p) >= -1e-12;
        };
        int guard = n * n + 8;
        while (poly.size() > 3 && guard-- > 0)
        {
            bool clipped = false;
            const int k = static_cast<int>(poly.size());
            for (int i = 0; i < k && !clipped; ++i)
            {
                const int a = poly[(i + k - 1) % k], b = poly[i], c = poly[(i + 1) % k];
                if (cross2(a, b, c) <= 1e-14)
                    continue;
                bool ear = true;
                for (int j = 0; j < k && ear; ++j)
                {
                    const int p = poly[j];
                    if (p != a && p != b && p != c && inside(a, b, c, p))
                        ear = false;
                }
                if (ear)
                {
                    out.push_back({a, b, c});
                    poly.erase(poly.begin() + i);
                    clipped = true;
                }
            }
            if (!clipped)
                break;
        }
        if (poly.size() == 3)
            out.push_back({poly[0], poly[1], poly[2]});
        else
            for (size_t i = 1; i + 1 < poly.size(); ++i)
                out.push_back({poly[0], poly[i], poly[i + 1]});
        return out;
    }

    void bounds(const Mesh &m, V3 &lo, V3 &hi)
    {
        if (m.v.empty())
        {
            lo = hi = V3();
            return;
        }
        lo = hi = m.v[0];
        for (const V3 &p : m.v)
            for (int a = 0; a < 3; ++a)
            {
                lo[a] = std::min(lo[a], p[a]);
                hi[a] = std::max(hi[a], p[a]);
            }
    }

    double signed_volume(const Mesh &m)
    {
        double vol = 0;
        for (size_t fi = 0; fi < m.f.size(); ++fi)
            for (const Tri &t : triangulate_face(m, static_cast<int>(fi)))
                vol += dot(m.v[t[0]], cross(m.v[t[1]], m.v[t[2]])) / 6.0;
        return vol;
    }

    void flip_faces(Mesh &m, const std::set<int> &faces)
    {
        for (int fi : faces)
        {
            if (fi < 0 || fi >= static_cast<int>(m.f.size()))
                continue;
            std::reverse(m.f[fi].v.begin(), m.f[fi].v.end());
            std::reverse(m.f[fi].uv.begin(), m.f[fi].uv.end());
        }
    }

    void apply_xform(Mesh &m, const Xform &x)
    {
        for (V3 &p : m.v)
            p = x.apply(p);
        if (x.scale.x * x.scale.y * x.scale.z < 0)
        {
            std::set<int> all;
            for (size_t i = 0; i < m.f.size(); ++i)
                all.insert(static_cast<int>(i));
            flip_faces(m, all);
        }
    }

    void remove_unused_vertices(Mesh &m)
    {
        std::vector<int> map(m.v.size(), -1);
        std::vector<int> umap(m.uv.size(), -1);
        std::vector<V3> nv;
        std::vector<V2> nuv;
        for (Face &face : m.f)
        {
            for (int &i : face.v)
            {
                if (map[i] < 0)
                {
                    map[i] = static_cast<int>(nv.size());
                    nv.push_back(m.v[i]);
                }
                i = map[i];
            }
            for (int &i : face.uv)
            {
                if (umap[i] < 0)
                {
                    umap[i] = static_cast<int>(nuv.size());
                    nuv.push_back(m.uv[i]);
                }
                i = umap[i];
            }
        }
        m.v = std::move(nv);
        m.uv = std::move(nuv);
    }

    void delete_faces(Mesh &m, const std::set<int> &faces)
    {
        std::vector<Face> keep;
        for (size_t i = 0; i < m.f.size(); ++i)
            if (!faces.count(static_cast<int>(i)))
                keep.push_back(std::move(m.f[i]));
        m.f = std::move(keep);
        remove_unused_vertices(m);
    }

    std::vector<int> extrude_faces(Mesh &m, const std::set<int> &faces, double distance)
    {
        std::vector<int> result;
        std::set<int> sel;
        for (int fi : faces)
            if (fi >= 0 && fi < static_cast<int>(m.f.size()))
                sel.insert(fi);
        if (sel.empty())
            return result;

        std::map<int, V3> vnormal;
        std::set<std::pair<int, int>> directed;
        for (int fi : sel)
        {
            const V3 n = face_normal(m, fi);
            const auto &idx = m.f[fi].v;
            for (size_t i = 0; i < idx.size(); ++i)
            {
                vnormal[idx[i]] += n;
                directed.insert({idx[i], idx[(i + 1) % idx.size()]});
            }
        }
        std::map<int, int> copy;
        for (auto &[old, n] : vnormal)
        {
            copy[old] = static_cast<int>(m.v.size());
            m.v.push_back(m.v[old] + normalized(n) * distance);
        }
        std::vector<Face> walls;
        for (int fi : sel)
        {
            const auto &idx = m.f[fi].v;
            for (size_t i = 0; i < idx.size(); ++i)
            {
                const int a = idx[i], b = idx[(i + 1) % idx.size()];
                if (directed.count({b, a}))
                    continue;
                Face wall;
                wall.v = {a, b, copy[b], copy[a]};
                wall.mat = m.f[fi].mat;
                walls.push_back(std::move(wall));
            }
        }
        for (int fi : sel)
        {
            for (int &i : m.f[fi].v)
                i = copy[i];
            result.push_back(fi);
        }
        for (Face &w : walls)
            m.f.push_back(std::move(w));
        return result;
    }

    std::vector<int> inset_faces(Mesh &m, const std::set<int> &faces, double amount)
    {
        std::vector<int> result;
        const double t = std::clamp(amount, 0.0, 0.95);
        std::vector<Face> rings;
        for (int fi : faces)
        {
            if (fi < 0 || fi >= static_cast<int>(m.f.size()))
                continue;
            Face original = m.f[fi];
            const int n = static_cast<int>(original.v.size());
            const V3 c = face_centre(m, fi);
            std::vector<int> inner(n), inner_uv;
            for (int i = 0; i < n; ++i)
            {
                inner[i] = static_cast<int>(m.v.size());
                m.v.push_back(lerp(m.v[original.v[i]], c, t));
            }
            const bool has_uv = original.uv.size() == original.v.size();
            if (has_uv)
            {
                V2 uc;
                for (int u : original.uv)
                {
                    uc.u += m.uv[u].u / n;
                    uc.v += m.uv[u].v / n;
                }
                inner_uv.resize(n);
                for (int i = 0; i < n; ++i)
                {
                    const V2 &o = m.uv[original.uv[i]];
                    inner_uv[i] = static_cast<int>(m.uv.size());
                    m.uv.push_back({o.u + (uc.u - o.u) * t, o.v + (uc.v - o.v) * t});
                }
            }
            for (int i = 0; i < n; ++i)
            {
                const int j = (i + 1) % n;
                Face ring;
                ring.v = {original.v[i], original.v[j], inner[j], inner[i]};
                ring.mat = original.mat;
                if (has_uv)
                    ring.uv = {original.uv[i], original.uv[j], inner_uv[j], inner_uv[i]};
                rings.push_back(std::move(ring));
            }
            m.f[fi].v = inner;
            m.f[fi].uv = inner_uv;
            result.push_back(fi);
        }
        for (Face &r : rings)
            m.f.push_back(std::move(r));
        return result;
    }

    int split_edge(Mesh &m, int a, int b)
    {
        int mid = -1;
        for (Face &face : m.f)
        {
            const int n = static_cast<int>(face.v.size());
            for (int i = 0; i < n; ++i)
            {
                const int p = face.v[i], q = face.v[(i + 1) % n];
                if (!((p == a && q == b) || (p == b && q == a)))
                    continue;
                if (mid < 0)
                {
                    mid = static_cast<int>(m.v.size());
                    m.v.push_back((m.v[a] + m.v[b]) * 0.5);
                }
                face.v.insert(face.v.begin() + i + 1, mid);
                if (face.uv.size() == static_cast<size_t>(n))
                {
                    const V2 &u0 = m.uv[face.uv[i]], &u1 = m.uv[face.uv[(i + 1) % n]];
                    face.uv.insert(face.uv.begin() + i + 1, static_cast<int>(m.uv.size()));
                    m.uv.push_back({(u0.u + u1.u) * 0.5, (u0.v + u1.v) * 0.5});
                }
                break;
            }
        }
        return mid;
    }

    void weld(Mesh &m, double eps)
    {
        if (m.v.empty())
            return;
        const double cell = std::max(eps, 1e-9);
        auto key = [&](long x, long y, long z) { return (x * 73856093L) ^ (y * 19349663L) ^ (z * 83492791L); };
        std::unordered_map<long, std::vector<int>> grid;
        std::vector<int> map(m.v.size());
        std::vector<V3> kept;
        for (size_t i = 0; i < m.v.size(); ++i)
        {
            const V3 &p = m.v[i];
            const long cx = std::lround(std::floor(p.x / cell)), cy = std::lround(std::floor(p.y / cell)), cz = std::lround(std::floor(p.z / cell));
            int found = -1;
            for (long dx = -1; dx <= 1 && found < 0; ++dx)
                for (long dy = -1; dy <= 1 && found < 0; ++dy)
                    for (long dz = -1; dz <= 1 && found < 0; ++dz)
                    {
                        auto it = grid.find(key(cx + dx, cy + dy, cz + dz));
                        if (it == grid.end())
                            continue;
                        for (int k : it->second)
                            if (length(kept[k] - p) <= eps)
                            {
                                found = k;
                                break;
                            }
                    }
            if (found < 0)
            {
                found = static_cast<int>(kept.size());
                kept.push_back(p);
                grid[key(cx, cy, cz)].push_back(found);
            }
            map[i] = found;
        }
        std::vector<Face> faces;
        for (Face &face : m.f)
        {
            Face nf;
            nf.mat = face.mat;
            const bool has_uv = face.uv.size() == face.v.size();
            for (size_t i = 0; i < face.v.size(); ++i)
            {
                const int nv = map[face.v[i]];
                if (!nf.v.empty() && nf.v.back() == nv)
                    continue;
                nf.v.push_back(nv);
                if (has_uv)
                    nf.uv.push_back(face.uv[i]);
            }
            while (nf.v.size() > 1 && nf.v.front() == nf.v.back())
            {
                nf.v.pop_back();
                if (!nf.uv.empty())
                    nf.uv.pop_back();
            }
            if (nf.v.size() >= 3)
                faces.push_back(std::move(nf));
        }
        m.v = std::move(kept);
        m.f = std::move(faces);
        remove_unused_vertices(m);
    }

    void mirror(Mesh &m, int axis, bool weld_seam)
    {
        const int vo = static_cast<int>(m.v.size()), uo = static_cast<int>(m.uv.size());
        const size_t fo = m.f.size();
        for (int i = 0; i < vo; ++i)
        {
            V3 p = m.v[i];
            p[axis] = -p[axis];
            m.v.push_back(p);
        }
        for (int i = 0; i < uo; ++i)
            m.uv.push_back({1.0 - m.uv[i].u, m.uv[i].v});
        for (size_t i = 0; i < fo; ++i)
        {
            Face copy = m.f[i];
            for (int &k : copy.v)
                k += vo;
            for (int &k : copy.uv)
                k += uo;
            std::reverse(copy.v.begin(), copy.v.end());
            std::reverse(copy.uv.begin(), copy.uv.end());
            m.f.push_back(std::move(copy));
        }
        if (weld_seam)
            weld(m, 1e-6);
    }

    void move_vertices(Mesh &m, const std::set<int> &verts, const V3 &delta)
    {
        for (int i : verts)
            if (i >= 0 && i < static_cast<int>(m.v.size()))
                m.v[i] += delta;
    }

    void project_uv(Mesh &m, const std::set<int> &faces, UvMode mode, int axis)
    {
        std::vector<int> targets;
        if (faces.empty())
            for (size_t i = 0; i < m.f.size(); ++i)
                targets.push_back(static_cast<int>(i));
        else
            targets.assign(faces.begin(), faces.end());
        if (targets.empty())
            return;
        V3 lo(1e30, 1e30, 1e30), hi(-1e30, -1e30, -1e30);
        for (int fi : targets)
            for (int vi : m.f[fi].v)
                for (int a = 0; a < 3; ++a)
                {
                    lo[a] = std::min(lo[a], m.v[vi][a]);
                    hi[a] = std::max(hi[a], m.v[vi][a]);
                }
        const V3 centre = (lo + hi) * 0.5;
        auto span = [&](int a) { return std::max(hi[a] - lo[a], 1e-9); };
        auto planar = [&](const V3 &p, int ax) -> V2
        {
            if (ax == 0)
                return {(p.z - lo.z) / span(2), (p.y - lo.y) / span(1)};
            if (ax == 1)
                return {(p.x - lo.x) / span(0), (p.z - lo.z) / span(2)};
            return {(p.x - lo.x) / span(0), (p.y - lo.y) / span(1)};
        };
        for (int fi : targets)
        {
            Face &face = m.f[fi];
            std::vector<V2> coords;
            int ax = axis;
            if (mode == UvMode::Box)
            {
                const V3 n = face_normal(m, fi);
                ax = std::fabs(n.x) >= std::fabs(n.y) && std::fabs(n.x) >= std::fabs(n.z) ? 0 : (std::fabs(n.y) >= std::fabs(n.z) ? 1 : 2);
            }
            for (int vi : face.v)
            {
                const V3 &p = m.v[vi];
                if (mode == UvMode::Planar || mode == UvMode::Box)
                    coords.push_back(planar(p, ax));
                else
                {
                    const double u = std::atan2(p.z - centre.z, p.x - centre.x) / (2 * kPi) + 0.5;
                    double v;
                    if (mode == UvMode::Cylindrical)
                        v = (p.y - lo.y) / span(1);
                    else
                    {
                        const V3 d = p - centre;
                        v = std::asin(std::clamp(length(d) > 1e-12 ? d.y / length(d) : 0.0, -1.0, 1.0)) / kPi + 0.5;
                    }
                    coords.push_back({u, v});
                }
            }
            if (mode == UvMode::Cylindrical || mode == UvMode::Spherical)
            {
                double mn = 1e9, mx = -1e9;
                for (const V2 &c : coords)
                {
                    mn = std::min(mn, c.u);
                    mx = std::max(mx, c.u);
                }
                if (mx - mn > 0.5)
                    for (V2 &c : coords)
                        if (c.u < 0.5)
                            c.u += 1.0;
            }
            face.uv.clear();
            for (const V2 &c : coords)
            {
                face.uv.push_back(static_cast<int>(m.uv.size()));
                m.uv.push_back(c);
            }
        }
    }

    namespace
    {
        void add_face(Mesh &m, std::vector<int> v, const V3 &outward)
        {
            Face face;
            face.v = std::move(v);
            m.f.push_back(std::move(face));
            const int fi = static_cast<int>(m.f.size()) - 1;
            if (dot(face_normal(m, fi), outward) < 0)
                std::reverse(m.f[fi].v.begin(), m.f[fi].v.end());
        }
    }

    Mesh make_box(double sx, double sy, double sz)
    {
        Mesh m;
        const double hx = sx / 2, hy = sy / 2, hz = sz / 2;
        for (int i = 0; i < 8; ++i)
            m.v.push_back({(i & 1) ? hx : -hx, (i & 2) ? hy : -hy, (i & 4) ? hz : -hz});
        add_face(m, {1, 3, 7, 5}, {1, 0, 0});
        add_face(m, {0, 2, 6, 4}, {-1, 0, 0});
        add_face(m, {2, 3, 7, 6}, {0, 1, 0});
        add_face(m, {0, 1, 5, 4}, {0, -1, 0});
        add_face(m, {4, 5, 7, 6}, {0, 0, 1});
        add_face(m, {0, 1, 3, 2}, {0, 0, -1});
        return m;
    }

    Mesh make_lathe(const std::vector<V2> &profile, int segments, bool cap_ends)
    {
        Mesh m;
        const int S = std::max(segments, 3), P = static_cast<int>(profile.size());
        if (P < 2)
            return m;
        std::vector<std::vector<int>> ring(P, std::vector<int>(S));
        for (int i = 0; i < P; ++i)
        {
            const bool pole = std::fabs(profile[i].u) < 1e-9;
            int pole_index = -1;
            for (int j = 0; j < S; ++j)
            {
                if (pole)
                {
                    if (pole_index < 0)
                    {
                        pole_index = static_cast<int>(m.v.size());
                        m.v.push_back({0, profile[i].v, 0});
                    }
                    ring[i][j] = pole_index;
                    continue;
                }
                const double a = 2 * kPi * j / S;
                ring[i][j] = static_cast<int>(m.v.size());
                m.v.push_back({profile[i].u * std::cos(a), profile[i].v, profile[i].u * std::sin(a)});
            }
        }
        for (int i = 0; i + 1 < P; ++i)
            for (int j = 0; j < S; ++j)
            {
                const int k = (j + 1) % S;
                Face face;
                for (int vi : {ring[i][j], ring[i + 1][j], ring[i + 1][k], ring[i][k]})
                    if (face.v.empty() || face.v.back() != vi)
                        face.v.push_back(vi);
                while (face.v.size() > 1 && face.v.front() == face.v.back())
                    face.v.pop_back();
                if (face.v.size() >= 3)
                    m.f.push_back(std::move(face));
            }
        if (cap_ends)
        {
            if (profile[0].u > 1e-9)
                add_face(m, ring[0], {0, profile[0].v - profile[1].v, 0});
            if (profile[P - 1].u > 1e-9)
                add_face(m, ring[P - 1], {0, profile[P - 1].v - profile[P - 2].v, 0});
        }
        return m;
    }

    Mesh make_cylinder(double radius, double height, int segments, bool caps)
    {
        return make_lathe({{radius, -height / 2}, {radius, height / 2}}, segments, caps);
    }

    Mesh make_cone(double radius_bottom, double radius_top, double height, int segments)
    {
        return make_lathe({{radius_bottom, -height / 2}, {radius_top, height / 2}}, segments, true);
    }

    Mesh make_sphere(double radius, int segments, int rings)
    {
        std::vector<V2> profile;
        const int R = std::max(rings, 2);
        for (int i = 0; i <= R; ++i)
        {
            const double t = kPi * i / R;
            profile.push_back({i == 0 || i == R ? 0.0 : radius * std::sin(t), -radius * std::cos(t)});
        }
        return make_lathe(profile, segments, false);
    }

    Mesh make_torus(double major, double minor, int segments, int ring_segments)
    {
        Mesh m;
        const int S = std::max(segments, 3), R = std::max(ring_segments, 3);
        for (int i = 0; i < S; ++i)
            for (int j = 0; j < R; ++j)
            {
                const double u = 2 * kPi * i / S, v = 2 * kPi * j / R;
                const double r = major + minor * std::cos(v);
                m.v.push_back({r * std::cos(u), minor * std::sin(v), r * std::sin(u)});
            }
        auto at = [&](int i, int j) { return (i % S) * R + (j % R); };
        for (int i = 0; i < S; ++i)
            for (int j = 0; j < R; ++j)
            {
                const double u = 2 * kPi * (i + 0.5) / S, v = 2 * kPi * (j + 0.5) / R;
                const V3 out(std::cos(v) * std::cos(u), std::sin(v), std::cos(v) * std::sin(u));
                add_face(m, {at(i, j), at(i, j + 1), at(i + 1, j + 1), at(i + 1, j)}, out);
            }
        return m;
    }

    Mesh make_plane(double width, double depth, int div_x, int div_z)
    {
        Mesh m;
        const int X = std::max(div_x, 1), Z = std::max(div_z, 1);
        for (int j = 0; j <= Z; ++j)
            for (int i = 0; i <= X; ++i)
                m.v.push_back({-width / 2 + width * i / X, 0, -depth / 2 + depth * j / Z});
        for (int j = 0; j < Z; ++j)
            for (int i = 0; i < X; ++i)
            {
                const int a = j * (X + 1) + i;
                add_face(m, {a, a + 1, a + X + 2, a + X + 1}, {0, 1, 0});
            }
        return m;
    }

    namespace
    {
        double area2(const std::vector<V2> &p)
        {
            double a = 0;
            for (size_t i = 0; i < p.size(); ++i)
            {
                const V2 &c = p[i], &n = p[(i + 1) % p.size()];
                a += c.u * n.v - n.u * c.v;
            }
            return a;
        }
    }

    Mesh make_loft(const std::vector<std::vector<V2>> &outlines_in, const std::vector<double> &z, bool cap_ends)
    {
        Mesh m;
        const size_t L = std::min(outlines_in.size(), z.size());
        if (L < 2)
            return m;
        const size_t N = outlines_in[0].size();
        if (N < 3)
            return m;
        std::vector<std::vector<V2>> outlines(outlines_in.begin(), outlines_in.begin() + L);
        for (auto &o : outlines)
            if (o.size() != N)
                return m;
        if (area2(outlines[0]) < 0)
            for (auto &o : outlines)
                std::reverse(o.begin(), o.end());
        for (size_t k = 0; k < L; ++k)
            for (size_t i = 0; i < N; ++i)
                m.v.push_back({outlines[k][i].u, outlines[k][i].v, z[k]});
        auto at = [&](size_t k, size_t i) { return static_cast<int>(k * N + i % N); };
        for (size_t k = 0; k + 1 < L; ++k)
            for (size_t i = 0; i < N; ++i)
            {
                const V2 &a = outlines[k][i], &b = outlines[k][(i + 1) % N];
                const V3 outward(b.v - a.v, -(b.u - a.u), 0); // right of travel for a counter-clockwise outline
                add_face(m, {at(k, i), at(k, i + 1), at(k + 1, i + 1), at(k + 1, i)}, outward);
            }
        if (cap_ends)
        {
            std::vector<int> first, last;
            for (size_t i = 0; i < N; ++i)
            {
                first.push_back(at(0, i));
                last.push_back(at(L - 1, i));
            }
            const double dz = z[L - 1] - z[0];
            add_face(m, first, {0, 0, dz > 0 ? -1.0 : 1.0});
            add_face(m, last, {0, 0, dz > 0 ? 1.0 : -1.0});
        }
        return m;
    }

    Mesh make_extrusion(const std::vector<V2> &outline, double depth)
    {
        return make_loft({outline, outline}, {-depth / 2, depth / 2}, true);
    }
}
