#!/usr/bin/env python3
"""Check Shuttle 2010 lift and elevator response in unpowered flight."""

import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/kosmos").resolve()
SAMPLE = re.compile(
    r"t=\s*([\d.]+) ias=\s*([\d.]+) alt=\s*([-\d.]+)"
    r" vs=\s*([-\d.]+) pitch=\s*([-\d.]+)"
)

with tempfile.TemporaryDirectory() as home:
    env = dict(os.environ, HOME=home, XDG_CONFIG_HOME=f"{home}/config",
               XDG_CACHE_HOME=f"{home}/cache", SDL_VIDEODRIVER="offscreen",
               SDL_AUDIODRIVER="dummy", LP_NUM_THREADS="4", KOSMOS_SELFTEST_DT="0.05")
    scenario = {
        "name": "Shuttle 2010 flight test",
        "weather": "XXXX 011200Z 00000KT 9999 CLR 15/10 Q1013",
        "ships": [{
            "ship": "CVEL Shuttle 2010", "body": "Earth",
            "surface": {"latitude": 28.6, "longitude": -80.6, "heading": 90,
                        "altitude": 2000, "speed": 120},
        }],
    }
    path = Path(home) / "flight.json"

    def fly(speed, pitch):
        scenario["ships"][0]["surface"]["speed"] = speed
        path.write_text(json.dumps(scenario))
        result = subprocess.run(
            [str(BINARY), "--root", str(ROOT), "--scenario-file", str(path),
             "--no-clutter", "--no-tiles", "--no-clouds", "--gen-threads", "1",
             "--selftest", "10", "--set", f"/controls/pitch={pitch}",
             "--set", "/controls/sas=off"],
            cwd=ROOT, env=env, capture_output=True, text=True, timeout=120,
        )
        output = result.stdout + result.stderr
        assert result.returncode == 0 and "crashed 0" in output, output
        samples = {int(float(t)): tuple(map(float, (ias, alt, vs, attitude)))
                   for t, ias, alt, vs, attitude in SAMPLE.findall(output)}
        assert 4 in samples and 8 in samples, output
        return samples

    approach = fly(120, 0.5)
    assert approach[4][3] > 5 and approach[4][2] > 0, approach
    assert approach[8][1] > 1990, approach

    pull_up = fly(160, 0.5)
    assert pull_up[8][1] > 2080 and pull_up[8][2] > 10, pull_up

    push_over = fly(120, -0.5)
    assert push_over[4][3] < -10 and push_over[4][2] < -10, push_over
    print("Shuttle 2010: approach lift and bidirectional elevator response passed")
