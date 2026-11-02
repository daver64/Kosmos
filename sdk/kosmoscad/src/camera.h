// KosmosCAD camera: the four standard views. Ship frame: X right, Y up, Z back (the nose is at -Z).
//   Top    looks down -Y with the nose up the screen      Front  looks along +Z (screen right is -X)
//   Side   looks along -X from the right (nose to the right)   Persp  an orbit camera
// Pure maths, shared by the GL drawing and by picking, so what you see is what you click.
#pragma once

#include "geom.h"

namespace cad
{
    struct Camera
    {
        enum Kind { Persp, Top, Front, Side };
        Kind kind = Persp;
        V3 target;
        double scale = 20;                  // orthographic: pixels per metre
        double yaw = 150, pitch = 25;       // perspective: degrees
        double dist = 40;                   // perspective: metres from the target
        double fov = 45;                    // degrees, vertical
        bool wire = false;
        bool textured = false;      // solid shading with each material's texture (or the checker) on

        bool ortho() const { return kind != Persp; }

        void basis(V3 &right, V3 &up, V3 &fwd) const
        {
            switch (kind)
            {
            case Top: right = {1, 0, 0}; up = {0, 0, -1}; fwd = {0, -1, 0}; return;
            case Front: right = {-1, 0, 0}; up = {0, 1, 0}; fwd = {0, 0, 1}; return;
            case Side: right = {0, 0, -1}; up = {0, 1, 0}; fwd = {-1, 0, 0}; return;
            default: break;
            }
            const double y = yaw * kPi / 180, p = pitch * kPi / 180;
            fwd = -V3(std::sin(y) * std::cos(p), std::sin(p), std::cos(y) * std::cos(p));
            right = normalized(cross(fwd, V3(0, 1, 0)));
            up = cross(right, fwd);
        }

        V3 eye() const
        {
            V3 r, u, f;
            basis(r, u, f);
            return target - f * (ortho() ? 1000.0 : dist);
        }

        double focal(int h) const { return (h / 2.0) / std::tan(fov * kPi / 360); }

        /** Pixel position inside a w x h viewport (y down) and the distance along the view direction. False if behind the eye. */
        bool project(const V3 &p, int w, int h, double &sx, double &sy, double &depth) const
        {
            V3 r, u, f;
            basis(r, u, f);
            if (ortho())
            {
                const V3 q = p - target;
                sx = w / 2.0 + dot(q, r) * scale;
                sy = h / 2.0 - dot(q, u) * scale;
                depth = dot(q, f);
                return true;
            }
            const V3 q = p - eye();
            depth = dot(q, f);
            if (depth < 0.01)
                return false;
            const double k = focal(h) / depth;
            sx = w / 2.0 + dot(q, r) * k;
            sy = h / 2.0 - dot(q, u) * k;
            return true;
        }

        void ray(double px, double py, int w, int h, V3 &origin, V3 &dir) const
        {
            V3 r, u, f;
            basis(r, u, f);
            if (ortho())
            {
                origin = target + r * ((px - w / 2.0) / scale) + u * ((h / 2.0 - py) / scale) - f * 1000.0;
                dir = f;
                return;
            }
            origin = eye();
            dir = normalized(f + r * ((px - w / 2.0) / focal(h)) + u * ((h / 2.0 - py) / focal(h)));
        }

        /** Metres covered by one pixel at point p (for screen-constant gizmos and picking tolerances). */
        double metres_per_pixel(const V3 &p, int h) const
        {
            if (ortho())
                return 1.0 / scale;
            V3 r, u, f;
            basis(r, u, f);
            return std::max(dot(p - eye(), f), 0.01) / focal(h);
        }

        void zoom(double factor)
        {
            if (ortho())
                scale = std::clamp(scale * factor, 0.05, 20000.0);
            else
                dist = std::clamp(dist / factor, 0.2, 20000.0);
        }

        /** Column-major model-view-projection for GL. */
        void mvp(int w, int h, float out[16]) const
        {
            V3 r, u, f;
            basis(r, u, f);
            const V3 o = ortho() ? target : eye();
            double m[16] = {0};
            // rows of the view matrix, then the projection folded in
            const double aspect = static_cast<double>(w) / std::max(h, 1);
            auto set_row = [&](int row, const V3 &a, double d) { m[0 + row] = a.x; m[4 + row] = a.y; m[8 + row] = a.z; m[12 + row] = d; };
            if (ortho())
            {
                const double R = 5000.0;
                set_row(0, r * (2 * scale / w), -dot(r, o) * 2 * scale / w);
                set_row(1, u * (2 * scale / h), -dot(u, o) * 2 * scale / h);
                set_row(2, f * (1 / R), -dot(f, o) / R);
                set_row(3, V3(), 1);
            }
            else
            {
                const double n = 0.1, fa = 20000.0, t = 1 / std::tan(fov * kPi / 360);
                set_row(0, r * (t / aspect), -dot(r, o) * t / aspect);
                set_row(1, u * t, -dot(u, o) * t);
                // z_clip = -(f+n)/(f-n) * z_view - 2fn/(f-n), with z_view = -depth
                const double A = (fa + n) / (fa - n), B = -2 * fa * n / (fa - n);
                set_row(2, f * A, -dot(f, o) * A + B);
                set_row(3, f, -dot(f, o));
            }
            for (int i = 0; i < 16; ++i)
                out[i] = static_cast<float>(m[i]);
        }

        void frame(const V3 &lo, const V3 &hi, int w, int h)
        {
            target = (lo + hi) * 0.5;
            const double radius = std::max(length(hi - lo) * 0.5, 0.5);
            if (ortho())
                scale = std::min(w, h) / (radius * 2.4);
            else
                dist = radius / std::sin(std::min(fov, 60.0) * kPi / 360) * 1.1;
        }
    };
}
