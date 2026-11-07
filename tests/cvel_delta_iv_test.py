#!/usr/bin/env python3
"""Verify Delta IV stage meshes, material textures, and source credit."""

import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
addon = root / "addons/cvel-delta-iv"
ship = json.loads((addon / "ship.json").read_text())
expected = [
    [("deltaivbooster_1", [-4.54, 0, 0]), ("deltaivbooster_2", [4.54, 0, 0])],
    [("deltaivstg1", [0, 0, 0])],
    [("deltaivplf_1", [0, 0, -29.1])],
    [("deltaivplf_2", [0, 0, -29.1])],
    [("deltaivstg2", [0, 0, -24.2])],
]
assert len(ship["stages"]) == len(expected)
for stage, parts in zip(ship["stages"], expected):
    assert [(part["model"], part["offset"]) for part in stage["parts"]] == [
        (f"models/{name}.obj", offset) for name, offset in parts]
assert ship["stages"][2]["with_next"]
models = [addon / part["model"] for stage in ship["stages"] for part in stage["parts"]]
for model in models:
    assert model.is_file(), model
    materials = [model.parent / line[7:] for line in model.read_text().splitlines()
                 if line.startswith("mtllib ")]
    assert materials, model
    for material in materials:
        textures = [line[7:] for line in material.read_text().splitlines()
                    if line.startswith("map_Kd ")]
        assert textures, material
        for texture in textures:
            assert (material.parent / texture).is_file(), (material, texture)
assert "Alex Hohlov" in (addon / "source/CVEL-Delta4H.txt").read_text()
print("cvel_delta_iv_test ok")
