#!/usr/bin/env python3
"""Writes the Quantum Portal models (addons/cvel-portal/models): a torus ring with outward pylons, and the emitter nodes that glow.

The ring lies in the ship's XY plane, so its axis is the ship's Z axis (the nose is -Z). Run from anywhere; takes no arguments.
"""
import math
import os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "addons", "cvel-portal", "models")
MAJOR, MINOR = 200.0, 14.0
SEGMENTS, SIDES = 72, 16
NODES = 12


class Mesh:
    def __init__(self):
        self.v, self.vn, self.f = [], [], []

    def quad(self, a, b, c, d, normal):
        n = len(self.vn) + 1
        self.vn.append(normal)
        base = len(self.v)
        self.v.extend([a, b, c, d])
        self.f.append([(base + i + 1, n) for i in range(4)])

    def box(self, centre, half):
        cx, cy, cz = centre
        hx, hy, hz = half
        p = [(cx + sx * hx, cy + sy * hy, cz + sz * hz) for sx in (-1, 1) for sy in (-1, 1) for sz in (-1, 1)]

        def at(sx, sy, sz):
            return p[((sx + 1) // 2) * 4 + ((sy + 1) // 2) * 2 + (sz + 1) // 2]

        self.quad(at(1, -1, -1), at(1, 1, -1), at(1, 1, 1), at(1, -1, 1), (1, 0, 0))
        self.quad(at(-1, -1, 1), at(-1, 1, 1), at(-1, 1, -1), at(-1, -1, -1), (-1, 0, 0))
        self.quad(at(-1, 1, -1), at(-1, 1, 1), at(1, 1, 1), at(1, 1, -1), (0, 1, 0))
        self.quad(at(-1, -1, 1), at(-1, -1, -1), at(1, -1, -1), at(1, -1, 1), (0, -1, 0))
        self.quad(at(-1, -1, 1), at(1, -1, 1), at(1, 1, 1), at(-1, 1, 1), (0, 0, 1))
        self.quad(at(1, -1, -1), at(-1, -1, -1), at(-1, 1, -1), at(1, 1, -1), (0, 0, -1))

    def torus(self):
        grid = []
        for i in range(SEGMENTS):
            a = 2 * math.pi * i / SEGMENTS
            row = []
            for j in range(SIDES):
                b = 2 * math.pi * j / SIDES
                # Slightly squashed along the axis so the ring reads as a heavy girder, not a pipe.
                nx, ny, nz = math.cos(a) * math.cos(b), math.sin(a) * math.cos(b), math.sin(b)
                pos = ((MAJOR + MINOR * math.cos(b)) * math.cos(a), (MAJOR + MINOR * math.cos(b)) * math.sin(a), MINOR * 1.4 * math.sin(b))
                row.append((pos, (nx, ny, nz)))
            grid.append(row)
        for i in range(SEGMENTS):
            for j in range(SIDES):
                corners = [grid[i][j], grid[(i + 1) % SEGMENTS][j], grid[(i + 1) % SEGMENTS][(j + 1) % SIDES], grid[i][(j + 1) % SIDES]]
                base = len(self.v)
                self.v.extend(c[0] for c in corners)
                idx = []
                for k, c in enumerate(corners):
                    self.vn.append(c[1])
                    idx.append((base + k + 1, len(self.vn)))
                self.f.append(idx)

    def write(self, path, material, mtl_name, mtl_text):
        with open(path, "w") as out:
            out.write("mtllib %s\n" % mtl_name)
            out.write("o portal\nusemtl %s\n" % material)
            for v in self.v:
                out.write("v %.3f %.3f %.3f\n" % v)
            for n in self.vn:
                out.write("vn %.4f %.4f %.4f\n" % n)
            for face in self.f:
                out.write("f " + " ".join("%d//%d" % c for c in face) + "\n")
        with open(os.path.join(os.path.dirname(path), mtl_name), "w") as out:
            out.write(mtl_text)


def main():
    os.makedirs(OUT, exist_ok=True)
    ring = Mesh()
    ring.torus()
    for k in range(6):
        a = 2 * math.pi * (k + 0.5) / 6
        c, s = math.cos(a), math.sin(a)
        # An outward pylon, made of boxes along the radius, ending in a cross-piece.
        for r in (MAJOR + 40.0, MAJOR + 70.0):
            ring.box((c * r, s * r, 0.0), (14.0, 14.0, 10.0))
        ring.box((c * (MAJOR + 92.0), s * (MAJOR + 92.0), 0.0), (8.0, 8.0, 36.0))
    ring.write(os.path.join(OUT, "ring.obj"), "PortalHull", "ring.mtl",
               "newmtl PortalHull\nKd 0.560 0.580 0.620\nKs 0.450 0.450 0.500\nKe 0.180 0.190 0.220\n")

    nodes = Mesh()
    for k in range(NODES):
        a = 2 * math.pi * k / NODES
        c, s = math.cos(a), math.sin(a)
        nodes.box((c * (MAJOR - MINOR - 6.0), s * (MAJOR - MINOR - 6.0), 0.0), (7.0, 7.0, 11.0))
    nodes.write(os.path.join(OUT, "nodes.obj"), "PortalNode", "nodes.mtl",
                "newmtl PortalNode\nKd 0.300 0.600 1.000\nKs 0.000 0.000 0.000\nKe 0.300 0.600 1.000\n")


if __name__ == "__main__":
    main()
