#!/usr/bin/env python3
"""Procedural equirectangular surface maps for the Sol bodies, written as BC1 (DXT1) .dds files.

Usage: gen_textures.py [--out DIR] [--scale N] [body ...]
Each body has its own default width (Earth, Moon and Mars are 16384x8192); --scale 2 halves everything,
--scale 4 quarters it, so a quick test is cheap. The maps are built from noise on the unit sphere, so
they wrap at the date line and do not pinch at the poles.
"""
import argparse
import os
import struct
import sys

import numpy as np

LATTICE = 128
rng = np.random.default_rng(7)
LAT = rng.random((LATTICE, LATTICE, LATTICE), dtype=np.float32)


def value_noise(x, y, z):
    xi, yi, zi = np.floor(x).astype(np.int32), np.floor(y).astype(np.int32), np.floor(z).astype(np.int32)
    fx, fy, fz = x - xi, y - yi, z - zi
    fx, fy, fz = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy), fz * fz * (3 - 2 * fz)
    m = LATTICE - 1
    x0, x1, y0, y1, z0, z1 = xi & m, (xi + 1) & m, yi & m, (yi + 1) & m, zi & m, (zi + 1) & m
    c00 = LAT[x0, y0, z0] * (1 - fx) + LAT[x1, y0, z0] * fx
    c10 = LAT[x0, y1, z0] * (1 - fx) + LAT[x1, y1, z0] * fx
    c01 = LAT[x0, y0, z1] * (1 - fx) + LAT[x1, y0, z1] * fx
    c11 = LAT[x0, y1, z1] * (1 - fx) + LAT[x1, y1, z1] * fx
    return (c00 * (1 - fy) + c10 * fy) * (1 - fz) + (c01 * (1 - fy) + c11 * fy) * fz


def fbm(p, freq, octaves, gain=0.5, offset=0.0):
    x, y, z = p
    total = np.zeros(x.shape, np.float32)
    amp, norm = 1.0, 0.0
    for o in range(octaves):
        f = freq * (2.0 ** o)
        total += amp * value_noise(x * f + offset + 13.7 * o, y * f + 5.1 * o, z * f + 29.3 * o)
        norm += amp
        amp *= gain
    return total / norm


def mix(a, b, t):
    t = np.clip(t, 0, 1)[..., None]
    return a * (1 - t) + b * t


def col(r, g, b):
    return np.array([r, g, b], np.float32) / 255.0


def ramp(t, stops):
    out = np.zeros(t.shape + (3,), np.float32)
    out[:] = stops[0][1]
    for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
        w = np.clip((t - t0) / (t1 - t0), 0, 1)
        out = np.where((t >= t0)[..., None], mix(c0, c1, w), out)
    return out


def earth(p, lat, lon):
    h = (fbm(p, 1.3, 9, 0.52) - 0.5) * 2.0
    sea = 0.14
    moisture = fbm(p, 2.4, 5, offset=71.0)
    temp = 1.0 - np.abs(lat) / (np.pi / 2) + (fbm(p, 4.0, 3, offset=3.0) - 0.5) * 0.25
    depth = np.clip((sea - h) / 0.6, 0, 1)
    ocean = ramp(depth, [(0.0, col(60, 140, 170)), (0.15, col(25, 90, 150)), (1.0, col(8, 28, 80))])
    land_h = np.clip((h - sea) / 0.5, 0, 1)
    desert = np.clip((0.5 - moisture) * 3.0, 0, 1) * np.clip(temp * 1.6 - 0.45, 0, 1)
    green = mix(col(40, 105, 45), col(95, 130, 55), 1 - temp)
    land = mix(green, col(205, 175, 115), desert)
    land = mix(land, col(110, 95, 80), np.clip(land_h * 2.2 - 0.6, 0, 1))
    land = mix(land, col(245, 247, 250), np.clip((land_h - 0.8) * 6, 0, 1))
    out = np.where((h > sea)[..., None], land, ocean)
    return mix(out, col(240, 246, 252), np.clip((0.22 - temp) * 9.0, 0, 1))


