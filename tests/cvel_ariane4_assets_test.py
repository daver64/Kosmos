#!/usr/bin/env python3
"""Check Ariane's fairing normals against the rendered face winding."""

import math
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def subtract(a, b):
    return tuple(x - y for x, y in zip(a, b))


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


for name in ("fairing_1.obj", "fairing_2.obj"):
    path = ROOT / "addons/cvel-ariane4/models" / name
    vertices, normals = [], []
    faces = 0
    for line in path.read_text().splitlines():
        fields = line.split()
        if not fields:
            continue
        if fields[0] == "v":
            vertices.append(tuple(map(float, fields[1:4])))
        elif fields[0] == "vn":
            normals.append(tuple(map(float, fields[1:4])))
        elif fields[0] == "f":
            assert len(fields) == 4, (name, line)
            corners = [field.split("/") for field in fields[1:]]
            points = [vertices[int(corner[0]) - 1] for corner in corners]
            face_normal = cross(subtract(points[1], points[0]), subtract(points[2], points[0]))
            face_length = math.sqrt(sum(x * x for x in face_normal))
            assert face_length > 0, (name, line)
            for corner in corners:
                normal = normals[int(corner[2]) - 1]
                normal_length = math.sqrt(sum(x * x for x in normal))
                alignment = sum(x * y for x, y in zip(face_normal, normal))
                assert normal_length > 0 and alignment > 0.9 * face_length * normal_length, (name, line)
            faces += 1
    assert faces, name
print("Ariane fairing normals match their faces")

# The upper-stage nose ring is double-sided, but its forward cap must only
# have one triangulation and the ring's winding must match its normals.
vertices, normals = [], []
group = ""
cap_faces = 0
for line in (ROOT / "addons/cvel-ariane4/models/stage3.obj").read_text().splitlines():
    fields = line.split()
    if not fields:
        continue
    if fields[0] == "o":
        group = fields[1]
    elif fields[0] == "v":
        vertices.append(tuple(map(float, fields[1:4])))
    elif fields[0] == "vn":
        normals.append(tuple(map(float, fields[1:4])))
    elif fields[0] == "f" and group in ("group3", "group4"):
        corners = [field.split("/") for field in fields[1:]]
        points = [vertices[int(corner[0]) - 1] for corner in corners]
        face_normal = cross(subtract(points[1], points[0]), subtract(points[2], points[0]))
        for corner in corners:
            normal = normals[int(corner[2]) - 1]
            assert sum(x * y for x, y in zip(face_normal, normal)) > 0, (group, line)
        if all(abs(point[2] + 3.88871) < 0.00002 for point in points):
            assert group == "group4" and face_normal[2] < 0, line
            cap_faces += 1
assert cap_faces == 22
print("Ariane third-stage nose cap and normals are consistent")
