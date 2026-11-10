#!/usr/bin/env python3
"""Verify rails warp near airless moons, in atmosphere and with parked vessels."""

import os
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
binary = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build/kosmos"
cases = [
    ("Phobos", ["--scenario", "phobos_luna_e3"], True),
    ("Deimos", ["--scenario", "deimos_luna_e3"], True),
    ("Earth atmospheric margin", ["--ship", "Luna E3", "--orbit-body", "Earth", "--orbit-alt", "85.5"], False),
    ("Earth above atmospheric margin", ["--ship", "Luna E3", "--orbit-body", "Earth", "--orbit-alt", "87"], True),
]

with tempfile.TemporaryDirectory() as home:
    env = dict(os.environ, HOME=home, XDG_CONFIG_HOME=str(Path(home) / ".config"),
               XDG_CACHE_HOME=str(Path(home) / ".cache"),
               SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy", LP_NUM_THREADS="4")
    for label, start, rails in cases:
        command = [str(binary), "--root", str(root), *start, "--selftest", "1",
                   "--set", "/selftest/warp-atmosphere=true",
                   "--set", f"/selftest/expect-rails={'true' if rails else 'false'}"]
        result = subprocess.run(command, cwd=root, env=env, capture_output=True, text=True, timeout=120)
        output = result.stdout + result.stderr
        assert result.returncode == 0 and "atmospheric warp selftest: 0 failure(s)" in output, output
        print(f"{label}: passed")
    command = [str(binary), "--root", str(root), "--scenario", "buran_energia_baikonur",
               "--selftest", "1", "--set", "/selftest/parked-warp=true"]
    result = subprocess.run(command, cwd=root, env=env, capture_output=True, text=True, timeout=120)
    output = result.stdout + result.stderr
    assert result.returncode == 0 and "Parked warp selftest ok" in output, output
    print("Buran with atmospheric and parked Energia: passed")
