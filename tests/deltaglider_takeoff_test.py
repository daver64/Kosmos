#!/usr/bin/env python3
"""Check hover-assisted ascent and unaided ground-roll takeoffs."""

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

    scenario = json.loads((root / "scenarios/CVEL/CVEL-Deltaglider/deltaglider_chippenham.json").read_text())
    ground_sample = re.compile(
        r"t=\s*([\d.]+) ias=\s*([\d.]+) alt=\s*([-\d.]+) vs=\s*([-\d.]+)"
        r" pitch=\s*([-\d.]+).*?landed=(\d).*?hover=([\d.]+)"
    )
    env["KOSMOS_SELFTEST_DT"] = "0.05"
    for name in ("CVEL Deltaglider", "CVEL Deltaglider (Nuclear)"):
        scenario["ships"][0]["ship"] = name
        scenario_path = Path(home) / "takeoff.json"
        scenario_path.write_text(json.dumps(scenario))
        result = subprocess.run(
            [str(binary), "--scenario-file", str(scenario_path), "--set", "/controls/hover=0",
             "--selftest-throttle", "1", "--selftest", "30"],
            cwd=root, env=env, capture_output=True, text=True, timeout=120, check=True,
        )
        output = result.stdout + result.stderr
        samples = [tuple(map(float, match)) for match in ground_sample.findall(result.stdout)]
        assert samples and all(hover == 0 for *_, hover in samples), output
        liftoff = next((s for s in samples if not s[5] and s[2] > 0.5 and s[3] > 0), None)
        assert liftoff and liftoff[0] <= 16 and liftoff[1] <= 180, output
        distance = sum((a[1] + b[1]) * (b[0] - a[0]) / 2
                       for a, b in zip(samples, samples[1:]) if b[0] <= liftoff[0])
        assert distance < 1480, output
        assert any(t <= 24 and altitude > 100 and vertical > 0 and not landed
                   for t, speed, altitude, vertical, pitch, landed, hover in samples), output
        assert "crashed 0" in output, output
        print(f"{name}: no-hover liftoff at {liftoff[1]:.1f} m/s, {liftoff[0]:.0f} s, ~{distance:.0f} m")
