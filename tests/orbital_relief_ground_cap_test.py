#!/usr/bin/env python3
"""Check the default ground-relief cap during a low-altitude descent."""

import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: orbital_relief_ground_cap_test.py KOSMOS")
    binary = Path(sys.argv[1]).resolve()
    for layer in ("Surf", "Elev"):
        path = ROOT / "import" / "orbiter" / "Earth" / "Archive" / f"{layer}.tree"
        if not path.is_file():
            raise SystemExit(f"Earth Orbiter test requires {path}")

    with tempfile.TemporaryDirectory() as directory:
        temp = Path(directory)
        scenario = temp / "descent.json"
        scenario.write_text(json.dumps({
            "name": "Soyuz ground relief cap",
            "date": "2000-01-02 17:20:50",
            "ships": [{
                "ship": "CVEL Soyuz TMA Descent Module",
                "body": "Earth",
                "orbit": {"altitude_km": 2},
                "state": {"r": [6373000, 0, 0], "v": [-60, -185, -426], "q": [1, 0, 0, 0]},
            }],
        }))
        env = dict(os.environ, HOME=directory, XDG_CONFIG_HOME=str(temp / "config"),
                   SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy", LP_NUM_THREADS="4")
        for extra, limit in (([], 512), (["--orbital-relief-patches", "24"], 24)):
            shot = temp / f"cap-{limit}.png"
            command = [str(binary), "--root", str(ROOT), "--scenario-file", str(scenario),
                       "--orbital-relief-ground", "--set", "/sim/settings/gfx-earth-relief=true",
                       "--set", "/sim/settings/gfx-max-fps=240", "--gen-threads", "1",
                       "--perf", "--shot", str(shot), "--shot-at", "15", *extra]
            result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=120)
            output = result.stdout + result.stderr
            assert result.returncode == 0 and shot.is_file(), output
            samples = re.findall(r"\[perf\] Orbiter relief: patches (\d+),.*patch cap (\d+)", output)
            patches = [int(value) for value, cap in samples if int(cap) == limit]
            assert patches and max(patches) <= limit, output
            assert max(patches) >= limit // 2, output
            assert re.search(r"\[perf\] Orbiter relief: .*limits relief=1", output), output
            fps = re.findall(r"\[perf\] ([\d.]+) fps", output)
            print(f"ground relief limit {limit}: peak {max(patches)} patches, fps samples {fps[1:]}")

        shot = temp / "orbit.png"
        command = [str(binary), "--root", str(ROOT), "--scenario", "dragonfly_orbit",
                   "--cam-planet", "1.03", "--orbital-relief-ground",
                   "--set", "/sim/settings/gfx-earth-relief=true", "--gen-threads", "1",
                   "--perf", "--shot", str(shot), "--shot-at", "8"]
        result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=120)
        output = result.stdout + result.stderr
        assert result.returncode == 0 and shot.is_file(), output
        samples = re.findall(r"\[perf\] Orbiter relief: patches (\d+),.*patch cap (\d+)", output)
        assert any(int(patches) > 0 and int(cap) == 0 for patches, cap in samples), output
        print("orbital relief remains unlimited")

        mars_files = [ROOT / "import" / "orbiter" / "Mars" / "Archive" / f"{layer}.tree"
                      for layer in ("Surf", "Elev")]
        if all(path.is_file() for path in mars_files):
            shot = temp / "mars.png"
            command = [str(binary), "--root", str(ROOT), "--scenario", "ares_spider_mars_orbit",
                       "--cam-planet", "1.0001", "--cam-phase", "0", "--hide-info", "--no-clutter",
                       "--no-clouds", "--gen-threads", "1", "--orbital-relief-ground",
                       "--perf", "--shot", str(shot), "--shot-at", "8"]
            result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=120)
            output = result.stdout + result.stderr
            assert result.returncode == 0 and shot.is_file(), output
            samples = re.findall(r"\[perf\] Mars Orbiter relief: patches (\d+),.*patch cap (\d+)", output)
            assert any(0 < int(patches) <= 512 and int(cap) == 512 for patches, cap in samples), output
            assert all(int(patches) <= 512 for patches, cap in samples if int(cap) == 512), output
            print("Mars ground relief uses the 512-patch cap")
        else:
            print("Mars ground relief check skipped: Orbiter Mars archives not installed")


if __name__ == "__main__":
    main()
