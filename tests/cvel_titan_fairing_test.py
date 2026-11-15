#!/usr/bin/env python3
"""Verify the Titan III/IV fairing panels meet at one shared nose tip."""

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

print("cvel_titan_fairing_test ok")
