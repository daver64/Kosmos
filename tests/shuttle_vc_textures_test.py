#!/usr/bin/env python3
"""Check the Shuttle-A cabin groups retain their original texture assignments."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MODELS = ROOT / "addons/cvel-shuttle-a/models"
EXPECTED = {
    "group14": "SHA1TX3.png",
    "group15": "SHA1TX1.png",
    "group18": "SHA1TX2.png",
    "group19": "SHA1TX2.png",
    "group20": "SHA1TX2.png",
    "group21": "SHA1TX1.png",
}

materials = {}
name = None
for line in (MODELS / "shuttlea_vc.mtl").read_text().splitlines():
    if line.startswith("newmtl "):
        name = line[7:].strip()
    elif line.startswith("map_Kd ") and name:
        materials[name] = line[7:].strip()

groups = {}
name = None
for line in (MODELS / "shuttlea_vc.obj").read_text().splitlines():
    if line.startswith("g "):
        name = line[2:].strip()
    elif line.startswith("usemtl ") and name in EXPECTED:
        groups[name] = line[7:].strip()

assert groups.keys() == EXPECTED.keys(), groups
for group, texture in EXPECTED.items():
    path = materials.get(groups[group])
    assert path and Path(path).name == texture, (group, path)
    assert (MODELS / path).is_file(), path

print("Shuttle-A cockpit texture assignments OK")
