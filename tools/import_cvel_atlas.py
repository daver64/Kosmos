#!/usr/bin/env python3
"""Convert the Artistic-licensed CVEL Atlas III source models."""

import argparse
from pathlib import Path

from PIL import Image, PngImagePlugin

import msh_import

ROOT = Path(__file__).resolve().parents[1]
NAMES = ("atlasiii_stg1", "atlasiii_interstg1", "atlasiii_fairing1",
         "atlasiii_fairing2", "atlasiii_centaur")
NOTICE = ("Kosmos conversion, 2026-11-07: converted from Orbiter meshes and textures, "
          "mirrored the Z axis, converted DDS to PNG, and removed coincident faces. "
          "See ../source/README.md and ../source/ARTISTIC-LICENSE.txt.")


def remove_coincident_faces(path):
    lines = path.read_text().splitlines()
    vertices = []
    seen = set()
    kept = []
    removed = 0
    for line in lines:
        words = line.split()
        if words and words[0] == "v":
            vertices.append(tuple(round(float(value), 4) for value in words[1:4]))
        elif words and words[0] == "f":
            corners = [vertices[int(value.split("/")[0]) - 1] for value in words[1:]]
            key = tuple(sorted(corners))
            if key in seen:
                removed += 1
                continue
            seen.add(key)
        kept.append(line)
    path.write_text("\n".join(kept) + "\n")
    return removed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="CVELAtlas distribution directory")
    args = parser.parse_args()
    meshes = {p.stem.lower(): p for p in (args.source / "Meshes").iterdir()
              if p.suffix.lower() == ".msh"}
    output = ROOT / "addons/cvel-atlas/models"
    textures = output / "textures"
    for name in NAMES:
        mesh = msh_import.parse_msh(meshes[name])
        needed = {mesh.textures[g.texture - 1] for g in mesh.groups
                  if g.triangles and not g.flag & 2 and 0 < g.texture <= len(mesh.textures)
                  and not mesh.textures[g.texture - 1].startswith(";")}
        vertices, triangles, count = msh_import.write_obj(
            mesh, str(output / name), [str(args.source / "Textures")],
            tex_out=str(textures))
        if count != len(needed):
            raise RuntimeError(f"{name}: converted {count} of {len(needed)} required textures")
        print(f"{name}: {vertices} vertices, {triangles} triangles, {count} textures", flush=True)
    for name in NAMES:
        print(f"{name}: removed {remove_coincident_faces(output / (name + '.obj'))} coincident faces")
        for suffix in (".obj", ".mtl"):
            path = output / (name + suffix)
            path.write_text("# " + NOTICE + "\n" + path.read_text())
    for path in textures.glob("*.png"):
        with Image.open(path) as image:
            image.load()
            metadata = PngImagePlugin.PngInfo()
            metadata.add_text("Description", NOTICE)
            image.save(path, pnginfo=metadata)


if __name__ == "__main__":
    main()
