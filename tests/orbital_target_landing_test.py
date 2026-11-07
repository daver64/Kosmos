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

    def run(ship, body, base, fault="", inclination=0, phase=0):
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
        print(f"{ship} {body} {fault or 'landing'}: passed", flush=True)

    run("Ares Spider", "Mars", "gale")
    run("Ares Spider", "Moon", "tranquility")
    run("CVEL Shuttle-A", "Moon", "tranquility")
    run("CVEL Shuttle-A", "Earth", "kourou")
    run("Ares Spider", "Mars", "gale", inclination=25, phase=90)
    run("CVEL Shuttle-A (Nuclear)", "Mars", "gale")
    run("CVEL Shuttle-AQ", "Moon", "tranquility")
    for fault in ("gravity", "fuel", "orbit-change", "sas", "competing-autopilot",
                  "cancel-burn", "disarm-burn", "gravity-descent", "fuel-descent"):
        run("Ares Spider", "Moon", "tranquility", fault)
    run("CVEL Shuttle-A", "Earth", "kourou", "weather")
