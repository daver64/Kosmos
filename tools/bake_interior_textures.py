#!/usr/bin/env python3
"""Bake the interior wall, ceiling and floor textures in data/textures/interior/ from import/open-game-art.

Walls and ceilings use the "Majestical" metal set (diffuse, normal map, specular), the floor the floor colour and
bump map. The bump map becomes a tangent-space normal map. Specular maps become a glTF metallic-roughness texture
(R occlusion, G roughness, B metallic). Everything is sized down to 1024 pixels.

Usage: python3 tools/bake_interior_textures.py
"""
import os
import sys

import numpy as np
from PIL import Image

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
metal = os.path.join(root, "import/open-game-art/textures/metal")
floor = os.path.join(root, "import/open-game-art/textures/floor")
out = os.path.join(root, "data/textures/interior")
SIZE = 1024


def load(path):
    image = Image.open(path).convert("RGB")
    if max(image.size) > SIZE:
        scale = SIZE / max(image.size)
        image = image.resize((round(image.width * scale), round(image.height * scale)), Image.LANCZOS)
    return image


def save(image, name):
    image.save(os.path.join(out, name), optimize=True)
    print("wrote", name, image.size)


def metallic_roughness(spec_path):
    spec = np.asarray(load(spec_path).convert("L"), dtype=np.float32) / 255.0
    packed = np.zeros(spec.shape + (3,), dtype=np.uint8)
    packed[..., 0] = 255
    packed[..., 1] = np.clip((1.0 - spec * 0.75) * 255.0, 0, 255)
    packed[..., 2] = np.clip(spec * 255.0, 0, 255)
    return Image.fromarray(packed)


def bump_to_normal(path, strength=3.0):
    height = np.asarray(load(path).convert("L"), dtype=np.float32) / 255.0
    dx = (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) * strength
    dy = (np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)) * strength
    normal = np.dstack((-dx, dy, np.ones_like(height)))
    normal /= np.linalg.norm(normal, axis=2, keepdims=True)
    return Image.fromarray(((normal * 0.5 + 0.5) * 255.0).astype(np.uint8))


def main():
    os.makedirs(out, exist_ok=True)
    for name, source in (("wall", "base"), ("ceiling", "beamopaque")):
        save(load(f"{metal}/mjst_metal_{source}_diffuse.png"), f"{name}.png")
        save(load(f"{metal}/mjst_metal_{source}_normalbump.png"), f"{name}_normal.png")
        save(metallic_roughness(f"{metal}/mjst_metal_{source}_specular.png"), f"{name}_mr.png")
    save(load(f"{floor}/floor.jpg"), "floor.png")
    save(bump_to_normal(f"{floor}/floor_bump.jpg"), "floor_normal.png")


if __name__ == "__main__":
    sys.exit(main())
