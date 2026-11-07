#!/usr/bin/env python3
"""Check Ares' landing contacts and, when present, animated mesh feet."""

import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ADDON = ROOT / "addons/cvel-ares"


def transformed(point, animation, position):
    a, b = animation["interpolation"]
    value = a[1] + (b[1] - a[1]) * (position - a[0]) / (b[0] - a[0])
    axis = animation["axis"]
    if animation["type"] == "translate":
        return tuple(point[i] + axis[i] * value for i in range(3))
    angle = math.radians(value)
    center = animation["center"]
    x, y, z = (point[i] - center[i] for i in range(3))
    c, s = math.cos(angle), math.sin(angle)
    dot = axis[0] * x + axis[1] * y + axis[2] * z
    cross = (axis[1] * z - axis[2] * y, axis[2] * x - axis[0] * z, axis[0] * y - axis[1] * x)
    return tuple(center[i] + (x, y, z)[i] * c + cross[i] * s + axis[i] * dot * (1 - c) for i in range(3))


def main():
    spec = json.loads((ADDON / "ship.json").read_text())
    stage = spec["stages"][0]
    control = next(c for c in spec["animation_controls"] if c["property"] == "legs")
    assert control["initial"] == 0
    part = stage["parts"][0]
    if part.get("prop") == "stage":
        assert len(stage["parts"]) == 1 and "model" not in part
        contacts = stage["gear"]
        assert len(contacts) == 4
        assert all(len(p) == 3 and p[2] == contacts[0][2] and math.hypot(*p[:2]) > part["radius"]
                   for p in contacts)
        assert all(any(math.dist((-p[0], -p[1], p[2]), q) < 1e-6 for q in contacts) for p in contacts)
        print("ares_legs_test ok (procedural placeholder; no animated feet)")
        return
    animations = json.loads((ADDON / stage["parts"][0]["animations"]).read_text())["animations"]
    vertices, indices = [], {f"Leg{i}Strut": set() for i in range(1, 5)}
    current = ""
    with (ADDON / stage["parts"][0]["model"]).open() as obj:
        for line in obj:
            parts = line.split()
            if not parts:
                continue
            if parts[0] == "v":
                vertices.append(tuple(map(float, parts[1:4])))
            elif parts[0] == "o":
                current = parts[1]
            elif parts[0] == "f" and current in indices:
                for part in parts[1:]:
                    index = int(part.split("/")[0])
                    indices[current].add(index - 1 if index > 0 else len(vertices) + index)

    for i, contact in enumerate(stage["gear"], 1):
        name = f"Leg{i}Strut"
        foot = [vertices[n] for n in indices[name] if vertices[n][2] > 5.0]
        assert foot, f"{name} has no foot geometry"
        center = tuple(sum(p[j] for p in foot) / len(foot) for j in range(3))
        relevant = [a for a in animations if a["property"] == "legs" and name in a["objects"]]
        assert len(relevant) == 2, name
        poses = []
        for position in (0, 1):
            point = center
            for animation in relevant:
                point = transformed(point, animation, position)
            poses.append(point)
        deployed, stowed = poses
        assert math.hypot(*deployed[:2]) > math.hypot(*stowed[:2]) + 2.0, name
        assert math.dist(deployed, contact) < 0.2, f"{name}: foot {deployed} contact {contact}"
    print("ares_legs_test ok")


if __name__ == "__main__":
    main()
