#!/usr/bin/env python3
"""Verify probe startup pointing, Earth tracking and saved-flight resume."""

import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "build/kosmos"

with tempfile.TemporaryDirectory() as home:
    env = dict(os.environ, HOME=home, XDG_CONFIG_HOME=str(Path(home) / ".config"))
    for scenario in ("pioneer10_jupiter", "voyager_jupiter"):
        result = subprocess.run(
            [str(BINARY), "--scenario", scenario, "--selftest", "1",
             "--set", "/selftest/earth-communications=true"],
            cwd=ROOT, env=env, capture_output=True, text=True, timeout=120,
        )
        assert result.returncode == 0 and "Earth communications selftest: 0 failure(s)" in result.stdout, result.stdout + result.stderr
        print(f"{scenario}: Earth communications passed")
