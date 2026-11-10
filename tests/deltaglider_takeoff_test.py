#!/usr/bin/env python3
"""Check both Deltaglider ascent profiles lift off promptly and fade hover."""

import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
binary = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build/kosmos"
sample = re.compile(
    r"t=\s*([\d.]+) ias=\s*([\d.]+) alt=\s*([-\d.]+).*?"
    r"landed=(\d).*?\n\s+ascent: (?:Liftoff|Climb) .*?hover=([\d.]+)"
)
with tempfile.TemporaryDirectory() as home:
    env = dict(os.environ, HOME=home, XDG_CONFIG_HOME=str(Path(home) / ".config"),
               KOSMOS_SELFTEST_DT="0.1", LP_NUM_THREADS="4")
    for args in (["--ship", "CVEL Deltaglider"], ["--scenario", "deltaglider_chippenham"]):
        result = subprocess.run(
            [str(binary), *args, "--set", "/autopilot/vessel-ascent/request=Deltaglider", "--selftest", "30"],
            cwd=root, env=env, capture_output=True, text=True, timeout=120, check=True,
        )
        output = result.stdout + result.stderr
        samples = [tuple(map(float, match)) for match in sample.findall(result.stdout)]
        assert samples and samples[0][4] > 0.9, output
        assert any(t <= 5 and speed < 60 and altitude > 5 and not landed
                   for t, speed, altitude, landed, hover in samples), output
        assert any(altitude > 200 and not landed and hover == 0
                   for t, speed, altitude, landed, hover in samples), output
        assert "Ascent aborted:" not in output and "crashed 0" in output, output
        print(f"{args[-1]}: hover-assisted takeoff and transition passed")

    elevators = []
    for addon in ("cvel-deltaglider", "cvel-deltaglider-nuclear"):
        ship = json.loads((root / "addons" / addon / "ship.json").read_text())
        elevators.append(next(surface for surface in ship["surfaces"] if surface["control"] == "elevator"))
    assert elevators[0] == elevators[1]
    result = subprocess.run(
        [str(binary), "--scenario", "deltaglider_chippenham", "--set", "/controls/throttle=1",
         "--set", "/controls/pitch=0.4", "--selftest", "34"],
        cwd=root, env=env, capture_output=True, text=True, timeout=120, check=True,
    )
    output = result.stdout + result.stderr
    manual = re.findall(r"t=\s*([\d.]+) ias=\s*[\d.]+ alt=\s*([-\d.]+).*?pitch=\s*([-\d.]+)", result.stdout)
    assert any(30 <= float(t) <= 33 and float(altitude) > 80 and float(pitch) > 5
               for t, altitude, pitch in manual), output
    assert "crashed 0" in output, output
    print("Deltaglider: manual elevator response passed")
