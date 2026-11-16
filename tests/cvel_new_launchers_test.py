#!/usr/bin/env python3
"""Exercise the imported launchers' ascent, staging and tank exhaustion."""

import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/kosmos").resolve()

with tempfile.TemporaryDirectory() as directory:
    env = dict(os.environ, HOME=directory, XDG_CONFIG_HOME=f"{directory}/config",
               XDG_CACHE_HOME=f"{directory}/cache", SDL_VIDEODRIVER="offscreen",
               SDL_AUDIODRIVER="dummy", LP_NUM_THREADS="4")
    for scenario, duration, status, stage in (
        ("atlas_iias_kennedy", "650", "In orbit", 5),
        ("ariane_44l_kennedy", "1400", "In orbit", 5),
        ("atlantis_kennedy", "540", "Out of propellant", 2),
    ):
        result = subprocess.run(
            [str(BINARY), "--root", str(ROOT), "--scenario", scenario, "--no-clutter",
             "--no-tiles", "--no-clouds", "--gen-threads", "1", "--selftest", duration,
             "--selftest-warp", "4", "--set", "/autopilot/enabled=1"],
            cwd=ROOT, env=env, capture_output=True, text=True, timeout=240,
        )
        output = result.stdout + result.stderr
        assert result.returncode == 0, output
        lines = [line for line in output.splitlines() if status in line and f"stage={stage}" in line]
        assert lines, output
        assert "crashed 0" in output.split("final:")[-1], output
        if scenario == "atlantis_kennedy":
            assert re.search(r"docked=1\s+links=1", lines[0]), output
        print(f"{scenario}: {lines[0]}", flush=True)
