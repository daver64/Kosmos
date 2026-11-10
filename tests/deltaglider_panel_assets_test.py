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
    strobe = [h for h in cockpit["vc_hotspots"] if h["control"].get("property") == "/controls/lighting/strobe"]
    assert len(strobe) == 1 and strobe[0]["control"]["type"] == "toggle", addon
    strobes = [light for light in ship["lights"] if light.get("pattern") == "strobe"]
    assert strobes and all(light.get("control") == "/controls/lighting/strobe" and light.get("always") for light in strobes), addon
    assert any(light.get("outer", 0) > 0 and light.get("always") for light in ship["lights"]), addon
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

vc = root / "addons/cvel-deltaglider/models"
material = (vc / "deltaglider_vc.mtl").read_text().splitlines()
for texture in ("dgmk4_2", "dgmk4_3", "dgmk4_4", "dgip_01", "dgip_03", "dgip_04", "dgip_05", "dg_vc4", "noise1"):
    path = f"textures/orbiter-vc/{texture}.png"
    assert f"map_Kd {path}" in material, texture
    assert (vc / path).is_file(), path
shadow = material.index("newmtl black_DG_dg_vc5")
assert "Kd 0.000 0.000 0.000" in material[shadow:shadow + 6]
assert "map_Kd textures/dg_vc5.png" in material[shadow:shadow + 6]
print("deltaglider_panel_assets_test ok")
