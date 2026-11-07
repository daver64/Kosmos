#!/usr/bin/env python3
"""Verify Energia-M model placements, textures and attribution."""

import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
addon = root / "addons/cvel-energia-m"
ship = json.loads((addon / "ship.json").read_text())
expected = [
    [("boosterm1", [5.95, 0, 0]), ("boosterm2", [-5.95, 0, 0])],
    [("energiamcore", [0, 0, 0])],
    [("energiamfair_1", [0, 0, -16.65])],
    [("energiamfair_2", [0, 0, -16.65])],
]
assert len(ship["stages"]) == len(expected)
for stage, parts in zip(ship["stages"], expected):
    assert stage["parts"] == [{"model": f"models/{name}.obj", "offset": offset}
                              for name, offset in parts]
    for name, _ in parts:
        model = addon / "models" / f"{name}.obj"
        assert model.is_file(), model
        with model.open() as stream:
            materials = [model.parent / line.strip()[7:] for line in stream if line.startswith("mtllib ")]
        assert materials, model
        for material in materials:
            textures = [line[7:] for line in material.read_text().splitlines() if line.startswith("map_Kd ")]
            assert textures, material
            for texture in textures:
                assert (material.parent / texture).is_file(), (material, texture)
assert "Erik Anderson" in (addon / "source/Energia-M.txt").read_text()
assert (addon / "source/COPYING.TXT").is_file()
print("cvel_energia_m_test ok")
