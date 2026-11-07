#!/usr/bin/env python3
"""Convert the CVEL Energia-M launcher meshes."""

import argparse
from pathlib import Path

import msh_import
from import_cvel_atlas import remove_coincident_faces

ROOT = Path(__file__).resolve().parents[1]
NAMES = ("boosterm1", "boosterm2", "energiamcore", "energiamfair_1", "energiamfair_2")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="CVELEnergiaM distribution directory")
    args = parser.parse_args()
    meshes = {p.stem.lower(): p for p in (args.source / "Meshes").rglob("*")
              if p.suffix.lower() == ".msh"}
    output = ROOT / "addons/cvel-energia-m/models"
    textures = args.source / "Textures"
    for name in NAMES:
        mesh = msh_import.parse_msh(meshes[name])
        needed = {mesh.textures[g.texture - 1] for g in mesh.groups
                  if g.triangles and not g.flag & 2 and 0 < g.texture <= len(mesh.textures)
                  and not mesh.textures[g.texture - 1].startswith(";")}
        available = {p.name.lower() for p in textures.rglob("*") if p.is_file()}
        missing = [tex for tex in needed if Path(tex.replace("\\", "/")).name.lower() not in available]
        if missing:
            raise FileNotFoundError(f"{name}: missing textures: {', '.join(missing)}")
        _, triangles, count = msh_import.write_obj(
            mesh, str(output / name), [str(textures), str(textures / "cvel_energiam")],
            tex_out=str(output / "textures"))
        if count != len(needed):
            raise RuntimeError(f"{name}: converted {count} of {len(needed)} required textures")
        removed = remove_coincident_faces(output / (name + ".obj"))
        print(f"{name}: {triangles} triangles, {count} textures, {removed} coincident faces removed",
              flush=True)


if __name__ == "__main__":
    main()
