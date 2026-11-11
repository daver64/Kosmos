#!/usr/bin/env python3
"""Verify the external view stays steady across Earth's sphere of influence."""

import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageChops, ImageStat

root = Path(__file__).resolve().parents[1]
binary = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build/kosmos"
solar = json.loads((root / "data/sol.json").read_text())
earth = next(body for body in solar["bodies"] if body["name"] == "Earth")
sun = next(body for body in solar["bodies"] if body["name"] == "Sun")
soi = earth["orbit"]["a_au"] * 149597870700.0 * (earth["gm"] / sun["gm"]) ** 0.4

with tempfile.TemporaryDirectory() as home:
    temp = Path(home)
    fixture = temp / "root"
    fixture.mkdir()
    for name in ("addons", "scenarios"):
        (fixture / name).symlink_to(root / name, target_is_directory=True)
    data = fixture / "data"
    data.mkdir()
    for entry in (root / "data").iterdir():
        if entry.name not in ("vsop87", "textures-hires"):
            (data / entry.name).symlink_to(entry, target_is_directory=entry.is_dir())
    scenario_path = temp / "scenario.json"
    env = dict(os.environ, HOME=home, XDG_CONFIG_HOME=str(temp / "config"),
               XDG_CACHE_HOME=str(temp / "cache"), SDL_VIDEODRIVER="offscreen",
               SDL_AUDIODRIVER="dummy", LIBGL_ALWAYS_SOFTWARE="1", LP_NUM_THREADS="4")
    for direction, sign in (("Earth -> Sun", 1), ("Sun -> Earth", -1)):
        scenario = {
            "name": "Camera SOI transition",
            "date": "2026-11-14 17:32:17",
            "ships": [{
                "ship": "CVEL Fregat", "body": "Earth", "orbit": {"altitude_km": 200},
                "state": {"r": [soi - sign * 180000, 0, 0], "v": [sign * 60000, 0, 0], "q": [1, 0, 0, 0]},
            }],
        }
        scenario_path.write_text(json.dumps(scenario))
        shots = []
        for when in (1, 6):
            shot = temp / f"{sign}-{when}.png"
            result = subprocess.run(
                [str(binary), "--root", str(fixture), "--scenario-file", str(scenario_path),
                 "--cam-pitch", "1.2", "--hide-info", "--no-clutter", "--no-clouds", "--no-tiles",
                 "--gen-threads", "1", "--perf", "--shot", str(shot), "--shot-at", str(when)],
                cwd=root, env=env, capture_output=True, text=True, timeout=120,
            )
            assert result.returncode == 0 and shot.is_file(), result.stdout + result.stderr
            output = result.stdout + result.stderr
            transitioned = f"[perf] orbit camera SOI {direction}" in output
            assert transitioned == (when == 6), output
            with Image.open(shot) as image:
                shots.append(image.convert("RGB"))
        difference = ImageChops.difference(shots[0], shots[1])
        mean = sum(ImageStat.Stat(difference.crop((0, 40, 1280, 650))).mean) / 3
        print(f"camera_soi_render_test {direction}: average image difference {mean:.3f}")
        assert mean < 2.0, mean
