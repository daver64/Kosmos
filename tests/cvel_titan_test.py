#!/usr/bin/env python3
"""Verify the restored Titan fleet's stages and model dependencies."""

import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
addon = root / "addons/cvel-titan"
expected = {
    "titan1": ["titan21 int10_8", "fairing201a", "fairing201b", "titani_stage2"],
    "titan2": ["titan21 titan2i", "fairing301a", "fairing301b", "titan22"],
    "titan3b": ["titan34_stage1 titan2i", "fairing102a", "fairing102b",
                "titan34_stage2 tentofiveftinterstage", "cvelagena"],
    "titan3c": ["ua1205w ua1205e", "titan34_stage1 titan2i", "fairing301a", "fairing301b",
                "titan34_stage2 tenftinterstage", "transtage"],
    "titan3d": ["ua1205w ua1205e", "titan34_stage1 titan2i", "fairing402a", "fairing402b",
                "titan34_stage2"],
    "titan3e": ["ua1205w ua1205e", "titan34_stage1 titan2i", "fairing612a", "fairing612b",
                "fairing612c", "titan34_stage2 tenftinterstage", "centaurd"],
    "titan34d": ["ua1205w ua1205e", "titan34_stage1 titan2i", "fairing612a", "fairing612b",
                 "fairing612c", "titan34_stage2 tenftinterstage", "transtage"],
    "titan4a": ["ua1207w ua1207e", "titan45_stage1 titan2i", "fairing615a", "fairing615b",
                "fairing615c", "titan45_stage2 tenftinterstage", "centaurd"],
    "titan4b": ["srmuw srmue", "titan45_stage1 titan2i", "fairing613a", "fairing613b",
                "fairing613c", "titan45_stage2"],
}
models = set()
for ship_name, stages in expected.items():
    ship = json.loads((addon / f"{ship_name}.json").read_text())
    assert len(ship["stages"]) == len(stages), ship_name
    for index, (stage, names) in enumerate(zip(ship["stages"], stages)):
        names = names.split()
        assert [p["model"] for p in stage["parts"]] == [
            f"models/{name}.obj" for name in names], (ship_name, index)
        if names[0].startswith("fairing"):
            next_is_fairing = index + 1 < len(stages) and stages[index + 1].startswith("fairing")
            assert stage.get("with_next", False) == next_is_fairing, (ship_name, index)
        models.update(addon / p["model"] for p in stage["parts"])

for model in models:
    assert model.is_file(), model
    with model.open() as stream:
        materials = [model.parent / line.strip()[7:] for line in stream if line.startswith("mtllib ")]
    assert materials, model
    for material in materials:
        textures = [line[7:] for line in material.read_text().splitlines() if line.startswith("map_Kd ")]
        assert textures, material
        for texture in textures:
            assert (material.parent / texture).is_file(), (material, texture)
assert (addon / "source/Titans.pdf").is_file()
assert "Erik Anderson" in (addon / "source/CVEL Titans.txt").read_text()
print(f"cvel_titan_test ok ({len(expected)} ships, {len(models)} models)")
