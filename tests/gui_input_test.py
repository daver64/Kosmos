#!/usr/bin/env python3
"""Check shared window ownership and transition input handling."""

import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "build/kosmos"

with tempfile.TemporaryDirectory() as home:
    env = dict(os.environ, HOME=home, XDG_CONFIG_HOME=str(Path(home) / ".config"))
    result = subprocess.run(
        [str(BINARY), "--ship", "CVEL Deltaglider (Nuclear)", "--orbit-body", "Mars",
         "--orbit-alt", "20000", "--selftest-ui-windows", "--selftest", "1"],
        cwd=ROOT, capture_output=True, text=True, timeout=300, env=env,
    )
    assert result.returncode == 0 and "UI windows selftest: 0 failure(s)" in result.stdout, result.stdout + result.stderr
    print("GUI input test passed")
