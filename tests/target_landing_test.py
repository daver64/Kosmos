#!/usr/bin/env python3
"""Exercise bounded local target landing and rejection in the simulator."""

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
    scenario = Path(home) / "target.json"
    env = dict(os.environ, HOME=home, XDG_CONFIG_HOME=str(Path(home) / ".config"),
               KOSMOS_SELFTEST_DT="0.1")

    def run(latitude, longitude, seconds, altitude=1000, speed=0, body="Mars", ship="Ares Spider"):
        scenario.write_text(json.dumps({
            "name": "Local target landing",
            "ships": [{"ship": ship, "body": body,
                       "surface": {"latitude": 0, "longitude": 0, "heading": 90,
                                   "altitude": altitude, "speed": speed}}],
        }))
        return subprocess.run(
            [str(BINARY), "--scenario-file", str(scenario), "--selftest", str(seconds),
             "--set", "/autopilot/land/targeted=1", "--set", f"/autopilot/land/latitude={latitude}",
             "--set", f"/autopilot/land/longitude={longitude}", "--set", "/autopilot/land/armed=1"],
            cwd=ROOT, env=env, capture_output=True, text=True, timeout=300, check=True,
        )

    for latitude, longitude in [(0.01, 0), (0, -0.01)]:
        result = run(latitude, longitude, 1000)
        output = result.stdout + result.stderr
        landed = re.search(r"target landing result: Autoland: landed distance=([\d.]+) m speed=([\d.]+) fuel=([\d.]+) crashed=(\d)", output)
        assert landed, output
        assert float(landed[1]) <= 50 and float(landed[2]) < 2 and float(landed[3]) > 100 and landed[4] == "0", output
        print(f"target landing ({latitude}, {longitude}): distance {landed[1]} m, speed {landed[2]} m/s, fuel {landed[3]}", flush=True)
    rejected = run(2, 0, 1)
    assert "Autoland aborted: target requires" in rejected.stderr, rejected.stdout + rejected.stderr
    invalid = run(91, 0, 1)
    assert "Autoland aborted: invalid target coordinates" in invalid.stderr, invalid.stdout + invalid.stderr
    low = run(0, 0, 1, altitude=200)
    assert "Autoland aborted: target requires" in low.stderr, low.stdout + low.stderr
    fast = run(0, 0, 1, speed=50)
    assert "Autoland aborted: target requires" in fast.stderr, fast.stdout + fast.stderr
    moon = run(0.01, 0, 1000, body="Moon")
    output = moon.stdout + moon.stderr
    landed = re.search(r"target landing result: Autoland: landed distance=([\d.]+) m speed=([\d.]+) fuel=([\d.]+) crashed=(\d)", output)
    assert landed and float(landed[1]) <= 50 and float(landed[2]) < 2 and landed[4] == "0", output
    print(f"Moon target landing: distance {landed[1]} m, speed {landed[2]} m/s", flush=True)
    high_mars = run(0, -0.033, 1000, altitude=2900, speed=20)
    output = high_mars.stdout + high_mars.stderr
    landed = re.search(r"target landing result: Autoland: landed distance=([\d.]+) m speed=([\d.]+) fuel=([\d.]+) crashed=(\d)", output)
    assert landed and float(landed[1]) <= 50 and float(landed[2]) < 2 and landed[4] == "0", output
    print(f"High Mars target landing: distance {landed[1]} m, speed {landed[2]} m/s", flush=True)
    high_earth = run(0, -0.017, 1000, altitude=2900, speed=20, body="Earth", ship="CVEL Shuttle-A")
    output = high_earth.stdout + high_earth.stderr
    landed = re.search(r"target landing result: Autoland: landed distance=([\d.]+) m speed=([\d.]+) fuel=([\d.]+) crashed=(\d)", output)
    assert landed and float(landed[1]) <= 50 and float(landed[2]) < 2 and landed[4] == "0", output
    print(f"High Earth Shuttle-A target landing: distance {landed[1]} m, speed {landed[2]} m/s", flush=True)
    shuttle = run(0.01, 0, 400, body="Moon", ship="CVEL Shuttle-A")
    output = shuttle.stdout + shuttle.stderr
    landed = re.search(r"target landing result: Autoland: landed distance=([\d.]+) m speed=([\d.]+) fuel=([\d.]+) crashed=(\d)", output)
    assert landed and float(landed[1]) <= 50 and float(landed[2]) < 2 and landed[4] == "0", output
    print(f"Moon Shuttle-A target landing: distance {landed[1]} m, speed {landed[2]} m/s", flush=True)
    earth = run(0.005, 0, 500, body="Earth", ship="CVEL Shuttle-A")
    output = earth.stdout + earth.stderr
    landed = re.search(r"target landing result: Autoland: landed distance=([\d.]+) m speed=([\d.]+) fuel=([\d.]+) crashed=(\d)", output)
    assert landed and float(landed[1]) <= 50 and float(landed[2]) < 2 and landed[4] == "0", output
    print(f"Earth Shuttle-A target landing: distance {landed[1]} m, speed {landed[2]} m/s", flush=True)
    missing_base = subprocess.run(
        [str(BINARY), "--scenario-file", str(scenario), "--selftest", "1",
         "--set", "/autopilot/land/targeted=true", "--set", "/autopilot/land/base=missing",
         "--set", "/autopilot/land/armed=true"],
        cwd=ROOT, env=env, capture_output=True, text=True, timeout=120, check=True,
    )
    assert "Autoland aborted: selected base not found on current body" in missing_base.stderr, missing_base.stdout + missing_base.stderr
    scenario.write_text(json.dumps({
        "name": "Offset pad test",
        "ships": [{"ship": "Ares Spider", "body": "Mars",
                   "surface": {"latitude": -5.4, "longitude": 137.8, "altitude": 1000}}],
    }))
    pad = subprocess.run(
        [str(BINARY), "--scenario-file", str(scenario), "--selftest", "1", "--set", "/selftest/base-pad=true"],
        cwd=ROOT, env=env, capture_output=True, text=True, timeout=120, check=True,
    )
    assert "base pad selftest: 0 failure(s)" in pad.stdout, pad.stdout + pad.stderr
    for changed in ("body", "stage"):
        binding = subprocess.run(
            [str(BINARY), "--scenario-file", str(scenario), "--selftest", "1",
             "--set", f"/selftest/autoland-binding={changed}"],
            cwd=ROOT, env=env, capture_output=True, text=True, timeout=120, check=True,
        )
        assert f"autoland {changed} binding selftest: 0 failure(s)" in binding.stdout, binding.stdout + binding.stderr
    print("body/stage changes cancel autoland and time skips are blocked: passed", flush=True)
    scenario.write_text(json.dumps({
        "name": "Base pad landing",
        "ships": [{"ship": "Ares Spider", "body": "Mars",
                   "surface": {"base": "gale", "altitude": 1000, "speed": 0}}],
    }))
    base_landing = subprocess.run(
        [str(BINARY), "--scenario-file", str(scenario), "--selftest", "500",
         "--set", "/autopilot/land/targeted=true", "--set", "/autopilot/land/base=gale",
         "--set", "/autopilot/land/armed=true"],
        cwd=ROOT, env=env, capture_output=True, text=True, timeout=300, check=True,
    )
    output = base_landing.stdout + base_landing.stderr
    landed = re.search(r"target landing result: Autoland: landed distance=([\d.]+) m speed=([\d.]+) fuel=([\d.]+) crashed=(\d)", output)
    assert landed and float(landed[1]) <= 10 and float(landed[2]) < 2 and landed[4] == "0", output
    print(f"Gale pad landing: distance {landed[1]} m, speed {landed[2]} m/s", flush=True)

    print("offset pad and missing base: passed", flush=True)
