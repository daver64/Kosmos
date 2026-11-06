#!/usr/bin/env python3
"""Check Proton's upper-stage plume origins against the nozzle meshes."""

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ADDON = ROOT / "addons/cvel-proton"


def mesh_groups(path):
    groups = []
    vertices = []
    material = ""
    for line in path.read_text().splitlines() + ["g end"]:
        if line.startswith("g "):
            if vertices:
                groups.append((material, vertices))
            vertices = []
        elif line.startswith("usemtl "):
            material = line.split(maxsplit=1)[1]
        elif line.startswith("v "):
            vertices.append(tuple(map(float, line.split()[1:4])))
    return groups


def centre_and_end(vertices, offset):
    bounds = [(min(v[i] for v in vertices), max(v[i] for v in vertices)) for i in range(3)]
    return ((bounds[0][0] + bounds[0][1]) / 2 + offset[0],
            (bounds[1][0] + bounds[1][1]) / 2 + offset[1],
            bounds[2][1] + offset[2])


def main():
    stages = json.loads((ADDON / "ship.json").read_text())["stages"]
    second, third = stages[1], stages[4]
    nozzles = [centre_and_end(vertices, second["parts"][0]["offset"])
               for material, vertices in mesh_groups(ADDON / "models/protkstg2_uw.obj")
               if material == "EngineInside"]
    engines = second["engines"]
    assert len(nozzles) == len(engines) == 4
    for nozzle in nozzles:
        assert any(all(abs(a - b) < 0.1 for a, b in zip(engine["position"], nozzle))
                   for engine in engines), nozzle
    assert all(abs(sum(e["position"][axis] * e["thrust"] for e in engines)) < 1e-6 for axis in (0, 1))
    assert sum(e["thrust"] for e in engines) == 2400000
    assert all(e["isp"] == 327 and e["plume"] <= 7 and e["plume_width"] <= 0.35 for e in engines)

    third_groups = mesh_groups(ADDON / "models/protonstg3.obj")
    nozzle = centre_and_end(third_groups[0][1], third["parts"][0]["offset"])
    engine = third["engines"][0]
    assert all(abs(a - b) < 0.1 for a, b in zip(engine["position"], nozzle))
    assert engine["thrust"] == 590390 and engine["isp"] == 325
    assert engine["plume"] <= 5 and engine["plume_width"] <= 0.35
    print("proton_exhaust_test ok")


if __name__ == "__main__":
    main()
