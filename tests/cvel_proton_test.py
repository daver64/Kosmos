#!/usr/bin/env python3
"""Verify the restored Proton stages and their model dependencies."""

import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
addon = root / "addons/cvel-proton"
ship = json.loads((addon / "ship.json").read_text())
expected = [
    ("protonstg1", [0, 0, 13]),
    ("protkstg2_uw", [0, 0, -5.5]),
    ("protonplfleft", [0, 0, -19.5]),
    ("protonplfright", [0, 0, -19.5]),
    ("protonstg3", [0, 0, -16.2]),
]
assert len(ship["stages"]) == len(expected)
for stage, (name, offset) in zip(ship["stages"], expected):
    assert stage["parts"] == [{"model": f"models/{name}.obj", "offset": offset}]
    model = addon / "models" / f"{name}.obj"
    assert model.is_file(), model
    with model.open() as stream:
        materials = [model.parent / line.strip()[7:] for line in stream if line.startswith("mtllib ")]
    assert materials, model
    for material in materials:
        assert "Kosmos conversion:" in material.read_text(), material
        for line in material.read_text().splitlines():
            if line.startswith("map_Kd "):
                assert (material.parent / line[7:]).is_file(), (material, line)
assert ship["stages"][2]["with_next"]
assert (addon / "source/ARTISTIC-LICENSE.txt").is_file()
assert "Artistic Licence" in (addon / "source/Proton-k.txt").read_text()
assert "ProtonStage2.dds" in (addon / "source/README.md").read_text()
print("cvel_proton_test ok")
