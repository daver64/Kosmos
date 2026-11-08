#!/usr/bin/env python3
"""Compare Earth across the high-altitude cloud transition using off-screen rendering."""

import os
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageChops, ImageFilter, ImageStat

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "build/kosmos"


def mean_centre(path, radius):
    with Image.open(path) as image:
        image = image.convert("RGB")
        x, y = image.width // 2, image.height // 2
        half = max(3, int(image.height / 8 * 4.15 / radius))
        return sum(ImageStat.Stat(image.crop((x - half, y - half, x + half, y + half))).mean) / 3


def cloud_hole_fraction(cloudy_path, clear_path):
    with Image.open(cloudy_path) as cloudy, Image.open(clear_path) as clear:
        difference = ImageChops.subtract(cloudy.convert("L"), clear.convert("L"))
        surrounding = difference.filter(ImageFilter.BoxBlur(5))
        x, y = difference.width // 2, difference.height // 2
        half = int(difference.height * 0.194)
        region = (x - half, y - half, x + half, y + half)
        values = difference.crop(region).getdata()
        neighbours = surrounding.crop(region).getdata()
        # A surface-coloured pixel inside an otherwise bright cloud is a likely depth hole.
        return sum(value < 8 and nearby > 35 for value, nearby in zip(values, neighbours)) / (4 * half * half)


with tempfile.TemporaryDirectory() as directory:
    temp = Path(directory)
    env = dict(os.environ, HOME=directory, XDG_CONFIG_HOME=str(temp / "config"),
               SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy", LP_NUM_THREADS="4")
    for tiled in (True, False):
        readings = []
        for radius, clouds in ((4.13, True), (4.15, True), (4.15, False), (60, True), (60, False)):
            output = temp / f"{tiled}-{radius}-{clouds}.png"
            args = [str(BINARY), "--root", str(ROOT), "--scenario", "dragonfly_orbit",
                    "--cam-planet", str(radius), "--cam-phase", "0", "--hide-info", "--no-clutter",
                    "--date", "2024-06-21 15:00:00", "--shot", str(output), "--shot-at", "2"]
            if not tiled:
                args.append("--no-tiles")
            if not clouds:
                args.append("--no-clouds")
            result = subprocess.run(args, cwd=ROOT, env=env, capture_output=True, text=True, timeout=120)
            assert result.returncode == 0 and output.is_file(), result.stdout + result.stderr
            readings.append(mean_centre(output, radius))
        below, above, clear, far, far_clear = readings
        assert abs(below - above) < max(below * 0.15, 5), (tiled, readings)
        assert above - clear > 10, (tiled, readings)
        assert far - far_clear > 10, (tiled, readings)
        holes = cloud_hole_fraction(temp / f"{tiled}-4.15-True.png", temp / f"{tiled}-4.15-False.png")
        # The collapsed shell produces over 7% holes in this view; natural cloud gaps
        # remain below 3% with the shell at its intended height.
        assert holes < 0.03, (tiled, holes)
        print(f"Earth clouds, {'tiled' if tiled else 'single-image'}: brightness {readings}, holes {holes:.3%}")
