#!/usr/bin/env python3
"""Verify restored Orbiter model files, textures and animation targets."""

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODELS = (
    ("cvel-deltaglider", "deltaglider", "gear.json"),
    ("cvel-shuttle-a", "shuttlea", "pods.json"),
    ("cvel-dragonfly", "dragonfly", "animations.json"),
    ("orbiter-stations", "mir", None),
)


def main():
    for addon, name, animations in MODELS:
        directory = ROOT / "addons" / addon
        model = directory / "orbiter-models" / f"{name}.obj"
        objects = set()
        materials = []
        for line in model.read_text().splitlines():
            if line.startswith("o "):
                objects.add(line[2:])
            elif line.startswith("mtllib "):
                materials.append(model.parent / line[7:])
        assert objects and materials, model
        for material in materials:
            textures = [line[7:] for line in material.read_text().splitlines() if line.startswith("map_Kd ")]
            assert textures, material
            for texture in textures:
                assert (material.parent / texture).is_file(), (material, texture)
        if animations:
            for animation in json.loads((directory / animations).read_text())["animations"]:
                for target in animation["objects"]:
                    assert target in objects, (animations, target)
    print("orbiter_models_test ok")


if __name__ == "__main__":
    main()
