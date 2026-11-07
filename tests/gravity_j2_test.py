#!/usr/bin/env python3
"""Check the J2 runtime setting, persistence and warp transitions."""

import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "build/kosmos"

with tempfile.TemporaryDirectory() as home:
    env = dict(os.environ, HOME=home, XDG_CONFIG_HOME=str(Path(home) / ".config"))
    for body in ("Earth", "Mars"):
        result = subprocess.run(
            [str(BINARY), "--root", str(ROOT), "--ship", "CVEL Deltaglider", "--orbit", "--orbit-body", body,
             "--selftest", "1", "--set", "/selftest/gravity-j2=true"],
            cwd=ROOT, env=env, capture_output=True, text=True, timeout=120,
        )
        assert result.returncode == 0 and "J2 selftest: 0 failure(s)" in result.stdout, result.stdout + result.stderr
    print("gravity_j2_test ok")
