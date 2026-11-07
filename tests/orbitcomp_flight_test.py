#!/usr/bin/env python3
"""Compare OrbitComp's Lambert plan with the simulator's independent orbit propagation."""

import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "build/kosmos"

with tempfile.TemporaryDirectory() as home:
    env = dict(os.environ, HOME=home, XDG_CONFIG_HOME=str(Path(home) / ".config"))
    scenario = Path(home) / "intercept.json"
    same_frame = ((70, False), (140, False), (210, False), (280, False), (210, True))
    for phase, finite_burn in (() if "--patched-only" in sys.argv or "--moon-only" in sys.argv else same_frame):
        scenario.write_text(json.dumps({
            "name": "OrbitComp intercept test",
            "date": "2024-06-21 10:00:00",
            "target": "Intercept Target",
            "ships": [
                {"ship": "CVEL Deltaglider", "body": "Earth",
                 "orbit": {"altitude_km": 400, "inclination": 0, "phase": 0}},
                {"ship": "CVEL Deltaglider", "name": "Intercept Target", "body": "Earth",
                 "orbit": {"altitude_km": 800, "apoapsis_km": 2500, "inclination": 8, "phase": phase}},
            ],
        }))
        result = subprocess.run(
            [str(BINARY), "--scenario-file", str(scenario), "--selftest", "1", "--selftest-orbitcomp",
             "--set", f"/selftest/orbitcomp-finite-burn={'true' if finite_burn else 'false'}"],
            cwd=ROOT, capture_output=True, text=True, timeout=120, env=env,
        )
        print(f"== target phase {phase} degrees, {'finite burn' if finite_burn else 'impulse'}")
        print(result.stdout, end="", flush=True)
        if result.returncode or "orbitcomp selftest: 0 failure(s)" not in result.stdout:
            print(result.stderr, file=sys.stderr)
            sys.exit(1)

    for origin, target, altitude in (("Earth", "Mars", 400), ("Mars", "Earth", 300), ("Moon", "Earth", 150)):
        if "--moon-only" in sys.argv and origin != "Moon":
            continue
        scenario.write_text(json.dumps({
            "name": "OrbitComp patched trajectory test",
            "date": "2024-06-21 10:00:00",
            "target": target,
            "ships": [{"ship": "CVEL Deltaglider", "body": origin,
                       "orbit": {"altitude_km": altitude, "inclination": 0, "phase": 0}}],
        }))
        result = subprocess.run(
            [str(BINARY), "--scenario-file", str(scenario), "--selftest", "1", "--selftest-orbitcomp",
             "--set", "/selftest/orbitcomp-patched=true"],
            cwd=ROOT, capture_output=True, text=True, timeout=120, env=env,
        )
        print(f"== patched {origin} to {target}")
        print(result.stdout, end="", flush=True)
        if result.returncode or "orbitcomp patched selftest: 0 failure(s)" not in result.stdout:
            print("\n".join(line for line in result.stderr.splitlines()
                            if not line.startswith("[kosmos] Time advanced ")), file=sys.stderr)
            sys.exit(1)
