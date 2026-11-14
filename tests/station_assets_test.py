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
                    if Path(part["model"]).name == "iss.obj":
                        assert "ISS_paired" in part.get("single_sided", []), f"ISS paired faces are not culled: {ship_file}"

assert {model.name for model in models} == {
    "mir.obj", "iss.obj", "wheel.obj", "salyut.obj", "lunar_salyut.obj"
}
for model in sorted(models):
    assert model.is_file(), f"missing model: {model}"
    materials = set()
    faces = 0
    normals = []
    positions = []
    surface = ""
    iss_faces = {}
    iss_pairs = set()
    with model.open() as stream:
        for line in stream:
            if line.startswith("mtllib "):
                materials.add(model.parent / line.split(maxsplit=1)[1].strip())
            elif line.startswith("v "):
                positions.append(tuple(map(float, line.split()[1:4])))
            elif line.startswith("vn "):
                normals.append(tuple(map(float, line.split()[1:4])))
            elif line.startswith("o "):
                surface = line.split()[1]
            elif line.startswith("f "):
                faces += 1
                if model.name in {"iss.obj", "mir.obj"}:
                    for corner in line.split()[1:]:
                        indices = corner.split("/")
                        if len(indices) > 2 and indices[2]:
                            normal = normals[int(indices[2]) - 1]
                            assert sum(value * value for value in normal) > 1e-8, f"zero normal in {model}"
                if model.name == "iss.obj":
                    points = tuple(positions[int(corner.split("/")[0]) - 1] for corner in line.split()[1:])
                    if len(points) == 3 and len(set(points)) == 3:
                        first = points.index(min(points))
                        winding = points[first:] + points[:first]
                        key = tuple(sorted(points))
                        if key in iss_faces:
                            assert key not in iss_pairs, f"more than two coincident faces in {model}"
                            previous, previous_surface = iss_faces[key]
                            assert winding != previous, f"same-facing duplicate in {model}"
                            assert surface == previous_surface == "ISS_paired", f"unculled panel faces in {model}"
                            iss_pairs.add(key)
                        else:
                            iss_faces[key] = (winding, surface)
    assert faces > 0 and materials, f"empty or untextured model: {model}"
    if model.name == "iss.obj":
        assert iss_pairs, "ISS paired panel surfaces are missing"
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
