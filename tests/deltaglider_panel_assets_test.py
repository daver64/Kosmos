#!/usr/bin/env python3
"""Check Deltaglider model references and both variants' cockpit artwork."""

import json
import struct
from pathlib import Path

root = Path(__file__).resolve().parents[1]
for addon in ("cvel-deltaglider", "cvel-deltaglider-nuclear"):
    directory = root / "addons" / addon
    ship = json.loads((directory / "ship.json").read_text())
    cockpit = ship["cockpit"]
    assert cockpit["vc_repeat_materials"] == ["panels_generic_noise1"], addon
    assert cockpit["vc_cabin_lighting"] is True, addon
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
counts = [0, 0, 0]
for line_number, line in enumerate((vc / "deltaglider.obj").open(), 1):
    fields = line.split()
    if not fields:
        continue
    if fields[0] in ("v", "vt", "vn"):
        counts[("v", "vt", "vn").index(fields[0])] += 1
    elif fields[0] == "f":
        for corner in fields[1:]:
            for index, value in enumerate(corner.split("/")):
                if value:
                    ref = int(value)
                    assert 0 < ref <= counts[index] or -counts[index] <= ref < 0, (line_number, corner)

material = (vc / "deltaglider_vc.mtl").read_text().splitlines()
for texture in ("dgmk4_2", "dgmk4_3", "dgmk4_4", "dgip_01", "dgip_03", "dgip_04", "dgip_05", "dg_vc4", "noise1"):
    path = f"textures/orbiter-vc/{texture}.png"
    assert f"map_Kd {path}" in material, texture
    assert (vc / path).is_file(), path
shadow = material.index("newmtl black_DG_dg_vc5")
assert "Kd 0.000 0.000 0.000" in material[shadow:shadow + 6]
assert "map_Kd textures/dg_vc5.png" in material[shadow:shadow + 6]
mesh = (vc / "deltaglider_vc.obj").read_text()
assert "g PANELS1\nusemtl panels_generic_noise1\n" in mesh

button_groups = {
    "LMFD_LBUTTONS": 6, "LMFD_RBUTTONS": 6, "LMFD_BBUTTONS": 2,
    "RMFD_LBUTTONS": 6, "RMFD_RBUTTONS": 6, "RMFD_BBUTTONS": 2,
}
uvs = {}
group = ""
for line in mesh.splitlines():
    fields = line.split()
    if not fields:
        continue
    if fields[0] == "g":
        group = fields[1]
    elif fields[0] == "vt" and group in button_groups:
        uvs.setdefault(group, []).append(tuple(map(float, fields[1:3])))

with (vc / "textures/orbiter-vc/dg_vc4.png").open("rb") as stream:
    header = stream.read(24)
assert struct.unpack(">II", header[16:24]) == (1024, 1024)
for group, count in button_groups.items():
    assert len(uvs[group]) == count * 12, group
    for start in range(0, len(uvs[group]), 12):
        face = uvs[group][start:start + 4]
        sides = uvs[group][start + 4:start + 12]
        umin, umax = min(u for u, v in face), max(u for u, v in face)
        vmin, vmax = min(v for u, v in face), max(v for u, v in face)
        for u, v in sides:
            # Side faces must sample two texels inside the button, not adjacent atlas artwork.
            assert abs(min(u - umin, umax - u) - 2 / 1024) < 0.00002, group
            assert abs(min(v - vmin, vmax - v) - 2 / 1024) < 0.00002, group
print("deltaglider_panel_assets_test ok")
