#!/usr/bin/env python3
"""Exercise Earth clouds at lunar distance with a nearby body's depth plane."""

import argparse
import json
import os
import subprocess
import tempfile
from pathlib import Path

from PIL import Image, ImageChops, ImageFilter, ImageStat

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("binary", nargs="?", default=ROOT / "build/kosmos")
parser.add_argument("--tiled", action="store_true", help="also use the locally installed high-resolution cloud tiles")
options = parser.parse_args()
BINARY = Path(options.binary).resolve()

with tempfile.TemporaryDirectory() as directory:
    temp = Path(directory)
    root = temp / "root"
    root.mkdir()
    for name in ("addons", "scenarios"):
        (root / name).symlink_to(ROOT / name, target_is_directory=True)
    data = root / "data"
    data.mkdir()
    for entry in (ROOT / "data").iterdir():
        if entry.name not in ("sol.json", "vsop87", "textures-hires"):
            (data / entry.name).symlink_to(entry, target_is_directory=entry.is_dir())
    if options.tiled:
        assert (ROOT / "data/textures-hires/earth_clouds").is_dir(), "Earth cloud tiles are not installed"
        (data / "textures-hires").symlink_to(ROOT / "data/textures-hires", target_is_directory=True)
    system = json.loads((ROOT / "data/sol.json").read_text())
    earth = next(body for body in system["bodies"] if body["name"] == "Earth")
    earth["orbit"] = {"a_au": 1.0, "e": 0.0, "i": 0.0, "raan": 0.0, "argp": 0.0, "m0": 0.0}
    marker = {
        "name": "Depth test moon", "kind": "moon", "parent": "Earth",
        "gm": 4.9e12, "radius": 1737000.0,
        "orbit": {"a_km": (60 * earth["radius"] + 1937000) / 1000, "e": 0,
                  "i": 0, "raan": 0, "argp": 0, "m0": 180},
    }
    env = dict(os.environ, HOME=directory, XDG_CONFIG_HOME=str(temp / "config"),
               XDG_CACHE_HOME=str(temp / "cache"), SDL_VIDEODRIVER="offscreen",
               SDL_AUDIODRIVER="dummy", LP_NUM_THREADS="4")
    images = []
    for nearby, clouds in ((False, True), (True, True), (True, False)):
        if nearby and marker not in system["bodies"]:
            system["bodies"].append(marker)
        (data / "sol.json").write_text(json.dumps(system))
        shot = temp / f"earth-{nearby}-{clouds}.png"
        args = [str(BINARY), "--root", str(root), "--scenario", "dragonfly_orbit",
                "--date", "2000-01-01 12:00:00", "--cam-planet", "60", "--cam-phase", "0",
                "--cam-fov", "5", "--hide-info", "--no-clutter",
                "--gen-threads", "1", "--shot", str(shot), "--shot-at", "2"]
        if not options.tiled:
            args.append("--no-tiles")
        if not clouds:
            args.append("--no-clouds")
        result = subprocess.run(args, cwd=root, env=env, capture_output=True, text=True, timeout=120)
        assert result.returncode == 0 and shot.is_file(), result.stdout + result.stderr
        with Image.open(shot) as image:
            images.append(image.convert("RGB"))
    difference = ImageChops.subtract(images[1].convert("L"), images[2].convert("L"))
    surrounding = difference.filter(ImageFilter.BoxBlur(5))
    x, y = difference.width // 2, difference.height // 2
    half = 110
    region = (x - half, y - half, x + half, y + half)
    values = difference.crop(region).getdata()
    neighbours = surrounding.crop(region).getdata()
    holes = sum(value < 8 and nearby > 35 for value, nearby in zip(values, neighbours)) / (4 * half * half)
    mean = ImageStat.Stat(difference.crop(region)).mean[0]
    reference = ImageStat.Stat(ImageChops.subtract(images[0].convert("L"), images[2].convert("L")).crop(region)).mean[0]
    print(f"Lunar-distance Earth clouds: mean difference {mean:.2f}, reference {reference:.2f}, holes {holes:.3%}", flush=True)
    assert mean > 10 and holes < 0.03, (mean, holes)
    assert mean > reference * 0.95, (mean, reference)
