#!/usr/bin/env python3
"""Verify restored Saturn variant ship model and texture dependencies."""

import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
for name in ("cvel-saturn-mlv3", "cvel-saturn-v23"):
    addon = root / "addons" / name
    manifest = json.loads((addon / "addon.json").read_text())
    models = set()

    def collect(value):
        if isinstance(value, dict):
            for key, child in value.items():
                if key == "model" and isinstance(child, str):
                    models.add((addon / child).resolve())
                else:
                    collect(child)
        elif isinstance(value, list):
            for child in value:
                collect(child)

    for ship in manifest["ships"]:
        collect(json.loads((addon / ship).read_text()))
    assert models, name
    for model in models:
        assert model.is_file(), model
        materials = [model.parent / line[7:] for line in model.read_text().splitlines()
                     if line.startswith("mtllib ")]
        assert materials, model
        for material in materials:
            for line in material.read_text().splitlines():
                if line.startswith("map_Kd "):
                    assert (material.parent / line[7:]).is_file(), (material, line)
    assert list((addon / "source").glob("*.txt")), name
print("cvel_saturn_variants_test ok")
