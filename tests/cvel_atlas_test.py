#!/usr/bin/env python3
"""Verify the restored Atlas III models, materials and source notices."""

import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
addon = root / "addons/cvel-atlas"
ship = json.loads((addon / "ship.json").read_text())
models = [addon / part["model"] for stage in ship["stages"]
          for part in stage.get("parts", []) if "model" in part]
assert len(models) == 5
for model in models:
    lines = model.read_text().splitlines()
    assert lines[0].startswith("# Kosmos conversion, 2026-11-07:"), model
    for line in lines:
        if line.startswith("mtllib "):
            material = model.parent / line[7:]
            material_lines = material.read_text().splitlines()
            assert material_lines[0].startswith("# Kosmos conversion, 2026-11-07:"), material
            for entry in material_lines:
                if entry.startswith("map_Kd "):
                    assert (material.parent / entry[7:]).is_file(), (material, entry)
for file in ("AtlasIII.txt", "ARTISTIC-LICENSE.txt", "Model/AtlasIII.an8"):
    assert (addon / "source" / file).is_file()
assert len(list((addon / "source/Meshes").glob("*.msh"))) == 5
print("cvel_atlas_test ok")
