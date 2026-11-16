#!/usr/bin/env python3
"""Verify Titan fairing panel tips and their attachment to the second stage."""

import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
models = root / "addons" / "cvel-titan" / "models"

for prefix, tip_z in (
    ("fairing102", -4.74829),
    ("fairing301", -5.36846),
    ("fairing402", -7.93161),
    ("fairing612", -15.72256),
    ("fairing613", -18.82256),
    ("fairing615", -24.92256),
):
    paths = sorted(models.glob(f"{prefix}*.obj"))
    assert paths, prefix
    for path in paths:
        apexes = set()
        for line in path.read_text().splitlines():
            fields = line.split()
            if len(fields) != 4 or fields[0] != "v":
                continue
            x, y, z = map(float, fields[1:4])
            if abs(z - tip_z) < 1e-5 and (x * x + y * y) ** 0.5 < 0.002:
                apexes.add((x, y, z))
        assert apexes == {(0.0, 0.0, tip_z)}, (path.name, apexes)

for filename in ("titan1.json", "titan2.json", "titan3d.json"):
    ship = json.loads((models.parent / filename).read_text())
    fairings = [stage for stage in ship["stages"] if stage["name"] == "Fairing segment"]
    stage = next(stage for stage in ship["stages"] if stage["name"] == "Stage 2")
    stage_part = stage["parts"][0]
    points = [tuple(map(float, line.split()[1:4]))
              for line in (models.parent / stage_part["model"]).read_text().splitlines()
              if line.startswith("v ")]
    assert len(fairings) == 2
    for fairing in fairings:
        part = fairing["parts"][0]
        skirt = [tuple(map(float, line.split()[1:4]))
                 for line in (models.parent / part["model"]).read_text().splitlines()
                 if line.startswith("v ")]
        bottom = max(p[2] for p in skirt)
        rim = [p for p in skirt if abs(p[2] - bottom) < 0.01]
        radius = max((p[0] ** 2 + p[1] ** 2) ** 0.5 for p in rim)
        top = min(p[2] for p in points if (p[0] ** 2 + p[1] ** 2) ** 0.5 >= radius * 0.9)
        gap = top + stage_part["offset"][2] - (bottom + part["offset"][2])
        assert abs(gap) < 0.02, (filename, part["model"], gap)
    assert fairings[0]["parts"][0]["offset"] == fairings[1]["parts"][0]["offset"]

print("cvel_titan_fairing_test ok")
