#!/usr/bin/env python3
"""Unpack an Orbiter planetary texture archive (.tree) into a tile pyramid that simlib's PlanetTiles streams.

An Orbiter tile is already a complete DDS image, so the tiles are written out unchanged:
    <out>/level<N>/tx_<column>_<row>.dds     (Orbiter level N + 4 is pyramid level N: level 4 is 1 x 2 tiles)
    <out>/tiles.ctx                          (a Celestia virtual texture descriptor)
Column 0 starts at longitude -180 and row 0 is the north, as in the Orbiter archive.

    orbiter_tiles.py <file.tree> <out dir> [--min-level 4] [--max-level 10] [--skip-clear]

--skip-clear leaves out DXT5 tiles whose alpha is zero everywhere (cloud tiles over clear sky) when the next coarser tile is clear
too, which keeps a cloud pyramid small; a patch with no tile of its own uses the coarser one.
"""
import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from orbiter_import import NONE, Tree, block_bytes, parse_tile  # noqa: E402

CTX = 'VirtualTexture\n{\n    ImageDirectory "."\n    BaseSplit 0\n    TileSize 512\n    TileType "dds"\n}\n'


def is_clear(data):
    """True when every alpha value of a DXT5 tile is 0 (alpha endpoints and all indices say so)."""
    _, _, fourcc, blocks = parse_tile(data)
    if fourcc != b"DXT5":
        return False
    arr = np.frombuffer(blocks, np.uint8).reshape(-1, 16)
    # alpha0 == alpha1 == 0 makes every index decode to 0.
    return bool((arr[:, 0] == 0).all() and (arr[:, 1] == 0).all())


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("tree")
    ap.add_argument("out")
    ap.add_argument("--min-level", type=int, default=4)
    ap.add_argument("--max-level", type=int, default=10)
    ap.add_argument("--skip-clear", action="store_true")
    args = ap.parse_args()

    tree = Tree(args.tree)
    os.makedirs(args.out, exist_ok=True)
    clear = {}
    written = skipped = 0
    for level in range(args.min_level, args.max_level + 1):
        folder = os.path.join(args.out, f"level{level - 4}")
        rows, cols = 1 << (level - 4), 1 << (level - 3)
        count = 0
        for row in range(rows):
            for col in range(cols):
                idx = tree.index(level, row, col)
                data = tree.read(idx) if idx != NONE else None
                if data is None:
                    continue
                if args.skip_clear:
                    clear[(level, row, col)] = is_clear(data)
                    # The coarser tile is clear here too, so a patch with no tile of its own shows the same nothing.
                    if clear[(level, row, col)] and clear.get((level - 1, row // 2, col // 2)):
                        skipped += 1
                        continue
                os.makedirs(folder, exist_ok=True)
                with open(os.path.join(folder, f"tx_{col}_{row}.dds"), "wb") as f:
                    f.write(data)
                count += 1
                written += 1
        print(f"level {level - 4}: {count} tiles", flush=True)
        if count == 0 and level > args.min_level:
            break
    with open(os.path.join(args.out, "tiles.ctx"), "w") as f:
        f.write(CTX)
    print(f"{written} tiles written, {skipped} clear tiles skipped -> {args.out}")


if __name__ == "__main__":
    main()
