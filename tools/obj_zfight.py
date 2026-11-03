#!/usr/bin/env python3
"""Find and remove z-fighting geometry in OBJ models.

Ship parts are drawn two-sided, so any two triangles lying (almost) in the same plane flicker against each other,
whichever way they face. Two cases are handled; neither is fixed by depth-buffer precision:

  * coincident: a triangle lying in the plane of another one and inside its outline (a second hull in another
    material, a strip repeated, or the reversed twin of a face). The one that comes later is dropped.
  * close: a small triangle a hair above or below a much larger one (rivets, decals, bands tessellated finer than
    the hull). With `--lift` it is raised that far (metres) clear of the larger one, along its outward direction.

Usage: obj_zfight.py MODEL.obj [MODEL.obj ...] [--dry-run] [--plane-tol 0.0002] [--close-tol 0.01] [--lift 0.01]

Outward is the side the vertex normals point to. Models are edited in place; --dry-run only reports.
"""
import argparse
import collections
import sys

import numpy as np


def parse(path):
    lines = open(path).read().split("\n")
    verts, norms, faces, mats = [], [], [], []
    material = None
    for number, line in enumerate(lines):
        t = line.split()
        if not t:
            continue
        if t[0] == "v":
            verts.append([float(x) for x in t[1:4]])
        elif t[0] == "vn":
            norms.append([float(x) for x in t[1:4]])
        elif t[0] == "usemtl":
            material = t[1]
        elif t[0] == "f" and len(t) == 4:
            ids = []
            for w in t[1:]:
                p = w.split("/")
                ids.append((int(p[0]) - 1, int(p[2]) - 1 if len(p) > 2 and p[2] else -1))
            faces.append((number, ids, material))
    return lines, np.array(verts), np.array(norms), faces


def triangle_data(verts, norms, faces):
    tri = np.array([[i[0] for i in f[1]] for f in faces])
    a, b, c = verts[tri[:, 0]], verts[tri[:, 1]], verts[tri[:, 2]]
    cross = np.cross(b - a, c - a)
    area2 = np.linalg.norm(cross, axis=1)
    geo = cross / np.maximum(area2, 1e-30)[:, None]
    front = geo.copy()
    if len(norms):
        for k, f in enumerate(faces):
            ids = [i[1] for i in f[1]]
            if min(ids) >= 0:
                n = norms[ids].sum(axis=0)
                if np.linalg.norm(n) > 1e-9 and geo[k] @ n < 0:
                    front[k] = -geo[k]
    return a, b, c, geo, front, area2 / 2


def grid_of(a, b, c, cell):
    lo = np.minimum(np.minimum(a, b), c)
    hi = np.maximum(np.maximum(a, b), c)
    grid = collections.defaultdict(list)
    for k in range(len(a)):
        l = np.floor(lo[k] / cell).astype(int)
        h = np.floor(hi[k] / cell).astype(int)
        for x in range(l[0], h[0] + 1):
            for y in range(l[1], h[1] + 1):
                for z in range(l[2], h[2] + 1):
                    grid[(x, y, z)].append(k)
    return grid, lo, hi


def inside(p, a, b, c, n, eps=1e-4):
    """Barycentric test of the points p (N,3) against triangle abc."""
    v0, v1 = b - a, c - a
    d00, d01, d11 = v0 @ v0, v0 @ v1, v1 @ v1
    den = d00 * d11 - d01 * d01
    if abs(den) < 1e-18:
        return np.zeros(len(p), bool)
    w = p - a
    d20, d21 = w @ v0, w @ v1
    s = (d11 * d20 - d01 * d21) / den
    t = (d00 * d21 - d01 * d20) / den
    return (s >= -eps) & (t >= -eps) & (s + t <= 1 + eps)


