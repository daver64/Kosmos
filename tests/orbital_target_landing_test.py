#!/usr/bin/env python3
"""Exercise the player-facing orbital base landing controller and its safety checks."""

import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "build/kosmos"

with tempfile.TemporaryDirectory() as home:
    env = dict(os.environ, HOME=home, XDG_CONFIG_HOME=str(Path(home) / ".config"))

    def run(ship, body, base, fault="", inclination=0, phase=0, warp=False):
        scenario = Path(home) / "orbital-base.json"
        scenario.write_text(json.dumps({
            "name": "Orbital base autoland",
            "ships": [{"ship": ship, "body": body, "orbit": {
                "altitude_km": 200, "inclination": inclination, "phase": phase,
            }}],
        }))
        args = [str(BINARY), "--scenario-file", str(scenario), "--selftest", "1",
                "--set", "/selftest/orbital-base-landing=true", "--set", "/selftest/orbital-real-step=true",
                "--set", f"/selftest/landing-plane-base={base}"]
        if fault:
            args += ["--set", f"/selftest/orbital-fault={fault}"]
        if warp:
            args += ["--set", "/selftest/orbital-warp=true"]
        result = subprocess.run(args, cwd=ROOT, env=env, capture_output=True, text=True, timeout=900)
        output = result.stdout + result.stderr
        assert result.returncode == 0, output
        if fault:
            assert f"orbital autoland fault {fault}: passed" in output, output
        else:
            for stage in ("Planning orbit", "Waiting for window", "Checking orbit",
                          "Plane alignment", "Plane burn", "Powered descent", "Target approach"):
                assert f"orbital autoland phase: {stage}" in output, output
            match = re.search(r"orbital autoland result: Autoland: landed distance ([\d.]+) speed ([\d.]+) fuel ([\d.]+) crashed (\d)", output)
            assert match and float(match[1]) <= 3000 and float(match[2]) < 2 and float(match[3]) > 100 and match[4] == "0", output
            burn = re.search(r"orbital autoland phase: Plane burn time [\d.]+ fuel ([\d.]+)", output)
            descent = re.search(r"orbital autoland phase: Powered descent time [\d.]+ fuel ([\d.]+)", output)
            assert burn and descent and float(burn[1]) > float(descent[1]), output
            if warp:
                assert "orbital autoland checks: countdown 1 warp alignment 1 burn 1 burn-1x 1 descent 1 approach 1" in output, output
        print(f"{ship} {body} {fault or 'landing'} {'4x' if warp else '1x'}: passed", flush=True)
        if not fault:
            return (*[float(match[i]) for i in (1, 2, 3)], float(burn[1]) - float(descent[1]))

    if "--regressions-only" not in sys.argv[2:]:
        for ship, body, base in (("Ares Spider", "Mars", "gale"),
                                 ("Ares Spider", "Moon", "tranquility"),
                                 ("CVEL Shuttle-A", "Moon", "tranquility"),
                                 ("CVEL Shuttle-A", "Earth", "kourou")):
            accelerated = run(ship, body, base, warp=True)
            baseline = run(ship, body, base)
            assert abs(accelerated[2] - baseline[2]) < max(100.0, baseline[2] * 0.1), (accelerated, baseline)
            assert abs(accelerated[3] - baseline[3]) < max(50.0, baseline[3] * 0.03), (accelerated, baseline)
            print(f"{ship} {body} fuel 4x/1x: {accelerated[2]:.1f}/{baseline[2]:.1f} kg", flush=True)
            print(f"{ship} {body} plane-burn fuel 4x/1x: {accelerated[3]:.1f}/{baseline[3]:.1f} kg", flush=True)
    if "--matrix-only" not in sys.argv[2:]:
        run("Ares Spider", "Mars", "gale", inclination=25, phase=90)
        run("CVEL Shuttle-A (Nuclear)", "Mars", "gale")
        run("CVEL Shuttle-AQ", "Moon", "tranquility")
        for fault in ("gravity", "fuel", "orbit-change", "sas", "competing-autopilot",
                      "cancel-burn", "disarm-burn", "gravity-descent", "fuel-descent"):
            run("Ares Spider", "Moon", "tranquility", fault)
        for fault in ("alignment-miss", "cancel-burn", "disarm-burn"):
            run("Ares Spider", "Moon", "tranquility", fault, warp=True)
        run("CVEL Shuttle-A", "Earth", "kourou", "weather")
