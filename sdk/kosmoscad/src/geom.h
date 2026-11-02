// KosmosCAD basic vector and transform types. Ship frame: X right, Y up, Z back (as in Kosmos), metres.
#pragma once

#include <algorithm>
#include <cmath>

namespace cad
{
    constexpr double kPi = 3.14159265358979323846;

    struct V3
    {
        double x = 0, y = 0, z = 0;
        V3() = default;
        V3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
        V3 operator+(const V3 &o) const { return {x + o.x, y + o.y, z + o.z}; }
        V3 operator-(const V3 &o) const { return {x - o.x, y - o.y, z - o.z}; }
        V3 operator*(double s) const { return {x * s, y * s, z * s}; }
        V3 operator/(double s) const { return {x / s, y / s, z / s}; }
        V3 operator-() const { return {-x, -y, -z}; }
        V3 &operator+=(const V3 &o) { x += o.x; y += o.y; z += o.z; return *this; }
        V3 &operator-=(const V3 &o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
        double &operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
        double operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
    };
    inline double dot(const V3 &a, const V3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    inline V3 cross(const V3 &a, const V3 &b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
    inline double length(const V3 &a) { return std::sqrt(dot(a, a)); }
    inline V3 normalized(const V3 &a)
    {
        const double l = length(a);
        return l > 1e-12 ? a / l : V3(0, 0, 0);
    }
    inline V3 lerp(const V3 &a, const V3 &b, double t) { return a + (b - a) * t; }

    struct V2
    {
        double u = 0, v = 0;
        V2() = default;
        V2(double u_, double v_) : u(u_), v(v_) {}
    };

    /** Position, Euler rotation in degrees (applied X, then Y, then Z) and per-axis scale. */
    struct Xform
    {
        V3 pos, rot, scale{1, 1, 1};

        V3 apply_dir(const V3 &d) const
        {
            const double rx = rot.x * kPi / 180, ry = rot.y * kPi / 180, rz = rot.z * kPi / 180;
            const double cx = std::cos(rx), sx = std::sin(rx), cy = std::cos(ry), sy = std::sin(ry), cz = std::cos(rz), sz = std::sin(rz);
            V3 p = d;
            p = {p.x, p.y * cx - p.z * sx, p.y * sx + p.z * cx};
            p = {p.x * cy + p.z * sy, p.y, -p.x * sy + p.z * cy};
            p = {p.x * cz - p.y * sz, p.x * sz + p.y * cz, p.z};
            return p;
        }
        V3 apply(const V3 &p) const { return apply_dir({p.x * scale.x, p.y * scale.y, p.z * scale.z}) + pos; }
        V3 inverse_dir(const V3 &d) const
        {
            // The rotation is orthonormal, so rotate by the transpose: undo Z, then Y, then X.
            const double rx = rot.x * kPi / 180, ry = rot.y * kPi / 180, rz = rot.z * kPi / 180;
            const double cx = std::cos(rx), sx = std::sin(rx), cy = std::cos(ry), sy = std::sin(ry), cz = std::cos(rz), sz = std::sin(rz);
            V3 p = d;
            p = {p.x * cz + p.y * sz, -p.x * sz + p.y * cz, p.z};
            p = {p.x * cy - p.z * sy, p.y, p.x * sy + p.z * cy};
            p = {p.x, p.y * cx + p.z * sx, -p.y * sx + p.z * cx};
            return p;
        }
        V3 inverse(const V3 &w) const
        {
            const V3 l = inverse_dir(w - pos);
            return {scale.x != 0 ? l.x / scale.x : 0, scale.y != 0 ? l.y / scale.y : 0, scale.z != 0 ? l.z / scale.z : 0};
        }
    };

    /** Row-major 3x3 rotation matrix. */
    struct M3
    {
        double m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        V3 operator*(const V3 &p) const
        {
            return {m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z, m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z,
                    m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z};
        }
        M3 operator*(const M3 &o) const
        {
            M3 r;
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    r.m[i][j] = m[i][0] * o.m[0][j] + m[i][1] * o.m[1][j] + m[i][2] * o.m[2][j];
            return r;
        }
    };

    /** The matrix Xform::apply_dir uses: X, then Y, then Z (so Rz * Ry * Rx). */
    inline M3 euler_matrix(const V3 &deg)
    {
        M3 r;
        const Xform x{{}, deg, {1, 1, 1}};
        const V3 cx = x.apply_dir({1, 0, 0}), cy = x.apply_dir({0, 1, 0}), cz = x.apply_dir({0, 0, 1});
        r.m[0][0] = cx.x; r.m[1][0] = cx.y; r.m[2][0] = cx.z;
        r.m[0][1] = cy.x; r.m[1][1] = cy.y; r.m[2][1] = cy.z;
        r.m[0][2] = cz.x; r.m[1][2] = cz.y; r.m[2][2] = cz.z;
        return r;
    }

    /** Inverse of euler_matrix (degrees). */
    inline V3 matrix_euler(const M3 &r)
    {
        // R = Rz Ry Rx: r[2][0] = -sin(y)
        const double sy = std::clamp(-r.m[2][0], -1.0, 1.0);
        double x, y = std::asin(sy), z;
        if (std::fabs(sy) < 0.999999)
        {
            x = std::atan2(r.m[2][1], r.m[2][2]);
            z = std::atan2(r.m[1][0], r.m[0][0]);
        }
        else
        {
            x = std::atan2(-r.m[1][2], r.m[1][1]);
            z = 0;
        }
        return {x * 180 / kPi, y * 180 / kPi, z * 180 / kPi};
    }

    /** Rotation by `angle` radians about a unit axis. */
    inline M3 axis_angle(const V3 &axis, double angle)
    {
        const V3 a = normalized(axis);
        const double c = std::cos(angle), s = std::sin(angle), t = 1 - c;
        M3 r;
        r.m[0][0] = t * a.x * a.x + c;       r.m[0][1] = t * a.x * a.y - s * a.z; r.m[0][2] = t * a.x * a.z + s * a.y;
        r.m[1][0] = t * a.x * a.y + s * a.z; r.m[1][1] = t * a.y * a.y + c;       r.m[1][2] = t * a.y * a.z - s * a.x;
        r.m[2][0] = t * a.x * a.z - s * a.y; r.m[2][1] = t * a.y * a.z + s * a.x; r.m[2][2] = t * a.z * a.z + c;
        return r;
    }
}
