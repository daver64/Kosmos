#!/usr/bin/env python3
"""Copy Celestia surface textures into data/textures/<body>.jpg, the name Kosmos looks for.

Celestia keeps each texture at up to three resolutions (hires, medres, lores). For every body this picks the largest
one available, so a moon with only a 512 x 256 image still gets a texture. Bodies already imported from Orbiter
(earth.dds, moon.dds, ...) are left alone because the loader prefers .dds.

Usage:
    celestia_import.py [--src data/textures/celestia] [--out data/textures] [--list]
"""
import argparse
import os
import shutil

# Kosmos body name (lower case) -> Celestia file stem.
BODIES = {
    "mercury": "mercury", "phobos": "phobos", "deimos": "deimos",
    "io": "io", "europa": "europa", "ganymede": "ganymede", "callisto": "callisto", "amalthea": "amalthea",
    "titan": "titan", "mimas": "mimas", "enceladus": "enceladus", "tethys": "tethys", "dione": "dione",
    "rhea": "rhea", "hyperion": "hyperion", "iapetus": "iapetus", "phoebe": "phoebe",
    "miranda": "miranda", "ariel": "ariel", "umbriel": "umbriel", "titania": "titania", "oberon": "oberon",
    "triton": "triton", "proteus": "proteus",
}


def best_source(src, stem):
    for level in ("hires", "medres", "lores"):
        path = os.path.join(src, level, stem + ".jpg")
        if os.path.exists(path):
            return path
    return None


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default=os.path.join(here, "..", "data", "textures", "celestia"))
    ap.add_argument("--out", default=os.path.join(here, "..", "data", "textures"))
    ap.add_argument("--list", action="store_true", help="show what would be copied")
    args = ap.parse_args()
    for body, stem in sorted(BODIES.items()):
        source = best_source(args.src, stem)
        if not source:
            print(f"{body}: no Celestia texture")
            continue
        dest = os.path.join(args.out, body + ".jpg")
        print(f"{body}: {os.path.relpath(source, args.src)} -> {os.path.relpath(dest, args.out)}")
        if not args.list:
            shutil.copyfile(source, dest)


if __name__ == "__main__":
    main()
