#!/usr/bin/env python3
"""Check experimental Mars orbital relief and the existing lower-altitude fallback."""

import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageChops, ImageStat

ROOT = Path(__file__).resolve().parents[1]


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: orbital_relief_mars_test.py EXPERIMENTAL_KOSMOS")
    binary = Path(sys.argv[1]).resolve()
    for layer in ("Surf", "Elev"):
        path = ROOT / "import" / "orbiter" / "Mars" / "Archive" / f"{layer}.tree"
        if not path.is_file():
            raise SystemExit(f"Mars Orbiter test requires {path}")

    with tempfile.TemporaryDirectory() as directory:
        temp = Path(directory)
        env = dict(os.environ, HOME=directory, XDG_CONFIG_HOME=str(temp / "config"),
                   SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy", LP_NUM_THREADS="1")
        transition = {}
        for radius, orbital in ((4.15, True), (1.05, False), (1.469, False), (1.473, True)):
            shot = temp / f"mars-{radius}.png"
            command = [str(binary), "--root", str(ROOT), "--scenario", "ares_spider_mars_orbit",
                       "--cam-planet", str(radius), "--cam-phase", "0", "--hide-info", "--no-clutter",
                       "--no-clouds", "--gen-threads", "1", "--orbital-relief-patches", "24",
                       "--orbital-relief-meshes", "96", "--orbital-relief-textures", "32",
                       "--perf", "--shot", str(shot), "--shot-at", "8"]
            result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=100)
            output = result.stdout + result.stderr
            assert result.returncode == 0 and shot.is_file(), output
            if orbital:
                assert "Mars Orbiter relief budgets:" in output, output
                assert re.search(r"Mars Orbiter relief: patches [1-9]\d*", output), output
            else:
                assert "Mars Orbiter relief budgets:" not in output, output
            with Image.open(shot) as image:
                rgb = image.convert("RGB").resize((64, 64))
                assert len(rgb.getcolors(4096)) > 8, output
                red, _, blue = rgb.getpixel((32, 32))
                assert red > blue + 5, (radius, red, blue, output)
                if radius in (1.469, 1.473):
                    transition[radius] = image.convert("RGB").resize((64, 36))
            print(f"Mars {'orbital relief' if orbital else 'ground fallback'} ok")

        difference = ImageStat.Stat(ImageChops.difference(transition[1.469], transition[1.473])).mean
        assert max(difference) < 12.0, f"Mars handoff colour mismatch: {difference}"
        print(f"Mars handoff mean absolute RGB differences: {difference}")

        scenario = temp / "instrument-mars.json"
        scenario.write_text(json.dumps({
            "name": "Instrument Mars relief test",
            "date": "2026-11-09 12:00:00",
            "active": 0,
            "ships": [{
                "ship": "NASA Voyager", "body": "Mars", "orbit": {"altitude_km": 2000},
                "state": {"r": [5400000, 0, 0], "v": [0, 0, -2816], "q": [0, 0, 1, 0]},
            }],
        }))
        shot = temp / "instrument-scene.png"
        command = [str(binary), "--root", str(ROOT), "--scenario-file", str(scenario),
                   "--hide-info", "--no-clutter", "--no-clouds", "--gen-threads", "1",
                   "--orbital-relief-patches", "24", "--orbital-relief-meshes", "96",
                   "--orbital-relief-textures", "32", "--set-at", "4",
                   "/ships/ship[0]/instruments/instrument[0]/shoot=1",
                   "--shot", str(shot), "--shot-at", "8"]
        result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=100)
        output = result.stdout + result.stderr
        assert result.returncode == 0 and shot.is_file(), output
        assert "Mars Orbiter relief budgets:" in output, output
        captures = list((temp / "config" / "kosmos" / "instrument-shots" / "NASA_Voyager").glob("nac-*.png"))
        assert len(captures) == 1, output
        with Image.open(captures[0]) as image:
            assert image.size == (800, 800), image.size
            red, _, blue = image.convert("RGB").getpixel((400, 400))
            assert red > blue + 5, (red, blue, output)
        print("Mars instrument capture ok")


if __name__ == "__main__":
    main()
