#!/usr/bin/env python3
"""Verify Titan IIIE/IV Centaur engines and PBR texture references."""

import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
addon = root / "addons" / "cvel-titan"

for filename in ("titan3e.json", "titan4a.json"):
    ship = json.loads((addon / filename).read_text())
    centaur = next(stage for stage in ship["stages"] if stage["name"] == "Centaur")
    engines = centaur["engines"]
    assert len(engines) == 2, (filename, engines)
    positions = [engine["position"] for engine in engines]
    assert positions == [[-0.008, 0.608, 4.655], [-0.008, -0.608, 4.655]], (filename, positions)
    assert sum(engine["thrust"] for engine in engines) == 73428, (filename, engines)
    assert all(engine["plume_width"] == 0.25 for engine in engines), (filename, engines)

    for part in (part for stage in ship["stages"] for part in stage.get("parts", [])):
        model = addon / part["model"]
        mtllibs = [line.split(maxsplit=1)[1] for line in model.read_text().splitlines() if line.startswith("mtllib ")]
        for mtllib in mtllibs:
            mtl = model.parent / mtllib
            lines = mtl.read_text().splitlines()
            for line in lines:
                if line.startswith("map_Bump "):
                    texture = mtl.parent / line.split(maxsplit=1)[1]
                    assert texture.is_file(), (filename, mtl, texture)

print("cvel_titan_assets_test ok")
