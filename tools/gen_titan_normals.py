#!/usr/bin/env python3
"""Generate subtle tangent-space normal maps from Titan texture atlases."""

import zlib
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter


TEXTURES = (
    "MOLSide.png",
    "PLFtitan.png",
    "titan21m.png",
    "titan22m.png",
    "Titanstage2.png",
    "TITAN4stg1.png",
    "Transtag.png",
    "TITAN2I.png",
    "tit3-hull0.png",
    "UA1207.png",
    "SRMU.png",
    "SSMEC.png",
)


def periodic_noise(width, height, cells, rng):
    grid = rng.random((cells, cells))
    x = np.arange(width, dtype=np.float64) * cells / width
    y = np.arange(height, dtype=np.float64) * cells / height
    x0, y0 = x.astype(int), y.astype(int)
    fx, fy = x - x0, y - y0
    fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    a = grid[y0[:, None] % cells, x0[None, :] % cells]
    b = grid[y0[:, None] % cells, (x0[None, :] + 1) % cells]
    c = grid[(y0[:, None] + 1) % cells, x0[None, :] % cells]
    d = grid[(y0[:, None] + 1) % cells, (x0[None, :] + 1) % cells]
    return (a * (1 - fx) + b * fx) * (1 - fy[:, None]) + (c * (1 - fx) + d * fx) * fy[:, None]


def make_normal(source, destination):
    image = Image.open(source).convert("L")
    height = np.asarray(image, dtype=np.float64) / 255.0
    broad = np.asarray(image.filter(ImageFilter.GaussianBlur(max(1, min(image.size) // 64))), dtype=np.float64) / 255.0
    detail = height - broad

    seed = zlib.crc32(source.name.encode())
    noise = sum(
        (periodic_noise(image.width, image.height, cells, np.random.default_rng(seed + cells)) - 0.5) / cells
        for cells in (8, 16, 32, 64)
    )
    relief = detail * 0.04 + noise * 0.8
    dx = (np.roll(relief, -1, axis=1) - np.roll(relief, 1, axis=1)) * 0.5
    dy = (np.roll(relief, -1, axis=0) - np.roll(relief, 1, axis=0)) * 0.5
    nx, ny, nz = -dx * 4.0, dy * 4.0, np.ones_like(relief)
    length = np.sqrt(nx * nx + ny * ny + nz * nz)
    rgb = np.stack((nx / length, ny / length, nz / length), axis=-1) * 0.5 + 0.5
    Image.fromarray((rgb * 255 + 0.5).astype(np.uint8)).save(destination)


def main():
    textures = Path(__file__).resolve().parents[1] / "addons" / "cvel-titan" / "models" / "textures"
    for name in TEXTURES:
        source = textures / name
        destination = textures / f"{source.stem}_normal.png"
        make_normal(source, destination)
        print(destination)


if __name__ == "__main__":
    main()
