#!/usr/bin/env python3
"""Compare the Sun's rendered glare at different distances."""

import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

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
    sun = next(body for body in solar_system["bodies"] if body["name"] == "Sun")
    sun_radius = sun["radius"]
    jupiter["orbit"]["e"] = 0.0
    scenario = json.loads((ROOT / "scenarios/Moons/Jupiter/europa_luna_e3.json").read_text())
    scenario["ships"][0]["orbit"]["altitude_km"] = 10000
    scenario_path = temp / "glare-scenario.json"
    scenario_path.write_text(json.dumps(scenario))
    env = dict(os.environ, HOME=str(temp), XDG_CONFIG_HOME=str(temp / "config"),
               SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy", LP_NUM_THREADS="4")
    readings = []
    for distance, radius_scale in ((1.0, 1.0), (5.2, 1.0), (39.5, 1.0), (1.0, 0.5), (1.0, 2.0)):
        jupiter["orbit"]["a_au"] = distance
        sun["radius"] = sun_radius * radius_scale
        (data / "sol.json").write_text(json.dumps(solar_system))
        shot = temp / f"glare-{distance}-{radius_scale}.png"
        args = [str(BINARY), "--root", str(root), "--scenario-file", str(scenario_path),
                "--date", "2000-01-01 12:00:00", "--cam-antisun", "--cam-distance", "10000",
                "--cam-yaw", "40", "--cam-pitch", "20", "--hide-info", "--no-clutter",
                "--no-clouds", "--gen-threads", "1", "--shot", str(shot), "--shot-at", "2"]
        result = subprocess.run(args, cwd=root, env=env, capture_output=True, text=True, timeout=120)
        assert result.returncode == 0 and shot.is_file(), result.stdout + result.stderr
        with Image.open(shot) as image:
            x, y = image.width // 2, image.height // 2
            pixels = image.convert("RGB").crop((x - 100, y - 100, x + 100, y - 4)).getdata()
            readings.append(sum(r > 30 for r, g, b in pixels))
    print(f"Sun glare pixels at 1, 5.2 and 39.5 AU, then half and twice the Sun's radius: {readings}", flush=True)
    near, jupiter, distant, smaller, larger = readings
    assert near > 50 and near > jupiter * 2 and jupiter > distant * 2 and distant > 0, readings
    assert smaller < near < larger, readings
