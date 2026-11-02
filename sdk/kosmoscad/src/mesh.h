// KosmosCAD polygon mesh: indexed vertices and n-gon faces with a material slot and optional per-corner UVs.
// Faces are counter-clockwise seen from outside. No GTK or cairo in here, so it is unit-tested headless.
#pragma once

#include "geom.h"

#include <array>
#include <set>
#include <utility>
#include <vector>

namespace cad
{
    struct Face
    {
        std::vector<int> v;  // vertex indices
        std::vector<int> uv; // empty, or one UV index per corner
        int mat = 0;         // material slot
    };

    struct Mesh
    {
        std::vector<V3> v;
        std::vector<V2> uv;
        std::vector<Face> f;

        bool empty() const { return f.empty(); }
        void append(const Mesh &o);
    };

    using Tri = std::array<int, 3>;
    using EdgeKey = std::pair<int, int>; // a < b

    V3 face_normal(const Mesh &m, int f);
    V3 face_centre(const Mesh &m, int f);
    EdgeKey edge_key(int a, int b);
    /** Every distinct edge, with the faces that use it. */
    std::vector<std::pair<EdgeKey, std::vector<int>>> edge_faces(const Mesh &m);
    /** Corner triples that cover face `f` (ear clipping in the face's own plane, so concave faces work). */
    std::vector<Tri> triangulate_face(const Mesh &m, int f);
    void bounds(const Mesh &m, V3 &lo, V3 &hi);
    /** Signed volume of a closed mesh: positive when the faces point outward. */
    double signed_volume(const Mesh &m);
    void apply_xform(Mesh &m, const Xform &x);

    // Editing. Each returns the faces it made so the caller can select them.
    std::vector<int> extrude_faces(Mesh &m, const std::set<int> &faces, double distance);
    std::vector<int> inset_faces(Mesh &m, const std::set<int> &faces, double amount);
    void delete_faces(Mesh &m, const std::set<int> &faces);
    /** Splits edge a-b at its middle in every face that has it; returns the new vertex, or -1. */
    int split_edge(Mesh &m, int a, int b);
    void flip_faces(Mesh &m, const std::set<int> &faces);
    /** Merges vertices closer than `eps` and drops faces that collapse. */
    void weld(Mesh &m, double eps);
    void remove_unused_vertices(Mesh &m);
    /** Adds a reflected copy across the plane through the origin normal to `axis` (0 X, 1 Y, 2 Z), welding the seam. */
    void mirror(Mesh &m, int axis, bool weld_seam = true);
    /** Moves every vertex of the given set. */
    void move_vertices(Mesh &m, const std::set<int> &verts, const V3 &delta);

    enum class UvMode { Planar, Box, Cylindrical, Spherical };
    /** Generates UVs for the faces (all faces when `faces` is empty). Planar looks along `axis`. */
    void project_uv(Mesh &m, const std::set<int> &faces, UvMode mode, int axis);

    // Shapes. Material 0, no UVs.
    Mesh make_box(double sx, double sy, double sz);
    Mesh make_cylinder(double radius, double height, int segments, bool caps = true);
    Mesh make_cone(double radius_bottom, double radius_top, double height, int segments);
    Mesh make_sphere(double radius, int segments, int rings);
    Mesh make_torus(double major, double minor, int segments, int ring_segments);
    Mesh make_plane(double width, double depth, int div_x = 1, int div_z = 1);
    /** Revolves a profile of (radius, y) points about the Y axis. Caps are added when a profile end is off the axis. */
    Mesh make_lathe(const std::vector<V2> &profile, int segments, bool cap_ends = true);
    /** Extrudes a closed polygon of (x, y) points along Z by `depth`, centred on Z = 0. */
    Mesh make_extrusion(const std::vector<V2> &outline, double depth);
    /** Lofts through closed outlines (same point count each) placed at the given Z values. */
    Mesh make_loft(const std::vector<std::vector<V2>> &outlines, const std::vector<double> &z, bool cap_ends = true);
}
