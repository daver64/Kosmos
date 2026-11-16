#!/usr/bin/env python3
"""Exercise the imported launchers' ascent, staging and flight."""

import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/kosmos").resolve()
CASES = (
    ("atlas_iias_kennedy", "650", "In orbit", 5),
    ("ariane_44l_kourou", "1400", "In orbit", 5),
    ("atlantis_kennedy", "540", "Out of propellant", 2),
    ("shuttle2010_kennedy", "450", "Coasting to apoapsis", 2),
)
unknown = set(sys.argv[2:]) - {case[0] for case in CASES}
if unknown:
    raise SystemExit(f"Unknown scenarios: {', '.join(sorted(unknown))}")

with tempfile.TemporaryDirectory() as directory:
    env = dict(os.environ, HOME=directory, XDG_CONFIG_HOME=f"{directory}/config",
               XDG_CACHE_HOME=f"{directory}/cache", SDL_VIDEODRIVER="offscreen",
               SDL_AUDIODRIVER="dummy", LP_NUM_THREADS="4")
    for scenario, duration, status, stage in CASES:
        if len(sys.argv) > 2 and scenario not in sys.argv[2:]:
            continue
        if scenario == "shuttle2010_kennedy":
            addon = ROOT / "addons/cvel-shuttle2010"
            orbiter = json.loads((addon / "ship.json").read_text())["stages"][0]["engines"]
            stack = json.loads((addon / "stack.json").read_text())["stages"]
            for stack_stage in stack[1:]:
                assert len(stack_stage["engines"]) == len(orbiter) == 3
                for attached, separated in zip(stack_stage["engines"], orbiter):
                    assert attached["fuel"] == separated["fuel"] == "hydrogen"
                    assert all(attached[key] == separated[key] for key in ("type", "plume", "plume_width"))
        result = subprocess.run(
            [str(BINARY), "--root", str(ROOT), "--scenario", scenario, "--no-clutter",
             "--no-tiles", "--no-clouds", "--gen-threads", "1", "--selftest", duration,
             "--selftest-warp", "4", "--set", "/autopilot/enabled=1"],
            cwd=ROOT, env=env, capture_output=True, text=True, timeout=240,
        )
        output = result.stdout + result.stderr
        assert result.returncode == 0, output
        lines = [line for line in output.splitlines() if status in line and f"stage={stage}" in line]
        assert lines, output
        assert "crashed 0" in output.split("final:")[-1], output
        if scenario in ("atlantis_kennedy", "shuttle2010_kennedy"):
            assert re.search(r"docked=1\s+links=1", lines[0]), output
        if scenario == "shuttle2010_kennedy":
            altitude = re.search(r"alt=\s*([\d.]+)\s*km", lines[0])
            assert altitude and float(altitude.group(1)) > 150, output
            undock = subprocess.run(
                [str(BINARY), "--root", str(ROOT), "--scenario", scenario, "--no-clutter",
                 "--no-tiles", "--no-clouds", "--gen-threads", "1", "--selftest", "3",
                 "--set", "/dock/undock=true"],
                cwd=ROOT, env=env, capture_output=True, text=True, timeout=120,
            )
            undock_output = undock.stdout + undock.stderr
            assert undock.returncode == 0, undock_output
            assert re.search(r"docked=0\s+links=0", undock_output), undock_output
        print(f"{scenario}: {lines[0]}", flush=True)
