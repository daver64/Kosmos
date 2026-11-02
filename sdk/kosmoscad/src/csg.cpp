#include "csg.h"

#include <memory>

namespace cad
{
    namespace
    {
        constexpr double kEps = 1e-6;

        struct Plane
        {
            V3 n;
            double w = 0;
            bool valid() const { return length(n) > 0.5; }
            void flip()
            {
                n = -n;
                w = -w;
            }
        };

        struct Poly
        {
            std::vector<V3> p;
            int mat = 0;
            Plane plane;
        };

        Poly make_poly(std::vector<V3> pts, int mat)
        {
            Poly poly;
            poly.p = std::move(pts);
            poly.mat = mat;
            poly.plane.n = normalized(cross(poly.p[1] - poly.p[0], poly.p[2] - poly.p[0]));
            poly.plane.w = dot(poly.plane.n, poly.p[0]);
            return poly;
        }

        void flip(Poly &poly)
        {
            std::reverse(poly.p.begin(), poly.p.end());
            poly.plane.flip();
        }

        void split_polygon(const Plane &pl, const Poly &poly, std::vector<Poly> &coplanar_front, std::vector<Poly> &coplanar_back,
                           std::vector<Poly> &front, std::vector<Poly> &back)
        {
            enum { Coplanar = 0, Front = 1, Back = 2, Spanning = 3 };
            int type = 0;
            std::vector<int> types;
            for (const V3 &v : poly.p)
            {
                const double t = dot(pl.n, v) - pl.w;
                const int ty = t < -kEps ? Back : (t > kEps ? Front : Coplanar);
                type |= ty;
                types.push_back(ty);
            }
            switch (type)
            {
            case Coplanar:
                (dot(pl.n, poly.plane.n) > 0 ? coplanar_front : coplanar_back).push_back(poly);
                break;
            case Front:
                front.push_back(poly);
                break;
            case Back:
                back.push_back(poly);
                break;
            default:
            {
                std::vector<V3> f, b;
                const size_t n = poly.p.size();
                for (size_t i = 0; i < n; ++i)
                {
                    const size_t j = (i + 1) % n;
                    const int ti = types[i], tj = types[j];
                    const V3 &vi = poly.p[i], &vj = poly.p[j];
                    if (ti != Back)
                        f.push_back(vi);
                    if (ti != Front)
                        b.push_back(vi);
                    if ((ti | tj) == Spanning)
                    {
                        const double t = (pl.w - dot(pl.n, vi)) / dot(pl.n, vj - vi);
                        const V3 v = lerp(vi, vj, t);
                        f.push_back(v);
                        b.push_back(v);
                    }
                }
                if (f.size() >= 3)
                    front.push_back(make_poly(f, poly.mat));
                if (b.size() >= 3)
                    back.push_back(make_poly(b, poly.mat));
            }
            }
        }

        struct Node
        {
            Plane plane;
            bool has_plane = false;
            std::unique_ptr<Node> front, back;
            std::vector<Poly> polys;

            void invert()
            {
                for (Poly &p : polys)
                    flip(p);
                if (has_plane)
                    plane.flip();
                if (front)
                    front->invert();
                if (back)
                    back->invert();
                std::swap(front, back);
            }

            std::vector<Poly> clip_polygons(const std::vector<Poly> &in) const
            {
                if (!has_plane)
                    return in;
                std::vector<Poly> f, b;
                for (const Poly &p : in)
                    split_polygon(plane, p, f, b, f, b);
                if (front)
                    f = front->clip_polygons(f);
                b = back ? back->clip_polygons(b) : std::vector<Poly>();
                f.insert(f.end(), b.begin(), b.end());
                return f;
            }

            void clip_to(const Node &other)
            {
                polys = other.clip_polygons(polys);
                if (front)
                    front->clip_to(other);
                if (back)
                    back->clip_to(other);
            }

            void all_polygons(std::vector<Poly> &out) const
            {
                out.insert(out.end(), polys.begin(), polys.end());
                if (front)
                    front->all_polygons(out);
                if (back)
                    back->all_polygons(out);
            }

            void build(const std::vector<Poly> &in)
            {
                if (in.empty())
                    return;
                if (!has_plane)
                {
                    plane = in[0].plane;
                    has_plane = true;
                }
                std::vector<Poly> f, b;
                for (const Poly &p : in)
                    split_polygon(plane, p, polys, polys, f, b);
                if (!f.empty())
                {
                    if (!front)
                        front = std::make_unique<Node>();
                    front->build(f);
                }
                if (!b.empty())
                {
                    if (!back)
                        back = std::make_unique<Node>();
                    back->build(b);
                }
            }
        };

        std::vector<Poly> to_polys(const Mesh &m)
        {
            std::vector<Poly> out;
            for (size_t fi = 0; fi < m.f.size(); ++fi)
                for (const Tri &t : triangulate_face(m, static_cast<int>(fi)))
                {
                    const V3 &a = m.v[t[0]], &b = m.v[t[1]], &c = m.v[t[2]];
                    if (length(cross(b - a, c - a)) < 1e-14)
                        continue;
                    out.push_back(make_poly({a, b, c}, m.f[fi].mat));
                }
            return out;
        }

        Mesh from_polys(const std::vector<Poly> &polys)
        {
            Mesh m;
            for (const Poly &p : polys)
            {
                if (p.p.size() < 3)
                    continue;
                Face face;
                face.mat = p.mat;
                for (const V3 &v : p.p)
                {
                    face.v.push_back(static_cast<int>(m.v.size()));
                    m.v.push_back(v);
                }
                m.f.push_back(std::move(face));
            }
            weld(m, 1e-6);
            return m;
        }
    }

    Mesh csg_union(const Mesh &am, const Mesh &bm)
    {
        Node a, b;
        a.build(to_polys(am));
        b.build(to_polys(bm));
        a.clip_to(b);
        b.clip_to(a);
        b.invert();
        b.clip_to(a);
        b.invert();
        std::vector<Poly> rest;
        b.all_polygons(rest);
        a.build(rest);
        std::vector<Poly> out;
        a.all_polygons(out);
        return from_polys(out);
    }

    Mesh csg_subtract(const Mesh &am, const Mesh &bm)
    {
        Node a, b;
        a.build(to_polys(am));
        b.build(to_polys(bm));
        a.invert();
        a.clip_to(b);
        b.clip_to(a);
        b.invert();
        b.clip_to(a);
        b.invert();
        std::vector<Poly> rest;
        b.all_polygons(rest);
        a.build(rest);
        a.invert();
        std::vector<Poly> out;
        a.all_polygons(out);
        return from_polys(out);
    }

    Mesh csg_intersect(const Mesh &am, const Mesh &bm)
    {
        Node a, b;
        a.build(to_polys(am));
        b.build(to_polys(bm));
        a.invert();
        b.clip_to(a);
        b.invert();
        a.clip_to(b);
        b.clip_to(a);
        std::vector<Poly> rest;
        b.all_polygons(rest);
        a.build(rest);
        a.invert();
        std::vector<Poly> out;
        a.all_polygons(out);
        return from_polys(out);
    }
}
