#!/usr/bin/env python3
"""Check Soyuz countdowns and warp through the pre-burn forecast check."""

import os
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
binary = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build/kosmos"

with tempfile.TemporaryDirectory() as home:
    env = dict(os.environ, HOME=home, XDG_CONFIG_HOME=f"{home}/config",
               XDG_CACHE_HOME=f"{home}/cache", SDL_VIDEODRIVER="offscreen",
               SDL_AUDIODRIVER="dummy", LP_NUM_THREADS="4")
    result = subprocess.run(
        [str(binary), "--scenario", "soyuz_tma_orbit", "--selftest", "1",
         "--set", "/selftest/soyuz-autoland=true", "--set", "/selftest/soyuz-countdowns=true"],
        cwd=binary.parent, env=env, capture_output=True, text=True, timeout=600,
    )
    output = result.stdout + result.stderr
    assert result.returncode == 0 and "Soyuz countdowns and warp: passed" in output, output
    result = subprocess.run(
        [str(binary), "--scenario", "soyuz_tma_orbit", "--selftest", "1",
         "--set", "/selftest/soyuz-entry=true", "--set", "/selftest/soyuz-entry-interface=true"],
        cwd=binary.parent, env=env, capture_output=True, text=True, timeout=600,
    )
    output = result.stdout + result.stderr
    assert result.returncode == 0 and "Soyuz 120 km entry forecast error:" in output, output
    print("soyuz_countdown_test ok")
