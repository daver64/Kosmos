#!/usr/bin/env python3
"""Convert an Orbiter planetary texture archive (Surf.tree) into a single equirectangular Kosmos .dds.

Orbiter stores a planet's surface as a quadtree of zlib-compressed DDS tiles (see ZTreeMgr in the Orbiter source):
level 4 is 1 x 2 tiles (each 180 x 180 degrees) and every level after it doubles both directions, so with 512 px
tiles level 7 is 8192 x 4096 and level 8 is 16384 x 8192. Tile row 0 is the north, tile column 0 the west edge.

The tiles are block compressed (DXT1/DXT5), so they are copied block-for-block into one image with no decode or
re-encode, and the levels of the tree become the mip chain of the output (levels below the tree's level 4 are built
by box filtering for DXT1). Missing tiles are filled with the average colour of the nearest ancestor tile.

    orbiter_import.py <Orbiter install | Textures/Earth | Archive | Surf.tree> [--body earth] [--level 8]
    orbiter_import.py --selftest
"""
import argparse
import os
import struct
import sys
import zlib

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_textures import box_down, encode_bc1  # noqa: E402

NONE = 0xFFFFFFFF
HEADER = struct.Struct("<4sIIIqIIIIII")  # magic, size, flags, dataOfs, dataLength, nodeCount, root1..3, root4[2]
NODE = struct.Struct("<qI4Ixxxx")  # pos, inflated size, child[4], padded to 32 bytes by the C++ struct


