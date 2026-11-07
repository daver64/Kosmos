#!/usr/bin/env python3
"""Convert the CVEL Delta IV Heavy launcher meshes."""

import argparse
from pathlib import Path

import msh_import
from import_cvel_atlas import remove_coincident_faces

ROOT = Path(__file__).resolve().parents[1]
MESHES = ("deltaivbooster_1", "deltaivbooster_2", "deltaivstg1",
          "deltaivstg2", "deltaivplf_1", "deltaivplf_2")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="CVELDeltaIV distribution directory")
    args = parser.parse_args()
    files = {path.stem.lower(): path for path in (args.source / "Meshes").rglob("*")
             if path.suffix.lower() == ".msh"}
    target = ROOT / "addons/cvel-delta-iv/models"
    for name in MESHES:
        mesh = msh_import.parse_msh(files[name])
        needed = {mesh.textures[group.texture - 1] for group in mesh.groups
                  if group.triangles and not group.flag & 2 and 0 < group.texture <= len(mesh.textures)
                  and not mesh.textures[group.texture - 1].startswith(";")}
        _, triangles, count = msh_import.write_obj(
            mesh, str(target / name), [str(args.source / "Textures")],
            tex_out=str(target / "textures"))
        if count != len(needed):
            raise RuntimeError(f"{name}: converted {count} of {len(needed)} required textures")
        removed = remove_coincident_faces(target / f"{name}.obj")
        print(f"{name}: {triangles} triangles, {count} textures, {removed} coincident faces removed", flush=True)


if __name__ == "__main__":
    main()
