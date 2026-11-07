#!/usr/bin/env python3
"""Convert the CVEL Saturn V meshes from the original Orbiter distribution."""

import argparse
import os
from pathlib import Path
import subprocess
import sys

import msh_import

ROOT = Path(__file__).resolve().parents[1]
MESHES = (
    "sat5stg1", "sat5intstg", "sat5stg2", "sat5stg3",
    "sat5stg31", "sat5stg32", "sat5stg33", "sat5stg34",
    "nsaturn1_sm", "nsaturn1_cm", "sat5bpc", "apollo_3chute",
)
COPLANAR_MESHES = (
    "sat5intstg", "sat5stg2", "sat5stg3", "sat5stg31",
    "sat5stg32", "sat5stg33", "sat5stg34", "nsaturn1_sm", "sat5bpc",
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="CVELSaturnV distribution directory")
    args = parser.parse_args()
    mesh_dir = args.source / "Meshes"
    sources = {path.stem.lower(): path for path in mesh_dir.iterdir() if path.suffix.lower() == ".msh"}
    target = ROOT / "addons" / "cvel-saturn-v" / "models"
    textures = args.source / "Textures"
    for name in MESHES:
        mesh = msh_import.parse_msh(sources[name])
        hidden = msh_import.hidden_triangles(mesh)
        vertices, triangles, count = msh_import.write_obj(
            mesh, str(target / name), [str(textures)], tex_out=str(target / "textures"), objects=True
        )
        needed = {mesh.textures[group.texture - 1] for group in mesh.groups
                  if group.triangles and not group.flag & 2 and 0 < group.texture <= len(mesh.textures)
                  and not mesh.textures[group.texture - 1].startswith(";")}
        if count != len(needed):
            raise RuntimeError(f"{name}: converted {count} of {len(needed)} required textures")
        print(f"{name}: {vertices} vertices, {triangles} triangles, {count} textures, "
              f"{sum(map(len, hidden.values()))} overlapping triangles removed", flush=True)
    env = os.environ.copy()
    env["OPENBLAS_NUM_THREADS"] = "1"
    env["OMP_NUM_THREADS"] = "1"
    subprocess.run([sys.executable, str(ROOT / "tools" / "obj_zfight.py"),
                    *(str(target / f"{name}.obj") for name in COPLANAR_MESHES)],
                   check=True, env=env)


if __name__ == "__main__":
    main()
