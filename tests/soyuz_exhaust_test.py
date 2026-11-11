#!/usr/bin/env python3
"""Check Soyuz TMA plume and RCS settings on both spacecraft variants."""

import json
from pathlib import Path

addon = Path(__file__).resolve().parents[1] / "addons/cvel-soyuz"
standalone = json.loads((addon / "tma.json").read_text())
launcher = json.loads((addon / "fg.json").read_text())
service = standalone["stages"][0]
payload = launcher["stages"][-1]
engine, stacked_engine = service["engines"][0], payload["engines"][0]
offset = payload["parts"][0]["offset"][2]

assert engine["thrust"] == stacked_engine["thrust"] == 3924
assert engine["isp"] == stacked_engine["isp"] == 305
assert 0 < engine["plume"] == stacked_engine["plume"] <= 1.2
assert 0 < engine["plume_width"] == stacked_engine["plume_width"] <= 0.05
assert engine["position"][:2] == stacked_engine["position"][:2] == [0, 0]
assert abs(engine["position"][2] + offset - stacked_engine["position"][2]) < 1e-6
for key in ("front_z", "rear_z"):
    assert abs(service["rcs_visual"][key] + offset - payload["rcs_visual"][key]) < 1e-6
for key in ("front_radius", "rear_radius"):
    assert service["rcs_visual"][key] == payload["rcs_visual"][key]
assert 0.3 <= service["rcs_visual"]["front_z"] < service["rcs_visual"]["rear_z"] <= 2.6
assert service["rcs_visual"]["front_radius"] <= 1.1
assert service["rcs_visual"]["rear_radius"] <= 1.4
print("soyuz_exhaust_test ok")
