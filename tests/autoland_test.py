#!/usr/bin/env python3
"""Exercise the Mars hover autoland from orbit, including unpowered entry."""

import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "build/kosmos"
REPORT = re.compile(
    r"^t=\s*([\d.]+)\s+Autoland: (Deorbit|Entry|Braking|landed)"
    r"\s+alt=\s*([-\d.]+) km speed=\s*([-\d.]+) vs=\s*([-\d.]+)"
    r".*fuel=\s*([\d.]+)",
    re.MULTILINE,
)


def run(body, ship, seconds):
    with tempfile.TemporaryDirectory() as home:
        env = dict(os.environ, HOME=home, XDG_CONFIG_HOME=str(Path(home) / ".config"), KOSMOS_SELFTEST_DT="0.25")
        return subprocess.run(
            [str(BINARY), "--ship", ship, "--orbit-body", body, "--orbit-alt", "200",
             "--selftest", str(seconds), "--set", "/autopilot/land/armed=1"],
            cwd=ROOT, capture_output=True, text=True, timeout=300, env=env, check=True,
        )


def main():
    result = run("Mars", "Ares Spider", 6100)
    output = result.stdout + result.stderr
    reports = [(float(t), phase, float(alt), float(speed), float(vs), float(fuel))
               for t, phase, alt, speed, vs, fuel in REPORT.findall(result.stdout)]
    entry = [r for r in reports if r[1] == "Entry"]
    landed = [r for r in reports if r[1] == "landed"]
    assert entry and any(r[1] == "Deorbit" for r in reports), output
    assert any(r[1] == "Braking" for r in reports), output
    assert entry[0][5] - entry[-1][5] < 2.0, "entry consumed hover fuel\n" + output
    assert entry[0][3] - entry[-1][3] > 200.0, "entry did not aerobrake\n" + output
    assert landed and landed[0][3] < 2.0 and landed[0][5] > 1000.0, output
    assert "crashed 0, soi Mars" in output and "Autoland aborted" not in output, output

    earth = run("Earth", "Ares Spider", 1)
    assert "Autoland aborted: only Mars or Moon/Shuttle-A targets are supported" in earth.stderr, earth.stderr
    no_hover = run("Mars", "Kestrel", 1)
    assert "Autoland aborted: insufficient hover thrust or fuel" in no_hover.stderr, no_hover.stderr
    no_gear = run("Mars", "Luna E3", 1)
    assert "Autoland aborted: unsupported vessel" in no_gear.stderr, no_gear.stderr
    print("autoland_test ok")


if __name__ == "__main__":
    main()
