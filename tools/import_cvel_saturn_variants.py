#!/usr/bin/env python3
"""Convert the CVEL Saturn MLV3 and V-23L Orbiter meshes."""

import argparse
from pathlib import Path

import msh_import
from import_cvel_atlas import remove_coincident_faces

ROOT = Path(__file__).resolve().parents[1]
VARIANTS = {
    "CVELSaturnVMLV3": ("cvel-saturn-mlv3", (
        "s1c_stretch240", "sat5intstg", "sat5stg2", "sat5stg3")),
    "CVELSaturnV23L": ("cvel-saturn-v23", (
        "ms-lrb-240ine", "ms-lrb-240inn", "ms-lrb-240ins", "ms-lrb-240inw",
        "s1c_stretch240", "s2_stretch41", "s4b_stretch198", "sat5intstg")),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("orbiter", type=Path, help="directory containing the CVEL distributions")
    args = parser.parse_args()
    for distribution, (addon, names) in VARIANTS.items():
        source = args.orbiter / distribution
        files = {path.stem.lower(): path for path in (source / "Meshes").iterdir()
                 if path.suffix.lower() == ".msh"}
        output = ROOT / "addons" / addon / "models"
        for name in names:
            mesh = msh_import.parse_msh(files[name])
            for index, texture in enumerate(mesh.textures):
                if texture.upper() in {"SICFINA.DDS", "SICFINB.DDS", "SICFINC.DDS", "SICFIND.DDS"}:
                    mesh.textures[index] = ";" + texture
                    print(f"{addon}/{name}: {texture} is absent from the source; using the fin material", flush=True)
            needed = {mesh.textures[group.texture - 1] for group in mesh.groups
                      if group.triangles and not group.flag & 2 and 0 < group.texture <= len(mesh.textures)
                      and not mesh.textures[group.texture - 1].startswith(";")}
            _, triangles, count = msh_import.write_obj(
                mesh, str(output / name), [str(source / "Textures")],
                tex_out=str(output / "textures"))
            if count != len(needed):
                raise RuntimeError(f"{name}: converted {count} of {len(needed)} textures")
            removed = remove_coincident_faces(output / f"{name}.obj")
            print(f"{addon}/{name}: {triangles} triangles, {removed} coincident faces removed", flush=True)


if __name__ == "__main__":
    main()