class Tree:
    def __init__(self, path):
        self.f = open(path, "rb")
        head = self.f.read(HEADER.size)
        if len(head) != HEADER.size:
            raise ValueError("file too short to be an Orbiter .tree")
        (magic, size, _flags, self.data_ofs, self.data_len, count, r1, r2, r3, r4a, r4b) = HEADER.unpack(head)
        if magic != b"TX\x01\x00" or size != HEADER.size:
            raise ValueError(f"{path} is not an Orbiter .tree archive (magic {magic!r})")
        self.roots = {1: r1, 2: r2, 3: r3}
        self.root4 = [r4a, r4b]
        raw = self.f.read(NODE.size * count)
        if len(raw) != NODE.size * count:
            raise ValueError("truncated node table")
        self.nodes = list(NODE.iter_unpack(raw))

    def index(self, level, ilat, ilng):
        if level <= 3:
            return self.roots.get(level, NONE)
        if level == 4:
            return self.root4[ilng] if ilat == 0 and 0 <= ilng < 2 else NONE
        parent = self.index(level - 1, ilat // 2, ilng // 2)
        if parent == NONE:
            return NONE
        return self.nodes[parent][2 + ((ilat & 1) << 1) + (ilng & 1)]

    def read(self, idx):
        """Inflated tile bytes, or None for a node with no data of its own."""
        if idx == NONE or self.nodes[idx][1] == 0:
            return None
        pos, size = self.nodes[idx][0], self.nodes[idx][1]
        end = self.nodes[idx + 1][0] if idx + 1 < len(self.nodes) else self.data_len
        self.f.seek(self.data_ofs + pos)
        data = zlib.decompress(self.f.read(end - pos))
        if len(data) != size:
            raise ValueError(f"tile {idx} inflated to {len(data)} bytes, expected {size}")
        return data


def parse_tile(data):
    """-> (width, height, fourcc, block bytes) of a DDS tile."""
    if data[:4] != b"DDS ":
        raise ValueError("tile is not a DDS image")
    h, w = struct.unpack_from("<II", data, 12)
    fourcc = data[84:88]
    offset = 128
    if fourcc == b"DX10":
        fmt = struct.unpack_from("<I", data, 128)[0]
        fourcc = {71: b"DXT1", 72: b"DXT1", 77: b"DXT5", 78: b"DXT5"}.get(fmt, b"")
        offset = 148
    if fourcc not in (b"DXT1", b"DXT5"):
        raise ValueError(f"unsupported tile format {fourcc!r}; only DXT1 and DXT5 tiles can be imported")
    return w, h, fourcc, data[offset:]


def block_bytes(fourcc):
    return 8 if fourcc == b"DXT1" else 16


def solid_block(fourcc, rgb):
    r, g, b = (int(c) for c in rgb)
    c565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    colour = struct.pack("<HHI", c565, c565, 0)
    return colour if fourcc == b"DXT1" else b"\xff\xff" + b"\0" * 6 + colour


def average_colour(fourcc, blocks):
    arr = np.frombuffer(blocks, np.uint8).reshape(-1, block_bytes(fourcc))
    col = arr[:, 0 if fourcc == b"DXT1" else 8:][:, :4].astype(np.uint16)
    total = np.zeros(3)
    for k in (0, 2):
        v = col[:, k] | (col[:, k + 1] << 8)
        total += np.stack([((v >> 11) & 31) * 255 / 31, ((v >> 5) & 63) * 255 / 63, (v & 31) * 255 / 31]).mean(axis=1) / 2
    return total


def decode_bc1(blocks, w, h):
    """BC1 blocks (row-major) -> (h, w, 3) uint8."""
    arr = np.frombuffer(blocks, np.uint8).reshape(-1, 8)
    c0 = arr[:, 0].astype(np.int32) | (arr[:, 1].astype(np.int32) << 8)
    c1 = arr[:, 2].astype(np.int32) | (arr[:, 3].astype(np.int32) << 8)
    bits = arr[:, 4].astype(np.uint32) | (arr[:, 5].astype(np.uint32) << 8) | (arr[:, 6].astype(np.uint32) << 16) | (arr[:, 7].astype(np.uint32) << 24)

    def expand(v):
        r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
        return np.stack([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)], axis=1)

    p0, p1 = expand(c0), expand(c1)
    four = (c0 > c1)[:, None]
    p2 = np.where(four, (2 * p0 + p1) // 3, (p0 + p1) // 2)
    p3 = np.where(four, (p0 + 2 * p1) // 3, 0)
    palette = np.stack([p0, p1, p2, p3], axis=1)
    idx = (bits[:, None] >> (2 * np.arange(16, dtype=np.uint32))[None, :]) & 3
    px = np.take_along_axis(palette, idx[:, :, None].astype(np.intp), axis=1)
    px = px.reshape(h // 4, w // 4, 4, 4, 3).transpose(0, 2, 1, 3, 4).reshape(h, w, 3)
    return px.astype(np.uint8)


def dds_header(w, h, levels, fourcc):
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000 | (0x20000 if levels > 1 else 0)
    linear = ((w + 3) // 4) * ((h + 3) // 4) * block_bytes(fourcc)
    head = b"DDS " + struct.pack("<7I", 124, flags, h, w, linear, 0, levels) + b"\0" * 44
    head += struct.pack("<2I4s5I", 32, 0x4, fourcc, 0, 0, 0, 0, 0)
    head += struct.pack("<5I", 0x1000 | (0x8 | 0x400000 if levels > 1 else 0), 0, 0, 0, 0)
    return head


def present_tiles(tree, level):
    nlat, nlng = 1 << (level - 4), 1 << (level - 3)
    return [(i, j) for i in range(nlat) for j in range(nlng) if tree.read(tree.index(level, i, j)) is not None]


def import_tree(tree_path, out_path, level):
    tree = Tree(tree_path)
    while level >= 4 and not present_tiles(tree, level):
        level -= 1
    if level < 4:
        raise ValueError("the archive has no tiles at level 4 or above")
    first = tree.read(tree.index(level, *present_tiles(tree, level)[0]))
    tw, th, fourcc, _ = parse_tile(first)
    bb = block_bytes(fourcc)
    tbx, tby = tw // 4, th // 4
    levels = list(range(level, 3, -1))
    top_w, top_h = (1 << (level - 3)) * tw, (1 << (level - 4)) * th

    tail = 0
    if fourcc == b"DXT1":
        w, h = top_w >> (len(levels) - 1), top_h >> (len(levels) - 1)
        while w >= 8 and h >= 8:
            w, h, tail = w // 2, h // 2, tail + 1

    print(f"{os.path.basename(tree_path)}: level {level} -> {top_w}x{top_h} {fourcc.decode()}, {len(levels)} tree levels + {tail} filtered mips")
    last_blocks = None
    with open(out_path, "wb") as out:
        out.write(dds_header(top_w, top_h, len(levels) + tail, fourcc))
        for lv in levels:
            nlat, nlng = 1 << (lv - 4), 1 << (lv - 3)
            keep = [] if lv == levels[-1] else None
            for ilat in range(nlat):
                row = np.empty((tby, nlng * tbx, bb), np.uint8)
                for ilng in range(nlng):
                    data = tree.read(tree.index(lv, ilat, ilng))
                    if data is None:
                        colour, anc = np.full(3, 128.0), None
                        a_lv, a_lat, a_lng = lv, ilat, ilng
                        while a_lv > 4 and anc is None:
                            a_lv, a_lat, a_lng = a_lv - 1, a_lat // 2, a_lng // 2
                            anc = tree.read(tree.index(a_lv, a_lat, a_lng))
                        if anc is not None:
                            colour = average_colour(fourcc, parse_tile(anc)[3])
                        tile = np.frombuffer(solid_block(fourcc, colour) * (tbx * tby), np.uint8)
                    else:
                        w, h, fc, blocks = parse_tile(data)
                        if (w, h, fc) != (tw, th, fourcc):
                            raise ValueError(f"level {lv} tile ({ilat},{ilng}) is {w}x{h} {fc!r}; expected {tw}x{th} {fourcc!r}")
                        tile = np.frombuffer(blocks, np.uint8, count=tbx * tby * bb)
                    row[:, ilng * tbx:(ilng + 1) * tbx] = tile.reshape(tby, tbx, bb)
                out.write(row.tobytes())
                if keep is not None:
                    keep.append(row)
            print(f"\r  level {lv}: {nlng * tw}x{nlat * th}", end="", flush=True)
            if keep is not None:
                last_blocks = np.concatenate(keep, axis=0).tobytes()
        if tail:
            w, h = (1 << (levels[-1] - 3)) * tw, (1 << (levels[-1] - 4)) * th
            img = decode_bc1(last_blocks, w, h)
            for _ in range(tail):
                img = box_down(img)
                out.write(encode_bc1(img.astype(np.float32) / 255.0))
    print(f"\n  -> {out_path} ({os.path.getsize(out_path) / 1e6:.1f} MB)")


def find_tree(path, body):
    candidates = [path]
    for base in (path, os.path.join(path, "Textures"), os.path.join(path, "Textures2")):
        if body:
            candidates.append(os.path.join(base, body))
        candidates.append(base)
    for c in candidates:
        for sub in ("", "Archive"):
            for name in ("Surf.tree",):
                p = os.path.join(c, sub, name) if os.path.isdir(c) else c
                if os.path.isfile(p):
                    return p
    return None


def selftest():
    import tempfile

    tile, top = 64, 6
    rng = np.random.default_rng(7)
    images, encoded = {}, {}
    for lv in range(4, top + 1):
        w, h = (1 << (lv - 3)) * tile, (1 << (lv - 4)) * tile
        y, x = np.mgrid[0:h, 0:w].astype(np.float32)
        img = np.stack([(np.sin(x / (9 + lv)) + 1) / 2, (np.cos(y / 7) + 1) / 2, rng.random((h, w), dtype=np.float32) * 0.2 + (lv / 8)], axis=-1)
        images[lv] = np.clip(img, 0, 1)
        encoded[lv] = np.frombuffer(encode_bc1(images[lv]), np.uint8).reshape(h // 4, w // 4, 8)
    missing = (top, 1, 2)
    nodes, index = [], {}
    for lv in range(4, top + 1):
        for i in range(1 << (lv - 4)):
            for j in range(1 << (lv - 3)):
                index[(lv, i, j)] = len(nodes)
                nodes.append({"pos": 0, "size": 0, "child": [NONE] * 4, "key": (lv, i, j)})
    for (lv, i, j), n in index.items():
        if lv > 4:
            nodes[index[(lv - 1, i // 2, j // 2)]]["child"][((i & 1) << 1) + (j & 1)] = n
    blobs, pos = [], 0
    for n in nodes:
        lv, i, j = n["key"]
        n["pos"] = pos
        if n["key"] != missing:
            blk = encoded[lv][i * tile // 4:(i + 1) * tile // 4, j * tile // 4:(j + 1) * tile // 4]
            raw = dds_header(tile, tile, 1, b"DXT1") + blk.tobytes()
            n["size"] = len(raw)
            blob = zlib.compress(raw)
        else:
            blob = b""
        blobs.append(blob)
        pos += len(blob)
    with tempfile.TemporaryDirectory() as d:
        tree_path, out_path = os.path.join(d, "Surf.tree"), os.path.join(d, "out.dds")
        with open(tree_path, "wb") as f:
            ofs = HEADER.size + NODE.size * len(nodes)
            f.write(HEADER.pack(b"TX\x01\x00", HEADER.size, 0, ofs, pos, len(nodes), NONE, NONE, NONE, index[(4, 0, 0)], index[(4, 0, 1)]))
            for n in nodes:
                f.write(NODE.pack(n["pos"], n["size"], *n["child"]))
            for b in blobs:
                f.write(b)
        import_tree(tree_path, out_path, top)
        data = open(out_path, "rb").read()
        assert data[:4] == b"DDS "
        w, h = (1 << (top - 3)) * tile, (1 << (top - 4)) * tile
        assert struct.unpack_from("<II", data, 12) == (h, w)
        levels = struct.unpack_from("<I", data, 28)[0]
        body = np.frombuffer(data, np.uint8, offset=128, count=(w // 4) * (h // 4) * 8).reshape(h // 4, w // 4, 8)
        expect = encoded[top].copy()
        gap = body[missing[1] * tile // 4:(missing[1] + 1) * tile // 4, missing[2] * tile // 4:(missing[2] + 1) * tile // 4]
        expect[missing[1] * tile // 4:(missing[1] + 1) * tile // 4, missing[2] * tile // 4:(missing[2] + 1) * tile // 4] = gap
        assert np.array_equal(body, expect), "assembled tiles differ from the source image"
        assert len(set(map(bytes, gap.reshape(-1, 8)))) == 1, "missing tile should be a flat fill"
        expected_size = 128 + sum(max(1, (w >> k) // 4) * max(1, (h >> k) // 4) * 8 for k in range(levels))
        assert len(data) == expected_size, (len(data), expected_size)
        print(f"selftest ok: {levels} mip levels, tile order, missing-tile fill and file size verified")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("path", nargs="?", help="Orbiter install, Textures/<Planet> directory, Archive directory or Surf.tree")
    ap.add_argument("--body", help="planet folder name (Earth, Mars, ...); also names the output file")
    ap.add_argument("--level", type=int, default=8, help="highest tree level to import (8 = 16384x8192 with 512 px tiles)")
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data", "textures-hires"))
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        selftest()
        return
    if not args.path:
        ap.error("path is required")
    tree = find_tree(args.path, args.body)
    if not tree:
        sys.exit(f"no Surf.tree found under {args.path}")
    body = args.body
    if not body:
        parts = os.path.normpath(tree).split(os.sep)
        body = parts[-3] if len(parts) >= 3 and parts[-2].lower() == "archive" else os.path.splitext(parts[-1])[0]
    os.makedirs(args.out, exist_ok=True)
    import_tree(tree, os.path.join(args.out, body.lower() + ".dds"), args.level)


if __name__ == "__main__":
    main()
