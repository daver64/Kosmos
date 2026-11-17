#!/usr/bin/env python3
"""Exercise the imported launchers' ascent, staging and flight."""

import json
import math
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
    ("atlantis_kennedy", "1200", "In orbit", 2),
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
        if scenario == "atlantis_kennedy":
            stack = json.loads((ROOT / "addons/cvel-atlantis/stack.json").read_text())["stages"]
            boosters, initial_tank, main_tank = stack
            assert math.isclose(boosters["propellant"], 1104677.2 * 1.15, rel_tol=1e-12)
            assert math.isclose(initial_tank["propellant"] + main_tank["propellant"],
                                (175000 + 651982.25) * 1.1, rel_tol=1e-12)
            assert len(boosters["engines"]) == 2
            assert all(math.isclose(engine["thrust"], 16213829.5 * 1.15, rel_tol=1e-12)
                       for engine in boosters["engines"])
            assert all(math.isclose(engine["thrust"], 2085815 * 1.15, rel_tol=1e-12)
                       for stack_stage in (initial_tank, main_tank) for engine in stack_stage["engines"])
            for stack_stage in stack:
                engines = stack_stage["engines"]
                assert all(engine["direction"] == [0, 0, -1] and engine["position"][1] == 0
                           for engine in engines)
                assert abs(sum(engine["thrust"] * engine["position"][0] for engine in engines)) < 1e-6
            for stack_stage, expected_y in zip(stack, ([-1.91, -1.91], [6.02, 6.02, 9.42], [6.02, 6.02, 9.42])):
                assert [e["position"][1] + e["plume_offset"][1] for e in stack_stage["engines"]] == expected_y
            unassisted = subprocess.run(
                [str(BINARY), "--root", str(ROOT), "--scenario", scenario, "--no-clutter",
                 "--no-tiles", "--no-clouds", "--gen-threads", "1", "--selftest", "12",
                 "--selftest-throttle", "1", "--set", "/controls/sas=off"],
                cwd=ROOT, env=env, capture_output=True, text=True, timeout=120)
            unassisted_output = unassisted.stdout + unassisted.stderr
            pitch = re.search(r"t=\s*10\b[^\n]*pitch=\s*([-\d.]+)", unassisted_output)
            assert unassisted.returncode == 0 and pitch and float(pitch.group(1)) > 89, unassisted_output
            orbiter = json.loads((ROOT / "addons/cvel-atlantis/ship.json").read_text())["stages"][0]["engines"]
            assert len(orbiter) == 2 and all(math.isclose(e["thrust"], 26700 * 1.15, rel_tol=1e-12) for e in orbiter)
            assert all(e["direction"] == [0, 0, -1] and e["position"][1] == 0 and
                       e["plume_offset"][1] == 3.7 for e in orbiter)
            oms = subprocess.run(
                [str(BINARY), "--root", str(ROOT), "--ship", "CVEL Atlantis", "--orbit",
                 "--orbit-alt", "400", "--alone", "--no-clutter", "--no-tiles", "--no-clouds",
                 "--gen-threads", "1", "--selftest", "12", "--set", "/controls/throttle=1",
                 "--set", "/controls/sas=off"],
                cwd=ROOT, env=env, capture_output=True, text=True, timeout=120)
            oms_output = oms.stdout + oms.stderr
            oms_pitch = re.search(r"t=\s*10\b[^\n]*pitch=\s*([-\d.]+)", oms_output)
            oms_fuel = re.search(r"t=\s*10\b[^\n]*fuel=\s*([\d.]+)", oms_output)
            assert (oms.returncode == 0 and oms_pitch and oms_fuel and
                    abs(float(oms_pitch.group(1))) < 1 and float(oms_fuel.group(1)) < 21500), oms_output
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
            links = 4 if scenario == "atlantis_kennedy" else 1
            assert re.search(rf"docked=1\s+links={links}\b", lines[0]), output
        if scenario == "atlantis_kennedy":
            periapsis = re.search(r"pe=\s*([-\d.]+)", lines[0])
            assert periapsis and float(periapsis.group(1)) > 150, output
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
