#!/usr/bin/env python3
"""Exercise Sync Orbit's approach page with a real nearby target ship."""

import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "build/kosmos"

with tempfile.TemporaryDirectory() as home:
    scenario = Path(home) / "approach.json"
    scenario.write_text(json.dumps({
        "name": "Sync approach test",
        "target": "Target",
        "ships": [
            {"ship": "CVEL Deltaglider", "body": "Earth",
             "orbit": {"altitude_km": 400, "phase": 0}},
            {"ship": "CVEL Deltaglider", "name": "Target", "body": "Earth",
             "orbit": {"altitude_km": 400, "phase": 0.1}},
        ],
    }))
    env = dict(os.environ, HOME=home, XDG_CONFIG_HOME=str(Path(home) / ".config"))
    result = subprocess.run(
        [str(BINARY), "--scenario-file", str(scenario), "--selftest", "1", "--selftest-timed-burn"],
        cwd=ROOT, capture_output=True, text=True, timeout=120, env=env,
    )
    if result.returncode or "timed burn selftest: 0 failure(s)" not in result.stdout:
        print(result.stdout, end="")
        print(result.stderr, file=sys.stderr)
        sys.exit(1)
    if "ok  : Sync approach display" not in result.stdout:
        print(result.stdout, end="")
        print("Sync approach display was not tested", file=sys.stderr)
        sys.exit(1)
    print("sync approach test passed", flush=True)
