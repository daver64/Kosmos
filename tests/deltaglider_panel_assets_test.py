#!/usr/bin/env python3
"""Check both Deltaglider variants' 2D cockpit artwork."""

import json
import struct
from pathlib import Path

root = Path(__file__).resolve().parents[1]
for addon in ("cvel-deltaglider", "cvel-deltaglider-nuclear"):
    directory = root / "addons" / addon
    ship = json.loads((directory / "ship.json").read_text())
    cockpit = ship["cockpit"]
    panels = [cockpit["layout"], *cockpit["panels"]]
    assert len(panels) == 2, addon
    for panel in panels:
        image = directory / panel["image"]
        with image.open("rb") as stream:
            header = stream.read(24)
        assert header[:8] == b"\x89PNG\r\n\x1a\n" and header[12:16] == b"IHDR", image
        assert struct.unpack(">II", header[16:24]) == (panel["width"], panel["height"]), image

for script in ("dg_aoa.lua", "dg_slip.lua", "dg_slope.lua"):
    text = (root / "addons/cvel-deltaglider" / script).read_text()
    assert 'g:image("panel/panel1.png"' in text, script

print("deltaglider_panel_assets_test ok")