def moon(p, lat, lon, tone=(168, 166, 162), maria=0.35, dark=(70, 70, 74)):
    base = fbm(p, 6.0, 10, 0.55)
    patches = fbm(p, 1.1, 4, offset=40.0)
    m = np.clip((patches - 0.52) * 7.0, 0, 1) * maria * 2.6
    rough = fbm(p, 60.0, 3, 0.6)
    light = mix(col(*tone) * (0.6 + 0.6 * base[..., None]), col(*dark) * (0.7 + 0.5 * base[..., None]), m)
    return np.clip(light * (0.9 + 0.2 * rough[..., None]), 0, 1)


def mars(p, lat, lon):
    base = fbm(p, 3.0, 9, 0.55)
    dark = fbm(p, 1.4, 4, offset=11.0)
    c = mix(col(196, 110, 70), col(150, 82, 52), base)
    c = mix(c, col(95, 70, 60), np.clip((dark - 0.55) * 5, 0, 1) * 0.8)
    c = mix(c, col(225, 160, 110), np.clip((base - 0.65) * 4, 0, 1) * 0.5)
    return mix(c, col(245, 240, 240), np.clip((np.abs(lat) - 1.38) * 14.0, 0, 1))


def mercury(p, lat, lon):
    return moon(p, lat, lon, tone=(150, 143, 136), maria=0.15, dark=(95, 90, 88))


def venus(p, lat, lon):
    warp = fbm(p, 2.0, 4, offset=5.0)
    swirl = fbm((p[0] + warp * 0.6, p[1] * 3.0, p[2] + warp * 0.6), 2.5, 6, 0.6)
    return mix(col(214, 180, 110), col(246, 228, 172), swirl)


def bands(p, lat, stops, count, turb):
    warp = (fbm(p, 3.0, 6, 0.55, offset=2.0) - 0.5) * turb
    t = 0.5 + 0.5 * np.sin(lat * count + warp * 9.0)
    fine = fbm((p[0] * 0.2, p[1] * 8.0, p[2] * 0.2), 6.0, 4, 0.6)
    return ramp(np.clip(t * 0.8 + fine * 0.2, 0, 1), stops)


def jupiter(p, lat, lon):
    c = bands(p, lat, [(0, col(150, 110, 80)), (0.4, col(205, 165, 125)), (0.7, col(236, 222, 200)), (1, col(250, 240, 225))], 17.0, 0.5)
    spot = np.exp(-(((lon - 2.4) / 0.28) ** 2 + ((lat + 0.38) / 0.12) ** 2))
    return mix(c, col(190, 85, 60), spot * 0.9)


def saturn(p, lat, lon):
    return bands(p, lat, [(0, col(180, 150, 100)), (0.5, col(215, 192, 140)), (1, col(238, 222, 175))], 13.0, 0.25)


def uranus(p, lat, lon):
    return bands(p, lat, [(0, col(120, 195, 205)), (1, col(170, 225, 230))], 7.0, 0.08)


def neptune(p, lat, lon):
    c = bands(p, lat, [(0, col(40, 80, 190)), (0.6, col(60, 110, 215)), (1, col(120, 160, 235))], 9.0, 0.35)
    spot = np.exp(-(((lon + 1.0) / 0.3) ** 2 + ((lat + 0.3) / 0.14) ** 2))
    return mix(c, col(20, 40, 120), spot * 0.8)


BODIES = {
    "earth": (earth, 16384),
    "moon": (moon, 16384),
    "mars": (mars, 16384),
    "mercury": (mercury, 8192),
    "venus": (venus, 4096),
    "jupiter": (jupiter, 8192),
    "saturn": (saturn, 4096),
    "uranus": (uranus, 2048),
    "neptune": (neptune, 2048),
}


