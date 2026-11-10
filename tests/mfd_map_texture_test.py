#!/usr/bin/env python3
"""Verify Map MFD surface images, including tiled Earth, in an isolated root."""

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
    fixture = temp / "root"
    fixture.mkdir()
    for name in ("addons", "scenarios"):
        (fixture / name).symlink_to(root / name, target_is_directory=True)
    data = fixture / "data"
    data.mkdir()
    for entry in (root / "data").iterdir():
        if entry.name not in ("textures", "textures-hires"):
            (data / entry.name).symlink_to(entry, target_is_directory=entry.is_dir())
    textures = data / "textures"
    textures.mkdir()
    for entry in (root / "data/textures").iterdir():
        (textures / entry.name).symlink_to(entry, target_is_directory=entry.is_dir())
    for body, colour in (("earth", (20, 80, 220)), ("mars", (220, 60, 20))):
        Image.new("RGB", (128, 64), colour).save(textures / f"{body}.png")
    env = dict(os.environ, HOME=home, XDG_CONFIG_HOME=str(temp / "config"),
               XDG_CACHE_HOME=str(temp / "cache"), SDL_VIDEODRIVER="offscreen",
               SDL_AUDIODRIVER="dummy", LP_NUM_THREADS="4")
    for body in ("earth", "mars"):
        shot = temp / f"{body}.png"
        result = subprocess.run(
            [str(binary), "--root", str(fixture), "--ship", "Luna E3", "--orbit",
             "--orbit-body", body.title(), "--mfd", "1", "--mfd-left", "map",
             "--hide-info", "--no-clutter", "--gen-threads", "1", "--max-texture", "2048",
             "--shot", str(shot), "--shot-at", "2"],
            cwd=fixture, env=env, capture_output=True, text=True, timeout=120,
        )
        output = result.stdout + result.stderr
        assert result.returncode == 0 and shot.is_file(), output
        assert f"surface texture {textures / (body + '.png')}" in output, output
        if body == "earth" and "Earth tiles: colour=" in output:
            assert output.count(f"surface texture {textures / 'earth.png'}") >= 2, output
        with Image.open(shot) as source:
            image = source.convert("RGB")
            pixels = list(image.crop((65, 412, 323, 539)).getdata())
            if body == "earth":
                matching = sum(b > 150 and b > 2 * r for r, g, b in pixels)
            else:
                matching = sum(r > 150 and r > 2 * b for r, g, b in pixels)
            assert matching > len(pixels) // 2, (body, matching, len(pixels), output)
    print("mfd_map_texture_test ok")
