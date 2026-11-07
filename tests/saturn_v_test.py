#!/usr/bin/env python3
"""Verify the restored Saturn V's ship, scenario, and model dependencies."""

import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
addon = root / "addons" / "cvel-saturn-v"
manifest = json.loads((addon / "addon.json").read_text())
models = set()


def collect(value):
    if isinstance(value, dict):
        for key, child in value.items():
            if key == "model" and isinstance(child, str):
                models.add(addon / child)
            else:
                collect(child)
    elif isinstance(value, list):
        for child in value:
            collect(child)


for ship in manifest["ships"]:
    collect(json.loads((addon / ship).read_text()))
assert len(models) == 12, models
for model in models:
    assert model.is_file(), model
    materials = [model.parent / line[7:] for line in model.read_text().splitlines()
                 if line.startswith("mtllib ")]
    assert materials, model
    for material in materials:
        for line in material.read_text().splitlines():
            if line.startswith("map_Kd "):
                assert (material.parent / line[7:]).is_file(), (material, line)

scenario = root / "scenarios/CVEL/CVEL-SaturnV/apollo11_saturn_v_t_minus_5.json"
assert json.loads(scenario.read_text())["ships"][0]["ship"] == "CVEL Saturn V"
assert (addon / "source/SaturnV.txt").is_file()
print("saturn_v_test ok")
