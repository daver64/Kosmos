#!/usr/bin/env python3
"""Verify the initial framed MFDs render near the lower corners."""

import os
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
               SDL_AUDIODRIVER="dummy", LP_NUM_THREADS="4")
    shot = temp / "mfd.png"
    result = subprocess.run(
        [str(binary), "--root", str(root), "--ship", "Luna E3", "--orbit",
         "--mfd", "1", "--hide-info", "--no-clutter", "--gen-threads", "1",
         "--shot", str(shot), "--shot-at", "2"],
        cwd=root, env=env, capture_output=True, text=True, timeout=120,
    )
    assert result.returncode == 0 and shot.is_file(), result.stdout + result.stderr
    with Image.open(shot) as source:
        image = source.convert("RGB")
        assert image.size == (1280, 720), image.size
        bezel = (22, 24, 30)
        for x in (24, 916):
            for dx, y in ((3, 370), (336, 370), (3, 700)):
                assert image.getpixel((x + dx, y)) == bezel, (x, dx, y, image.getpixel((x + dx, y)))
    print("mfd_overlay_layout_test ok")
