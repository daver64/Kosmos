#!/usr/bin/env python3
"""Check shipped planet textures and overrides in an isolated game root."""

import os
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
binary = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build/kosmos"

with tempfile.TemporaryDirectory() as directory:
    temp = Path(directory)
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
    env = dict(os.environ, HOME=directory, XDG_CONFIG_HOME=str(temp / "config"), XDG_CACHE_HOME=str(temp / "cache"),
               SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy", LP_NUM_THREADS="4")

    def render(label):
        shot = temp / (label + ".png")
        command = [str(binary), "--root", str(fixture), "--scenario", "venus_luna_e3",
                   "--hide-info", "--no-clutter", "--no-clouds", "--gen-threads", "1",
                   "--max-texture", "4096", "--shot", str(shot), "--shot-at", "4"]
        result = subprocess.run(command, cwd=fixture, env=env, capture_output=True, text=True, timeout=120)
        output = result.stdout + result.stderr
        assert result.returncode == 0 and shot.is_file(), output
        return output

    output = render("shipped")
    assert f"surface texture {textures / 'venus.jpg'}: 4096x2048" in output, output
    for body in ("mercury", "jupiter", "saturn"):
        assert f"surface texture {textures / 'planets' / (body + '.jpg')}: 2048x1024" in output, output
    assert not list((temp / "cache/kosmos/planets").glob("mercury-4012-*-g1-*.png")), output

    hires = data / "textures-hires"
    hires.mkdir()
    (hires / "venus.jpg").symlink_to(root / "data/textures/planets/venus.jpg")
    output = render("override")
    assert f"surface texture {hires / 'venus.jpg'}: 2048x1024" in output, output
    assert f"surface texture {textures / 'venus.jpg'}:" not in output, output
print("planet texture selection: shipped maps and hi-res override passed")
