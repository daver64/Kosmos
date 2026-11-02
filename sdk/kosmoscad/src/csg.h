// Boolean operations on closed meshes (BSP trees, after csg.js). Both meshes must already be in the same space.
// UVs are not kept; every output polygon keeps the material of the polygon it came from.
#pragma once

#include "mesh.h"

namespace cad
{
    Mesh csg_union(const Mesh &a, const Mesh &b);
    Mesh csg_subtract(const Mesh &a, const Mesh &b);
    Mesh csg_intersect(const Mesh &a, const Mesh &b);
}
