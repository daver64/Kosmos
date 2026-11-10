#!/usr/bin/env python3
"""Verify per-body close-orbit haze in the actual renderer."""

import json
import math
import os
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageChops, ImageStat

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
        if entry.name not in ("sol.json", "textures-hires"):
            (data / entry.name).symlink_to(entry, target_is_directory=entry.is_dir())
    solar_system = json.loads((ROOT / "data/sol.json").read_text())
    env = dict(os.environ, HOME=directory, XDG_CONFIG_HOME=str(temp / "config"),
               XDG_CACHE_HOME=str(temp / "cache"), SDL_VIDEODRIVER="offscreen",
               SDL_AUDIODRIVER="dummy", LP_NUM_THREADS="4")
    scenarios = {}
    for name, altitude in (("Venus", 400), ("Jupiter", 2000), ("Saturn", 2000)):
        scenario = json.loads((ROOT / "scenarios/Planets" / (name.lower() + "_luna_e3.json")).read_text())
        scenario["ships"][0]["orbit"]["altitude_km"] = altitude
        scenarios[name] = temp / (name.lower() + ".json")
        scenarios[name].write_text(json.dumps(scenario))
    for name in ("Venus", "Jupiter", "Saturn"):
        body = next(item for item in solar_system["bodies"] if item["name"] == name)
        images = []
        for strength in (0.0, 0.8):
            body["atmosphere"]["close_orbit_haze"] = strength
            (data / "sol.json").write_text(json.dumps(solar_system))
            shot = temp / f"{name}-{strength}.png"
            command = [str(BINARY), "--root", str(root), "--scenario-file", str(scenarios[name]),
                       "--date", "2024-06-21 15:00:00", "--hide-info", "--no-clutter",
                       "--no-clouds", "--gen-threads", "1", "--shot", str(shot), "--shot-at", "2"]
            result = subprocess.run(command, cwd=root, env=env, capture_output=True, text=True, timeout=120)
            assert result.returncode == 0 and shot.is_file(), result.stdout + result.stderr
            with Image.open(shot) as image:
                images.append(image.convert("RGB"))
        difference = ImageChops.difference(*images)
        changed = sum(max(pixel) >= 3 for pixel in difference.getdata())
        mean = sum(ImageStat.Stat(difference).mean) / 3
        print(f"{name} close-orbit haze: {changed} changed pixels, mean difference {mean:.3f}", flush=True)
        assert changed > 500 and mean > 0.03, (name, changed, mean)
        if name == "Venus":
            # The old collapsed rings made an abrupt horizontal band across the sky.
            rows = [sum(ImageStat.Stat(difference.crop((80, y, 500, y + 1))).mean) / 3
                    for y in range(80, 320)]
            largest_step = max(abs(a - b) for a, b in zip(rows, rows[1:]))
            print(f"Venus sky maximum row-to-row haze change: {largest_step:.3f}", flush=True)
            assert largest_step < 1.5, largest_step

    venus = next(item for item in solar_system["bodies"] if item["name"] == "Venus")
    atmosphere = venus["atmosphere"]
    dense = min(1.0, math.log10(1.0 + atmosphere["density"]) / math.log10(66.0))
    thickness = max(atmosphere["scale_height"] * 10, venus["radius"] * 0.04) * (3 + 1.4 * dense) * 0.7
    boundary = 1.0 + thickness / venus["radius"]

    def planet_view(strength, radius, label):
        atmosphere["close_orbit_haze"] = strength
        (data / "sol.json").write_text(json.dumps(solar_system))
        shot = temp / f"{label}.png"
        command = [str(BINARY), "--root", str(root), "--scenario-file", str(scenarios["Venus"]),
                   "--date", "2024-06-21 15:00:00", "--cam-planet", str(radius),
                   "--cam-phase", "0", "--hide-info", "--no-clutter", "--no-clouds",
                   "--gen-threads", "1", "--shot", str(shot), "--shot-at", "2"]
        result = subprocess.run(command, cwd=root, env=env, capture_output=True, text=True, timeout=120)
        assert result.returncode == 0 and shot.is_file(), result.stdout + result.stderr
        with Image.open(shot) as image:
            return image.convert("RGB")

    for radius, label in ((1.01, "inside"), (3.0, "far")):
        disabled = planet_view(0.0, radius, label + "-off")
        enabled = planet_view(0.8, radius, label + "-on")
        difference = ImageChops.difference(disabled, enabled)
        mean = sum(ImageStat.Stat(difference).mean) / 3
        print(f"Venus {label} haze on/off mean image difference: {mean:.3f}", flush=True)
        assert mean < 0.1, (label, mean)
    below = planet_view(0.8, boundary - 0.00001, "boundary-below")
    above = planet_view(0.8, boundary + 0.00001, "boundary-above")
    transition = sum(ImageStat.Stat(ImageChops.difference(below, above)).mean) / 3
    print(f"Venus distant-glow transition mean image difference: {transition:.3f}", flush=True)
    assert transition < 1.0, transition
