#!/usr/bin/env python3
"""Writes the target sphere model for addons/target-sphere: a UV sphere of banded red and white, by material.

Usage: make_target_sphere.py OUT_DIR [RADIUS_M]
"""
import math
import os
import sys

out = sys.argv[1]
radius = float(sys.argv[2]) if len(sys.argv) > 2 else 40.0
rings, segments = 24, 48
os.makedirs(out, exist_ok=True)

with open(os.path.join(out, "target.mtl"), "w") as f:
    f.write("newmtl TargetRed\nKd 0.85 0.12 0.10\nKs 0.3 0.3 0.3\nKe 0.0 0.0 0.0\n\n")
    f.write("newmtl TargetWhite\nKd 0.92 0.92 0.92\nKs 0.3 0.3 0.3\nKe 0.0 0.0 0.0\n")

verts = []
for i in range(rings + 1):
    phi = math.pi * i / rings
    for j in range(segments):
        theta = 2.0 * math.pi * j / segments
        verts.append((radius * math.sin(phi) * math.cos(theta), radius * math.cos(phi), radius * math.sin(phi) * math.sin(theta)))

with open(os.path.join(out, "target.obj"), "w") as f:
    f.write("mtllib target.mtl\n")
    for v in verts:
        f.write("v %.4f %.4f %.4f\n" % v)
    for v in verts:
        n = math.sqrt(sum(c * c for c in v))
        f.write("vn %.5f %.5f %.5f\n" % (v[0] / n, v[1] / n, v[2] / n))
    for material in ("TargetRed", "TargetWhite"):
        f.write("g %s\nusemtl %s\n" % (material, material))
        for i in range(rings):
            # Alternating bands of latitude and longitude make a checkered ball, so its spin and distance read at a glance.
            for j in range(segments):
                if ((i // 3) + (j // 6)) % 2 != (0 if material == "TargetRed" else 1):
                    continue
                a = i * segments + j + 1
                b = i * segments + (j + 1) % segments + 1
                c = (i + 1) * segments + j + 1
                d = (i + 1) * segments + (j + 1) % segments + 1
                f.write("f %d//%d %d//%d %d//%d\n" % (a, a, c, c, b, b))
                f.write("f %d//%d %d//%d %d//%d\n" % (b, b, c, c, d, d))
