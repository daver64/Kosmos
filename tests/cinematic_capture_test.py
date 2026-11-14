#!/usr/bin/env python3
"""Exercise fixed-step frame capture, camera motion and a quantum transition."""

import os
import json
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageChops, ImageStat

root = Path(__file__).resolve().parents[1]
binary = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build/kosmos"

with tempfile.TemporaryDirectory() as home:
    temp = Path(home)
    env = dict(os.environ, HOME=home, XDG_CONFIG_HOME=str(temp / "config"),
               XDG_CACHE_HOME=str(temp / "cache"), SDL_VIDEODRIVER="offscreen",
               SDL_AUDIODRIVER="dummy", LP_NUM_THREADS="4")
    base = [str(binary), "--root", str(root), "--ship", "CVEL Shuttle-AQ", "--orbit",
            "--alone", "--clean", "--mfd", "0", "--no-clutter", "--link-port", "0",
            "--date", "2024-06-21 15:00:00", "--cam-distance", "80",
            "--gen-threads", "1"]

    def run(args):
        return subprocess.run(base + args, cwd=root, env=env, capture_output=True,
                              text=True, timeout=180)

    frames = temp / "frames"
    camera = temp / "camera.json"
    camera.write_text(json.dumps([
        {"time": 0, "yaw": 40, "pitch": 10, "distance": 80, "fov": 50},
        {"time": 0.5, "yaw": 70, "pitch": 15, "distance": 90, "fov": 45},
    ]))
    args = ["--capture-frames", str(frames), "10", "6", "320", "180",
            "--capture-warmup", "2", "--capture-camera", str(camera)]
    result = run(args)
    assert result.returncode == 0, result.stdout + result.stderr
    files = sorted(frames.glob("*.png"))
    assert len(files) == 6 and files[0].name == "frame-000000.png" and files[-1].name == "frame-000005.png"
    with Image.open(files[0]) as first, Image.open(files[-1]) as last:
        assert first.size == last.size == (320, 180)
        assert sum(ImageStat.Stat(ImageChops.difference(first.convert("RGB"), last.convert("RGB"))).mean) > 0.1
    original = files[0].read_bytes()
    result = run(args)
    assert result.returncode != 0 and "already exists" in result.stderr, result.stdout + result.stderr
    assert files[0].read_bytes() == original

    jump = temp / "jump"
    result = run(["--test-jump", "1", "--capture-frames", str(jump), "10", "25", "320", "180"])
    assert result.returncode == 0, result.stdout + result.stderr
    assert len(list(jump.glob("*.png"))) == 25
    with Image.open(jump / "frame-000015.png") as image:
        assert image.size == (320, 180)

    daylight = temp / "daylight"
    result = run(["--system", "Tau Ceti", "--orbit-body", "Thalassa", "--orbit-alt", "400",
                  "--sunlit", "--sun-angle", "0", "--capture-warmup", "10",
                  "--capture-frames", str(daylight), "10", "1", "320", "180"])
    assert result.returncode == 0, result.stdout + result.stderr
    with Image.open(daylight / "frame-000000.png") as image:
        ground = image.convert("RGB").crop((0, 3 * image.height // 4, image.width, image.height))
        assert sum(ImageStat.Stat(ground).mean) / 3 > 30, "destination planet is unexpectedly dark"

    result = run(["--capture-frames", str(temp / "bad"), "0", "6", "320", "180"])
    assert result.returncode != 0 and "invalid --capture-frames" in result.stderr
    result = run(["--capture-frames", str(temp / "fullscreen"), "10", "6", "320", "180", "--fullscreen"])
    assert result.returncode != 0 and "cannot be combined" in result.stderr
    camera.write_text('[{"time":0}]')
    result = run(["--capture-frames", str(temp / "bad-camera"), "10", "6", "320", "180",
                  "--capture-camera", str(camera)])
    assert result.returncode != 0 and "camera key requires numeric" in result.stderr
    print("cinematic_capture_test ok")