def analyse(path, plane_tol, close_tol, lift):
    lines, verts, norms, faces = parse(path)
    if not faces:
        return None
    a, b, c, geo, front, area = triangle_data(verts, norms, faces)
    cell = 1.0
    grid, lo, hi = grid_of(a, b, c, cell)
    first_use = {}
    for f in faces:
        first_use.setdefault(f[2], len(first_use))
    order = np.array([first_use[f[2]] for f in faces])
    drop = set()
    lifted = {}
    for k in range(len(faces)):
        if area[k] < 1e-10:
            continue
        l = np.floor(lo[k] / cell).astype(int)
        h = np.floor(hi[k] / cell).astype(int)
        near = set()
        for x in range(l[0], h[0] + 1):
            for y in range(l[1], h[1] + 1):
                for z in range(l[2], h[2] + 1):
                    near.update(grid.get((x, y, z), ()))
        near.discard(k)
        near = [u for u in near if area[u] > 1e-10 and abs(geo[k] @ geo[u]) > 0.999]
        if not near:
            continue
        m = (a[k] + b[k] + c[k]) / 3
        pts = np.array([m, (a[k] + b[k]) / 2, (b[k] + c[k]) / 2, (a[k] + c[k]) / 2,
                        m + 0.8 * (a[k] - m), m + 0.8 * (b[k] - m), m + 0.8 * (c[k] - m)])
        # for each sample: the best (smallest |d|) candidates within tolerance, with that candidate's priority and area
        covered_same = np.zeros(len(pts), bool)   # covered, within plane_tol, by a higher-priority triangle
        covered_near = np.zeros(len(pts), bool)   # covered, within close_tol, by a larger triangle
        dmin = np.full(len(pts), np.inf)
        biggest = 0.0
        for u in near:
            d = (pts - a[u]) @ (geo[u] * np.sign(geo[u] @ front[k] or 1.0))
            ok = inside(pts, a[u], b[u], c[u], geo[u])
            if not ok.any():
                continue
            wins = (order[u], -area[u], u) < (order[k], -area[k], k)
            covered_same |= ok & (np.abs(d) < plane_tol) & wins
            big = ok & (np.abs(d) < close_tol) & (area[u] > area[k])
            covered_near |= big
            dmin = np.where(big, np.minimum(dmin, d), dmin)
            if big.any():
                biggest = max(biggest, area[u])
        if covered_same.all() and not (biggest > 20 * area[k]):
            drop.add(k)
        elif lift and covered_near.all():
            amount = lift - float(dmin.min())
            if amount > 0:
                lifted[k] = amount
    return lines, verts, norms, faces, a, b, c, front, drop, lifted


def report_materials(faces, indexes):
    return dict(collections.Counter(faces[k][2] for k in indexes))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("models", nargs="+")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--plane-tol", type=float, default=0.0002)
    ap.add_argument("--close-tol", type=float, default=0.01)
    ap.add_argument("--lift", type=float, default=0.0)
    args = ap.parse_args()
    for path in args.models:
        result = analyse(path, args.plane_tol, args.close_tol, args.lift)
        if result is None:
            continue
        lines, verts, norms, faces, a, b, c, front, drop, lifted = result
        print("%s: %d triangles, %d coplanar duplicates, %d lifted %s" % (path, len(faces), len(drop), len(lifted), report_materials(faces, drop)))
        if args.dry_run or not (drop or lifted):
            continue
        removed = {faces[k][0] for k in drop}
        lifted_at = {faces[k][0]: k for k in lifted if k not in drop}
        # A lifted triangle gets private copies of its corners, moved along its front normal. The copies are written
        # just before its face so every index it uses is already defined; later vertex numbers shift to match.
        final, new_index, seen, added = [], {}, 0, 0
        for number, line in enumerate(lines):
            if number in removed:
                continue
            t = line.split()
            if t and t[0] == "v":
                seen += 1
                new_index[seen] = seen + added
            elif t and t[0] == "f" and len(t) == 4:
                refs = []
                for w in t[1:]:
                    p = w.split("/")
                    p[0] = str(new_index[int(p[0])])
                    refs.append(p)
                if number in lifted_at:
                    k = lifted_at[number]
                    for n, corner in enumerate((a[k], b[k], c[k])):
                        final.append("v %.6f %.6f %.6f" % tuple(corner + front[k] * lifted[k]))
                        refs[n][0] = str(seen + added + 1)
                        added += 1
                line = "f " + " ".join("/".join(p) for p in refs)
            final.append(line)
        open(path, "w").write("\n".join(final))


if __name__ == "__main__":
    sys.exit(main())
