#!/usr/bin/env python3
"""Verify Titan IIIE/IV Centaur engines and PBR texture references."""

import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
addon = root / "addons" / "cvel-titan"

for path in sorted(addon.glob("titan*.json")):
    ship = json.loads(path.read_text())
    stages = ship["stages"]
    boosters = stages[0].get("engines", [])
    if any(engine.get("fuel") == "solid" for engine in boosters):
        assert stages[1].get("ignite_with_previous") is True, (path.name, stages[1])
        assert all(
            engine["plume"] == 35
            and engine["plume_width"] == 0.65
            and engine["plume_glow"] == 1.0
            and engine["plume_density"] == 2
            for engine in boosters
        ), (path.name, boosters)

titans = sorted(addon.glob("titan*.json"))
assert titans
for path in titans:
    cockpit = json.loads(path.read_text())["cockpit"]
    assert cockpit["mfds"] is True, (path.name, cockpit)
    assert cockpit["external_mfds"] is False, (path.name, cockpit)

iiic = json.loads((addon / "titan3c.json").read_text())
iiid = json.loads((addon / "titan3d.json").read_text())
for stage_index in (1, 4):
    iiic_engines = iiic["stages"][stage_index]["engines"]
    reference_engines = iiid["stages"][stage_index]["engines"]
    assert len(iiic_engines) == len(reference_engines)
    assert all(
        engine["plume_width"] == reference["plume_width"]
        and engine["plume_density"] == reference["plume_density"]
        for engine, reference in zip(iiic_engines, reference_engines)
    )

titan34d = json.loads((addon / "titan34d.json").read_text())
core = next(stage for stage in titan34d["stages"] if stage["name"] == "Stage 1")
stage_2 = next(stage for stage in titan34d["stages"] if stage["name"] == "Stage 2")
assert all(
    engine["plume_width"] == 0.9 and engine["plume_offset"] == [0, 0, 0.3]
    for engine in core["engines"]
)
assert all(
    engine["plume_width"] == 0.72 and engine["plume_offset"] == [0, 0, 0.3]
    for engine in stage_2["engines"]
)

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

titan4b = json.loads((addon / "titan4b.json").read_text())
titan4b_stage2 = next(stage for stage in titan4b["stages"] if stage["name"] == "Stage 2")
assert any(
    part["model"] == "models/tenftinterstage.obj" and part["offset"] == [0, 0, 3.75]
    for part in titan4b_stage2["parts"]
)

print("cvel_titan_assets_test ok")
