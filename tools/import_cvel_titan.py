#!/usr/bin/env python3
"""Convert the launcher meshes used by the CVEL Titan fleet."""

import argparse
from pathlib import Path

import msh_import
from import_cvel_atlas import remove_coincident_faces

ROOT = Path(__file__).resolve().parents[1]
MESHES = (
    "titan21", "titan22", "titani_stage2", "titan2i", "titan34_stage1",
    "titan34_stage2", "titan45_stage1", "titan45_stage2", "int10_8",
    "tenftinterstage", "tentofiveftinterstage", "cvelagena", "centaurd",
    "transtage", "ua1205w", "ua1205e", "ua1207w", "ua1207e", "srmuw",
    "srmue", "fairing102a", "fairing102b", "fairing201a", "fairing201b",
    "fairing301a", "fairing301b", "fairing402a", "fairing402b",
    "fairing612a", "fairing612b", "fairing612c", "fairing613a",
    "fairing613b", "fairing613c", "fairing615a", "fairing615b",
    "fairing615c",
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="CVELTitan distribution directory")
    parser.add_argument("orbiter_textures", type=Path, help="Orbiter source Textures directory for Centaur's SSMEC texture")
    args = parser.parse_args()
    files = {path.stem.lower(): path for path in (args.source / "Meshes").rglob("*")
             if path.suffix.lower() == ".msh"}
    target = ROOT / "addons/cvel-titan/models"
    for name in MESHES:
        mesh = msh_import.parse_msh(files[name])
        needed = {mesh.textures[group.texture - 1] for group in mesh.groups
                  if group.triangles and not group.flag & 2 and 0 < group.texture <= len(mesh.textures)
                  and not mesh.textures[group.texture - 1].startswith(";")}
        _, triangles, count = msh_import.write_obj(
            mesh, str(target / name), [str(args.source / "Textures"), str(args.orbiter_textures)],
            tex_out=str(target / "textures"))
        if count != len(needed):
            raise RuntimeError(f"{name}: converted {count} of {len(needed)} required textures")
        removed = remove_coincident_faces(target / f"{name}.obj")
        print(f"{name}: {triangles} triangles, {count} textures, {removed} coincident faces removed", flush=True)


if __name__ == "__main__":
    main()
