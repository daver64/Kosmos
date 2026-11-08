#!/usr/bin/env python3
"""Check upright and overturned rover ground contact on Mars."""

import os
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
binary = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build/kosmos"
with tempfile.TemporaryDirectory() as home:
    env = dict(os.environ, HOME=home, XDG_CONFIG_HOME=str(Path(home) / ".config"))
    result = subprocess.run(
        [str(binary), "--root", str(root), "--ship", "CVEL Lunar Rover",
         "--selftest", "1", "--set", "/selftest/rover-rollover=true"],
        cwd=root, env=env, capture_output=True, text=True, timeout=60,
    )
if result.returncode or "Rover rollover selftest ok" not in result.stdout:
    raise SystemExit(result.stdout + result.stderr)
print("rover_rollover_test ok")
