#!/usr/bin/env python3
"""Check the SEV model and wheel contacts used by the Tranquility scenario."""

import json
import math
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
addon = ROOT / "addons/nasa-sev"
ship = json.loads((addon / "ship.json").read_text())
scenario = json.loads((ROOT / "scenarios/CVEL/CVEL-Bases/lunar_rover_tranquility.json").read_text())
assert scenario["ships"][scenario["active"]]["ship"] == ship["name"] == "Space Exploration Vehicle"
assert scenario["ships"][scenario["active"]]["surface"]["base"] == "tranquility"
assert len(ship["wheels"]) == 6 and all(w["drive"] for w in ship["wheels"])
assert any(s["type"] == "drive_motor" for s in ship["systems"])

data = (addon / ship["stages"][0]["parts"][0]["model"]).read_bytes()
magic, version, length, json_length, json_type = struct.unpack_from("<5I", data)
assert (magic, version, length, json_type) == (0x46546C67, 2, len(data), 0x4E4F534A)
document = json.loads(data[20:20 + json_length])
binary_length, binary_type = struct.unpack_from("<2I", data, 20 + json_length)
binary_start = 28 + json_length
assert binary_type == 0x004E4942 and binary_start + binary_length == len(data)
assert document["images"] and all("bufferView" in image for image in document["images"])
primitive = document["meshes"][0]["primitives"][0]
assert "TEXCOORD_0" in primitive["attributes"] and "material" in primitive
positions = document["accessors"][primitive["attributes"]["POSITION"]]
view = document["bufferViews"][positions["bufferView"]]
assert positions["componentType"] == 5126 and positions["type"] == "VEC3"
start = binary_start + view.get("byteOffset", 0) + positions.get("byteOffset", 0)
stride = view.get("byteStride", 12)
vertices = [struct.unpack_from("<3f", data, start + i * stride) for i in range(positions["count"])]
# The source model's longer, windowed nose is at +Z; Kosmos drives toward -Z.
assert max(p[2] for p in vertices) > -min(p[2] for p in vertices) + 0.5
part = ship["stages"][0]["parts"][0]
angle = math.radians(part.get("rotate", [0, 0, 0])[1])
c, s = math.cos(angle), math.sin(angle)
vertices = [(c * x + s * z, y, -s * x + c * z) for x, y, z in vertices]
assert -min(p[2] for p in vertices) > max(p[2] for p in vertices) + 0.5
for wheel in ship["wheels"]:
    x, y, z = wheel["position"]
    near = [p for p in vertices if abs(p[0] - x) < 0.3 and abs(p[2] - z) < 0.3]
    assert near and abs(min(p[1] for p in near) - y) < 0.08, wheel
print("sev_assets_test ok")
