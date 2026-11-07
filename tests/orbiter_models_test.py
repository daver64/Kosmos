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
    for addon in ("cvel-deltaglider", "cvel-deltaglider-nuclear", "cvel-shuttle-a",
                  "cvel-shuttle-a-nuclear", "cvel-shuttle-aq"):
        directory = ROOT / "addons" / addon
        cockpit = json.loads((directory / "ship.json").read_text())["cockpit"]
        model = (directory / cockpit["vc_model"]).resolve()
        objects = set()
        materials = []
        for line in model.read_text().splitlines():
            if line.startswith("o "):
                objects.add(line[2:])
            elif line.startswith("mtllib "):
                materials.append(model.parent / line[7:])
        for material in materials:
            for line in material.read_text().splitlines():
                if line.startswith("map_Kd "):
                    assert (material.parent / line[7:]).is_file(), (material, line)
        assert {"LMFD_DISPLAY", "RMFD_DISPLAY", "HUDDISP"} <= objects, model
        assert cockpit["vc_hotspots"] and cockpit["vc_animations"], addon
        for animation in cockpit.get("vc_animations", []):
            for name in animation["parts"]:
                assert any(name in obj for obj in objects), (addon, name)
        for instrument in cockpit.get("vc_instruments", []):
            assert instrument["part"] in objects, (addon, instrument)
        for gauge in cockpit.get("vc_gauges", []):
            assert gauge["part"] in objects and (directory / gauge["image"]).is_file(), (addon, gauge)
        if addon.startswith("cvel-shuttle"):
            assert len(cockpit["vc_gauges"]) == 2, addon
        else:
            assert {instrument["part"] for instrument in cockpit["vc_instruments"]} == {"ADI_DISPLAY", "HSI_DISPLAY"}
    print("orbiter_models_test ok")


if __name__ == "__main__":
    main()
