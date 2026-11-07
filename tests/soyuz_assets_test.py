#!/usr/bin/env python3
"""Check that the Soyuz add-on includes its referenced media and mesh sources."""

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ADDON = ROOT / "addons/cvel-soyuz"
MODELS = ADDON / "models"


def main():
    launcher = (
        "r7blocka", "r7blockb", "r7blockd", "r7blockg", "r7blocki",
        "r7blockw", "r7skirt3p1", "r7skirt3p2", "r7skirt3p3", "r7tmaer",
        "soyshrd1", "soyshrd2",
    )
    for name in launcher:
        assert (MODELS / f"{name}.obj").is_file()
        assert (ADDON / f"source/launcher/{name}.msh").is_file()
        material = (MODELS / f"{name}.mtl").read_text()
        assert not any(line.startswith("map_") for line in material.splitlines())
    for name in ("soychute", "soyuz", "soyuz-descent"):
        assert (ADDON / f"source/tma/{name}.msh").is_file()
    assert not any(line.startswith("map_") for line in
                   (MODELS / "chute.mtl").read_text().splitlines())
    assert (ADDON / "source/LICENSE-LGPL-2.1.txt").is_file()

    textures = set()
    for material in MODELS.glob("*.mtl"):
        for line in material.read_text().splitlines():
            if line.startswith("map_Kd "):
                path = material.parent / line.removeprefix("map_Kd ").strip()
                assert path.is_file(), f"{material}: missing {path}"
                textures.add(path.name)
    assert textures == {
        "SOY-DESC.png", "SOY-DOCK.png", "SOY-ORB.png", "SOY-REAR.png",
        "SOY-SER2.png", "SOY-SERV.png", "SOY-SOL.png",
    }
    for filename in ("fg.json", "tma.json", "descent.json"):
        json.loads((ADDON / filename).read_text())
    print("Soyuz asset checks passed")


if __name__ == "__main__":
    main()
