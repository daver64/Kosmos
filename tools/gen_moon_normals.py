#!/usr/bin/env python3
"""Make tangent-space normal maps for airless moons from their colour textures: data/textures/<body>_normal.jpg.

The relief is faked. Craters and ridges are dark and bright in the image, so a high-passed copy of the brightness is
used as a height field (the broad albedo patterns are filtered out so a dark hemisphere does not look like a slope),
and fractal noise fills in the roughness a 1-4k image cannot show. It only shades; the surface stays a smooth sphere.

Usage:
    gen_moon_normals.py [body ...] [--size 2048] [--relief 1.0]
"""
import argparse
import os
import zlib

import numpy as np
from PIL import Image, ImageFilter

# Body -> (relief strength, noise strength); airless, cratered bodies read best.
BODIES = {
    "mimas": (1.0, 0.5), "tethys": (0.9, 0.5), "dione": (1.0, 0.5), "rhea": (1.0, 0.5), "iapetus": (0.8, 0.6),
    "phoebe": (1.0, 0.6), "enceladus": (0.7, 0.4), "mercury": (1.0, 0.5), "deimos": (0.8, 0.6), "phobos": (0.9, 0.6),
    "callisto": (1.0, 0.5), "ganymede": (0.7, 0.5), "hyperion": (0.8, 0.6), "miranda": (0.9, 0.5),
}


def value_noise(width, height, cells_x, rng):
    """Seamless in x, bilinear value noise with cells_x cells around."""
    cells_y = max(2, cells_x // 2)
    grid = rng.random((cells_y + 2, cells_x + 1))
    grid[:, cells_x] = grid[:, 0]
    ys = np.linspace(0, cells_y, height, endpoint=False)
    xs = np.linspace(0, cells_x, width, endpoint=False)
    y0 = ys.astype(int)
    x0 = xs.astype(int)
    fy = (ys - y0)[:, None]
    fx = (xs - x0)[None, :]
    fy = fy * fy * (3 - 2 * fy)
    fx = fx * fx * (3 - 2 * fx)
    a = grid[y0][:, x0]
    b = grid[y0][:, x0 + 1]
    c = grid[y0 + 1][:, x0]
    d = grid[y0 + 1][:, x0 + 1]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy


def fbm(width, height, rng, base=64, octaves=5):
    total = np.zeros((height, width))
    amplitude = 1.0
    for o in range(octaves):
        total += amplitude * (value_noise(width, height, base * (2 ** o), rng) - 0.5)
        amplitude *= 0.5
    return total


def make(src, out, size, relief, noise):
    width, height = size, size // 2
    image = Image.open(src).convert("L").resize((width, height), Image.LANCZOS)
    lum = np.asarray(image, dtype=np.float64) / 255.0
    broad = np.asarray(image.filter(ImageFilter.GaussianBlur(max(2, width // 256))), dtype=np.float64) / 255.0
    high = lum - broad
    rng = np.random.default_rng(zlib.crc32(os.path.basename(out).encode()))
    h = high * relief * 8.0 + fbm(width, height, rng) * noise * 0.6
    lat = (np.arange(height) + 0.5) / height * np.pi - np.pi / 2
    stretch = np.maximum(np.cos(lat), 0.2)[:, None]
    gx = (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) * 0.5 / stretch
    gy = np.gradient(h, axis=0)
    nx, ny, nz = -gx * 6.0, gy * 6.0, np.ones_like(h)
    length = np.sqrt(nx * nx + ny * ny + nz * nz)
    rgb = np.stack([nx / length, ny / length, nz / length], axis=-1) * 0.5 + 0.5
    Image.fromarray((rgb * 255 + 0.5).astype(np.uint8)).save(out, quality=92)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser()
    ap.add_argument("bodies", nargs="*")
    ap.add_argument("--dir", default=os.path.join(here, "..", "data", "textures"))
    ap.add_argument("--size", type=int, default=2048)
    ap.add_argument("--relief", type=float, default=1.0)
    args = ap.parse_args()
    for name in args.bodies or sorted(BODIES):
        src = os.path.join(args.dir, name + ".jpg")
        if not os.path.exists(src):
            print(f"{name}: no {src}")
            continue
        relief, noise = BODIES.get(name, (1.0, 0.5))
        out = os.path.join(args.dir, name + "_normal.jpg")
        make(src, out, args.size, relief * args.relief, noise)
        print(f"{name}: {out}")


if __name__ == "__main__":
    main()
