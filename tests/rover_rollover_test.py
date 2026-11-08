#!/usr/bin/env python3
"""Check rover ground contact and surface-relative obstacle collisions."""

import os
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
binary = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build/kosmos"
with tempfile.TemporaryDirectory() as home:
    env = dict(os.environ, HOME=home, XDG_CONFIG_HOME=str(Path(home) / ".config"))
    for mode in ("rollover", "collision"):
        result = subprocess.run(
            [str(binary), "--root", str(root), "--ship", "CVEL Lunar Rover",
             "--selftest", "1", "--set", f"/selftest/rover-{mode}=true"],
            cwd=root, env=env, capture_output=True, text=True, timeout=60,
        )
        if result.returncode or f"Rover {mode} selftest ok" not in result.stdout:
            raise SystemExit(result.stdout + result.stderr)
        print(f"rover {mode} test ok")
    for scenario in ("lunar_rover_tranquility", "rover_gale_crater"):
        result = subprocess.run(
            [str(binary), "--root", str(root), "--scenario", scenario,
             "--selftest", "1", "--set", "/selftest/curiosity=true"],
            cwd=root, env=env, capture_output=True, text=True, timeout=60,
        )
        if result.returncode or "Curiosity selftest ok" not in result.stdout:
            raise SystemExit(result.stdout + result.stderr)
        print(f"Curiosity {scenario} test ok")