def encode_bc1(rgb):
    """rgb: (h, w, 3) floats 0..1, h and w multiples of 4 -> BC1 blocks in row-major block order."""
    h, w, _ = rgb.shape
    px = (np.clip(rgb, 0, 1) * 255.0 + 0.5).astype(np.int32)
    blocks = px.reshape(h // 4, 4, w // 4, 4, 3).transpose(0, 2, 1, 3, 4).reshape(-1, 16, 3)
    lo, hi = blocks.min(axis=1), blocks.max(axis=1)

    def to565(c):
        return ((c[:, 0] >> 3) << 11) | ((c[:, 1] >> 2) << 5) | (c[:, 2] >> 3)

    def from565(v):
        r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
        return np.stack([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)], axis=1)

    c0, c1 = to565(hi), to565(lo)
    c0, c1 = np.maximum(c0, c1), np.minimum(c0, c1)
    # Equal endpoints would select the 3-colour mode; a flat block uses index 0 so it is harmless.
    p0, p1 = from565(c0), from565(c1)
    palette = np.stack([p0, p1, (2 * p0 + p1) // 3, (p0 + 2 * p1) // 3], axis=1)
    d = ((blocks[:, :, None, :] - palette[:, None, :, :]) ** 2).sum(axis=3)
    idx = d.argmin(axis=2).astype(np.uint32)
    idx[c0 == c1] = 0
    packed = np.zeros(idx.shape[0], np.uint32)
    for i in range(16):
        packed |= idx[:, i] << (2 * i)
    raw = np.empty((idx.shape[0], 8), np.uint8)
    raw[:, 0], raw[:, 1] = c0 & 255, c0 >> 8
    raw[:, 2], raw[:, 3] = c1 & 255, c1 >> 8
    for k in range(4):
        raw[:, 4 + k] = (packed >> (8 * k)) & 255
    return raw.tobytes()


def dds_header(w, h, levels):
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000 | 0x20000
    head = b"DDS " + struct.pack("<7I", 124, flags, h, w, (w // 4) * (h // 4) * 8, 0, levels)
    head += b"\0" * 44
    head += struct.pack("<2I4s5I", 32, 0x4, b"DXT1", 0, 0, 0, 0, 0)
    head += struct.pack("<5I", 0x1000 | 0x8 | 0x400000, 0, 0, 0, 0)
    return head


def box_down(px):
    """Halve a (h, w, 3) uint8 image with a 2x2 box filter."""
    h, w, _ = px.shape
    s = px.reshape(h // 2, 2, w // 2, 2, 3).astype(np.uint16).sum(axis=(1, 3))
    return ((s + 2) // 4).astype(np.uint8)


def build(name, width, out_dir, mips=True):
    gen = BODIES[name][0]
    w, h = width, width // 2
    levels = 1
    while mips and (w >> levels) >= 4 and (h >> levels) >= 4:
        levels += 1
    path = os.path.join(out_dir, name + ".dds")
    strip = 128
    rest = np.zeros((h // 2, w // 2, 3), np.uint8) if levels > 1 else None
    with open(path, "wb") as f:
        f.write(dds_header(w, h, levels))
        lon = ((np.arange(w, dtype=np.float32) + 0.5) / w * 2 - 1) * np.pi
        for y0 in range(0, h, strip):
            rows = min(strip, h - y0)
            lat = (0.5 - (np.arange(y0, y0 + rows, dtype=np.float32) + 0.5) / h) * np.pi
            LON, LATG = np.meshgrid(lon, lat)
            cl = np.cos(LATG)
            p = (cl * np.cos(LON) * 4.0 + 64.0, np.sin(LATG) * 4.0 + 64.0, cl * np.sin(LON) * 4.0 + 64.0)
            rgb = np.clip(gen(p, LATG, LON).astype(np.float32), 0, 1)
            f.write(encode_bc1(rgb))
            if rest is not None:
                px = (rgb * 255.0 + 0.5).astype(np.uint8)
                rest[y0 // 2:(y0 + rows) // 2] = box_down(px)
            print(f"\r{name}: {w}x{h} {100 * (y0 + rows) // h}%", end="", flush=True)
        level = rest
        for _ in range(1, levels):
            f.write(encode_bc1(level.astype(np.float32) / 255.0))
            if level.shape[0] >= 8 and level.shape[1] >= 8:
                level = box_down(level)
    print(f"\r{name}: {w}x{h} x{levels} mips -> {path} ({os.path.getsize(path) / 1e6:.0f} MB)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data", "textures-hires"))
    ap.add_argument("--scale", type=int, default=1)
    ap.add_argument("--no-mips", action="store_true")
    ap.add_argument("bodies", nargs="*")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    for name in args.bodies or BODIES:
        if name not in BODIES:
            sys.exit(f"unknown body {name}")
        build(name, max(64, BODIES[name][1] // args.scale), args.out, not args.no_mips)


if __name__ == "__main__":
    main()
