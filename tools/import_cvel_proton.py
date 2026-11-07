#!/usr/bin/env python3
"""Convert the Artistic-licensed CVEL Proton-K launcher meshes."""

import argparse
from pathlib import Path

from PIL import Image, PngImagePlugin

import msh_import
from import_cvel_atlas import remove_coincident_faces

ROOT = Path(__file__).resolve().parents[1]
NAMES = ("protonstg1", "protkstg2_uw", "protonplfleft",
         "protonplfright", "protonstg3")
NOTICE = ("Kosmos conversion: reflected the Z axis, converted DDS to PNG, "
          "and removed coincident faces. See ../source/README.md and "
          "../source/ARTISTIC-LICENSE.txt.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="CVELProton distribution directory")
    args = parser.parse_args()
    meshes = {p.stem.lower(): p for p in (args.source / "Meshes").iterdir()
              if p.suffix.lower() == ".msh"}
    output = ROOT / "addons/cvel-proton/models"
    textures = output / "textures"
    for name in NAMES:
        mesh = msh_import.parse_msh(meshes[name])
        available = {p.name.lower() for p in (args.source / "Textures").iterdir()
                     if p.is_file()}
        for g in mesh.groups:
            if 0 < g.texture <= len(mesh.textures):
                texture = mesh.textures[g.texture - 1]
                if name == "protkstg2_uw" and texture.lower() == "protonstage2.dds" and texture.lower() not in available:
                    g.texture = 0
                    print(f"{name}: {texture} is missing; using the source material colour", flush=True)
        needed = {mesh.textures[g.texture - 1] for g in mesh.groups
                  if g.triangles and not g.flag & 2 and 0 < g.texture <= len(mesh.textures)
                  and not mesh.textures[g.texture - 1].startswith(";")}
        missing = [texture for texture in needed if Path(texture).name.lower() not in available]
        if missing:
            raise FileNotFoundError(f"{name}: missing textures: {', '.join(missing)}")
        _, triangles, count = msh_import.write_obj(
            mesh, str(output / name), [str(args.source / "Textures")],
            tex_out=str(textures))
        if count != len(needed):
            raise RuntimeError(f"{name}: converted {count} of {len(needed)} required textures")
        removed = remove_coincident_faces(output / (name + ".obj"))
        print(f"{name}: {triangles} triangles, {count} textures, {removed} coincident faces removed",
              flush=True)
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
