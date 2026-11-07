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

    def run(latitude, longitude, seconds, altitude=1000, speed=0):
        scenario.write_text(json.dumps({
            "name": "Local target landing",
            "ships": [{"ship": "Ares Spider", "body": "Mars",
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
