#!/usr/bin/env python3
"""Check Salyut's animated mesh frames and cells fold together."""

import json
import math
from pathlib import Path

from ares_legs_test import transformed

ROOT = Path(__file__).resolve().parents[1]
ADDON = ROOT / "addons/salyut"


def main():
    spec = json.loads((ADDON / "ship.json").read_text())
    part = spec["stages"][0]["parts"][0]
    animations = json.loads((ADDON / part["animations"]).read_text())["animations"]
    wings = {a["objects"][0]: a for a in animations}
    assert set(wings) == {"SolarPositiveY", "SolarNegativeX", "SolarNegativeY"}
    vertices, faces, materials = [], {name: set() for name in wings}, {name: set() for name in wings}
    obj = material = ""
    for line in (ADDON / part["model"]).read_text().splitlines():
        words = line.split()
        if not words:
            continue
        if words[0] == "v":
            vertices.append(tuple(map(float, words[1:4])))
        elif words[0] == "o":
            obj = words[1]
        elif words[0] == "usemtl":
            material = words[1]
        elif words[0] == "f" and obj in wings:
            materials[obj].add(material)
            for word in words[1:]:
                index = int(word.split("/")[0])
                faces[obj].add(index - 1 if index > 0 else len(vertices) + index)

    for name, animation in wings.items():
        assert animation["property"] == "solar_panels"
        assert materials[name] == {"material04", "material07_salyutsolar"}, name
        points = [vertices[index] for index in faces[name]]
        assert len(points) > 1000, name
        deployed = max(math.hypot(*p[:2]) for p in points)
        folded = [transformed(p, animation, 0) for p in points]
        radii = [math.hypot(*p[:2]) for p in folded]
        assert deployed > 11 and 1.8 < min(radii) <= max(radii) < 5.1, (name, deployed, min(radii), max(radii))
        assert max(p[2] for p in folded) < 4.0, name
        assert all(math.dist(p, transformed(p, animation, 1)) < 1e-6 for p in points)
    print("salyut_panels_test ok")


if __name__ == "__main__":
    main()
