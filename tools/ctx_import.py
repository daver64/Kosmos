#!/usr/bin/env python3
"""Stitch a Celestia virtual texture (.ctx plus levelN/tx_<col>_<row>.<ext> tiles) into one equirectangular image.

Celestia splits very large planet maps into tiles. Level 0 is 2 x 1 tiles (with BaseSplit 0) and every level doubles
both directions; column 0 starts at longitude -180 and row 0 is the north. Kosmos draws one texture per body, so this
picks the deepest level whose stitched width fits --max-width (16384 suits most GPUs) and writes it to
data/textures-hires/<body>.jpg. Runtime streaming of the full-resolution tiles is not supported yet.

Usage:
    ctx_import.py PATH/TO/name.ctx BODY [--max-width 16384] [--level N] [--out data/textures-hires] [--quality 92] [--list]

Example:
    ctx_import.py "data/textures/celestia/hires/earth/JMII DDS.ctx" earth
"""
import argparse
import os
import re
import sys

from PIL import Image

Image.MAX_IMAGE_PIXELS = None


def parse_ctx(path):
    text = open(path, encoding="utf-8", errors="replace").read()

    def field(name, default=None):
        m = re.search(r'\b' + name + r'\s+"?([^"\s}]+(?: [^"\s}]+)*)"?', text)
        return m.group(1) if m else default

    d = field("ImageDirectory")
    if d is None:
        sys.exit("no ImageDirectory in " + path)
    # ImageDirectory is quoted and may contain spaces.
    m = re.search(r'ImageDirectory\s+"([^"]+)"', text)
    d = m.group(1) if m else d
    return d, int(field("BaseSplit", "0")), (field("TileType", "dds") or "dds").lower()


def levels_available(root, ext):
    out = {}
    for name in os.listdir(root):
        m = re.fullmatch(r"level(\d+)", name)
        if m:
            out[int(m.group(1))] = os.path.join(root, name)
    return dict(sorted(out.items()))


def tile_path(folder, col, row, ext):
    return os.path.join(folder, "tx_%d_%d.%s" % (col, row, ext))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ctx")
    ap.add_argument("body")
    ap.add_argument("--max-width", type=int, default=16384, help="largest stitched width to produce")
    ap.add_argument("--level", type=int, help="use this level instead of picking by --max-width")
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data", "textures-hires"))
    ap.add_argument("--quality", type=int, default=92)
    ap.add_argument("--list", action="store_true", help="show the levels and their sizes, then stop")
    args = ap.parse_args()

    directory, split, ext = parse_ctx(args.ctx)
    base = os.path.dirname(os.path.abspath(args.ctx))
    root = os.path.join(base, directory)
    # Some packs name an ImageDirectory that was renamed; fall back to level folders beside the .ctx.
    if not os.path.isdir(root):
        root = base
    levels = levels_available(root, ext)
    if not levels:
        sys.exit("no levelN folders under " + root)

    # The real tile size comes from a file, since the descriptor's TileSize is not always right.
    first = min(levels)
    probe = tile_path(levels[first], 0, 0, ext)
    if not os.path.exists(probe):
        sys.exit("missing " + probe)
    with Image.open(probe) as im:
        tile = im.size[0]

    def grid(level):
        rows = 1 << (level + split)
        return rows * 2, rows

    sizes = {lv: (grid(lv)[0] * tile, grid(lv)[1] * tile) for lv in levels}
    for lv, (w, h) in sizes.items():
        print("level %d: %d x %d (%d tiles)" % (lv, w, h, grid(lv)[0] * grid(lv)[1]))
    if args.list:
        return

    if args.level is not None:
        level = args.level
        if level not in levels:
            sys.exit("level %d not present" % level)
    else:
        fit = [lv for lv, (w, _) in sizes.items() if w <= args.max_width]
        if not fit:
            sys.exit("even level %d is wider than --max-width" % first)
        level = max(fit)

    cols, rows = grid(level)
    w, h = sizes[level]
    print("stitching level %d into %d x %d" % (level, w, h))
    canvas = Image.new("RGB", (w, h))
    missing = 0
    for r in range(rows):
        for c in range(cols):
            p = tile_path(levels[level], c, r, ext)
            if not os.path.exists(p):
                missing += 1
                continue
            with Image.open(p) as im:
                canvas.paste(im.convert("RGB"), (c * tile, r * tile))
        if r % 4 == 3 or r == rows - 1:
            print("  row %d / %d" % (r + 1, rows), flush=True)
    if missing:
        print("warning: %d tiles missing (left black)" % missing)

    os.makedirs(args.out, exist_ok=True)
    out = os.path.join(args.out, args.body.lower() + ".jpg")
    canvas.save(out, quality=args.quality, subsampling=0)
    print("wrote", out, "%.1f MB" % (os.path.getsize(out) / 1e6))


if __name__ == "__main__":
    main()
