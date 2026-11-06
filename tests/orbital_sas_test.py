#!/usr/bin/env python3
"""Verify orbital SAS targets and powered retrograde tracking."""

import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "build/kosmos"

with tempfile.TemporaryDirectory() as home:
    env = dict(os.environ, HOME=home, XDG_CONFIG_HOME=str(Path(home) / ".config"))
    for warp in (0, 2):
        result = subprocess.run(
            [str(BINARY), "--ship", "CVEL Deltaglider (Nuclear)", "--orbit-body", "Mars",
             "--orbit-alt", "20000", "--set", f"/sim/warp-index={warp}",
             "--selftest-orbital-sas", "--selftest", "1"],
            cwd=ROOT, capture_output=True, text=True, timeout=300, env=env, check=True,
        )
        assert "orbital SAS selftest: 0 failure(s)" in result.stdout, result.stdout + result.stderr
        print(f"Mars orbital SAS at {'1x' if warp == 0 else '3x'}: passed")
