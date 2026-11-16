#!/usr/bin/env python3
"""Verify Delta IV reaches orbit with the corrected upper-stage thrust."""

import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "build/kosmos"
FINAL = re.compile(r"final: alt [-\d.]+ km, apoapsis ([-\d.]+) km, periapsis ([-\d.]+) km, .*crashed (\d+)")

with tempfile.TemporaryDirectory() as home:
    env = dict(os.environ, HOME=home, XDG_CONFIG_HOME=str(Path(home) / "config"),
               XDG_CACHE_HOME=str(Path(home) / "cache"), SDL_VIDEODRIVER="offscreen",
               SDL_AUDIODRIVER="dummy", LP_NUM_THREADS="4")
    for scenario, links, seconds in (("delta_iv_heavy_kennedy", 2, 1650),
                                     ("delta_iv_star48_kennedy", 3, 1400)):
        result = subprocess.run(
            [str(BINARY), "--root", str(ROOT), "--scenario", scenario, "--selftest", str(seconds),
             "--set", "/autopilot/enabled=1"],
            cwd=ROOT, env=env, capture_output=True, text=True, timeout=300,
        )
        output = result.stdout + result.stderr
        assert result.returncode == 0 and "script error" not in output.lower(), output
        assert "CRASHED" not in output and "In orbit" in output, output
        upper_stage = re.findall(rf"stage=4 fuel=\s*(\d+).*docked=1 links={links}", result.stdout)
        assert len(upper_stage) >= 2 and int(upper_stage[-1]) < int(upper_stage[0]), output
        final = FINAL.search(result.stdout)
        assert final and final.group(3) == "0", output
        apoapsis, periapsis = map(float, final.group(1, 2))
        assert 185 <= periapsis <= apoapsis <= 310, output
        print(f"{scenario}: apoapsis {apoapsis:.1f} km, periapsis {periapsis:.1f} km", flush=True)

print("delta_iv_autopilot_test ok")
