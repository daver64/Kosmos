#!/usr/bin/env python3
"""Verify an engine plume is visible and rendered in a bounded batch."""

import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

root = Path(__file__).resolve().parents[1]
binary = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build/kosmos"

with tempfile.TemporaryDirectory() as home:
    temp = Path(home)
    env = dict(os.environ, HOME=home, XDG_CONFIG_HOME=str(temp / "config"),
               XDG_CACHE_HOME=str(temp / "cache"), SDL_VIDEODRIVER="offscreen",
               SDL_AUDIODRIVER="dummy", LIBGL_ALWAYS_SOFTWARE="1", LP_NUM_THREADS="4")
    shot = temp / "exhaust.png"
    result = subprocess.run(
        [str(binary), "--root", str(root), "--ship", "CVEL Fregat", "--orbit-body", "Mars",
         "--orbit-alt", "200", "--no-clutter", "--no-tiles", "--no-clouds", "--gen-threads", "1",
         "--date", "2024-06-21 15:00:00", "--perf", "--set", "/controls/throttle=1",
         "--shot", str(shot), "--shot-at", "6"],
        cwd=root, env=env, capture_output=True, text=True, timeout=120,
    )
    output = result.stdout + result.stderr
    assert result.returncode == 0 and shot.is_file(), output
    counts = [tuple(map(int, match)) for match in
              re.findall(r"\[perf\] 3D draws (\d+), instanced draws (\d+), instances (\d+)", output)]
    assert any(meshes < 250 and draws >= 1 and instances >= 100 for meshes, draws, instances in counts), counts
    assert "exhaust instancing unavailable" not in output, output
    with Image.open(shot) as source:
        pixels = source.convert("RGB").crop((675, 275, 840, 420)).getdata()
        assert sum(r > 220 and g > 170 and b > 80 for r, g, b in pixels) > 100, output
    print(f"exhaust_render_test ok: {counts}")
