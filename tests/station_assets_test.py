#!/usr/bin/env python3
"""Check bundled station models and all their material textures."""

import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
models = set()
for addon_name in ("orbiter-stations", "salyut"):
    addon = root / "addons" / addon_name
    manifest = json.loads((addon / "addon.json").read_text())
    for ship_file in manifest["ships"]:
        ship = json.loads((addon / ship_file).read_text())
        for stage in ship["stages"]:
            for part in stage["parts"]:
                if part.get("model"):
                    models.add(addon / part["model"])

assert {model.name for model in models} == {
    "mir.obj", "iss.obj", "wheel.obj", "salyut.obj", "lunar_salyut.obj"
}
for model in sorted(models):
    assert model.is_file(), f"missing model: {model}"
    materials = set()
    faces = 0
    normals = []
    with model.open() as stream:
        for line in stream:
            if line.startswith("mtllib "):
                materials.add(model.parent / line.split(maxsplit=1)[1].strip())
            elif line.startswith("vn "):
                normals.append(tuple(map(float, line.split()[1:4])))
            elif line.startswith("f "):
                faces += 1
                if model.name == "mir.obj":
                    for corner in line.split()[1:]:
                        indices = corner.split("/")
                        if len(indices) > 2 and indices[2]:
                            normal = normals[int(indices[2]) - 1]
                            assert sum(value * value for value in normal) > 1e-8, f"zero normal in {model}"
    assert faces > 0 and materials, f"empty or untextured model: {model}"
    textures = set()
    for material in materials:
        assert material.is_file(), f"missing material: {material}"
        for line in material.read_text().splitlines():
            if line.startswith("map_Kd "):
                textures.add(material.parent / line.split(maxsplit=1)[1])
    assert textures, f"no textures referenced: {model}"
    for texture in textures:
        assert texture.is_file() and texture.stat().st_size > 0, f"missing texture: {texture}"

print(f"station_assets_test ok ({len(models)} models)")
