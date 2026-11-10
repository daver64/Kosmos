#!/usr/bin/env python3
"""Check that the same moon gets dimmer as its distance from the Sun increases."""

import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageStat

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/kosmos").resolve()

with tempfile.TemporaryDirectory() as directory:
    temp = Path(directory)
    root = temp / "root"
    root.mkdir()
    for name in ("addons", "scenarios"):
        (root / name).symlink_to(ROOT / name, target_is_directory=True)
    data = root / "data"
    data.mkdir()
    for entry in (ROOT / "data").iterdir():
        if entry.name not in ("sol.json", "textures-hires", "vsop87"):
            (data / entry.name).symlink_to(entry, target_is_directory=entry.is_dir())

    solar_system = json.loads((ROOT / "data/sol.json").read_text())
    jupiter = next(body for body in solar_system["bodies"] if body["name"] == "Jupiter")
    jupiter["orbit"]["e"] = 0.0
    env = dict(os.environ, HOME=str(temp), XDG_CONFIG_HOME=str(temp / "config"),
               SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy", LP_NUM_THREADS="4")
    readings = []
    for distance in (1.0, 5.2, 30.0):
        jupiter["orbit"]["a_au"] = distance
        (data / "sol.json").write_text(json.dumps(solar_system))
        shot = temp / f"europa-{distance}.png"
        args = [str(BINARY), "--root", str(root), "--scenario", "europa_luna_e3",
                "--date", "2000-01-01 12:00:00", "--cam-planet", "3", "--cam-phase", "0",
                "--hide-info", "--no-clutter", "--no-clouds", "--gen-threads", "1",
                "--shot", str(shot), "--shot-at", "2"]
        result = subprocess.run(args, cwd=root, env=env, capture_output=True, text=True, timeout=120)
        assert result.returncode == 0 and shot.is_file(), result.stdout + result.stderr
        with Image.open(shot) as image:
            x, y = image.width // 2, image.height // 2
            half = image.height // 8
            readings.append(sum(ImageStat.Stat(image.convert("RGB").crop(
                (x - half, y - half, x + half, y + half))).mean) / 3)
    near, jupiter, distant = readings
    assert 20 < distant < jupiter * 0.7 and jupiter < near * 0.65, readings
    print(f"Europa lighting at 1, 5.2 and 30 AU: {readings}")
