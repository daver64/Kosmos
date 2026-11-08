#!/usr/bin/env python3
"""Check bundled base control panels are reachable at standing height."""

import json
import math
from pathlib import Path

root = Path(__file__).resolve().parents[1]
checked = 0
for addon in ("gale-base", "outpost-base"):
    for path in sorted((root / "addons" / addon).glob("*.json")):
        spec = json.loads(path.read_text())
        panels = spec.get("interior", {}).get("panels", [])
        if not panels:
            continue
        layout = spec["cockpit"]["layout"]
        for panel in panels:
            height = panel["width"] * layout["height"] / layout["width"]
            centre = panel["pos"][1]
            bottom, top = centre - height / 2, centre + height / 2
            assert 1.0 <= bottom < top <= 1.9, (path, bottom, top)
            # At 1.5 m from the wall the standing walker can face and reach the panel.
            vertical = centre - 1.7
            distance = math.hypot(1.5, vertical)
            assert distance <= 2.5 and 1.5 / distance > 0.8, (path, distance)
            checked += 1

assert checked > 0, "no bundled base panels found"
print(f"base_panel_height_test ok ({checked} panels)")
