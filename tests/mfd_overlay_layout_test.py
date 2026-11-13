#!/usr/bin/env python3
"""Verify the initial framed MFDs render near the lower corners."""

import json
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

    fixture = temp / "root"
    addon = fixture / "addons" / "test-rocket"
    addon.mkdir(parents=True)
    for name in ("data", "scenarios"):
        (fixture / name).symlink_to(root / name, target_is_directory=True)
    ship = {
        "name": "External MFD test rocket", "class": "rocket",
        "cockpit": {"mfds": True, "external_mfds": True},
        "stages": [{"name": "Stage", "dry_mass": 1000, "propellant": 1000,
                    "length": 4, "radius": 1,
                    "engines": [{"thrust": 500000, "isp": 300}],
                    "parts": [{"prop": "stage", "length": 4, "radius": 1}]}],
    }
    (addon / "ship.json").write_text(json.dumps(ship))
    (addon / "addon.json").write_text(json.dumps({"id": "test-rocket", "ships": ["ship.json"]}))
    scenario = temp / "off.json"
    scenario.write_text(json.dumps({
        "name": "External MFD override", "mfd_display": "off",
        "ships": [{"ship": ship["name"], "body": "Earth", "orbit": {"altitude_km": 200}}],
    }))
    for name, args, expected in (
        ("default", ["--ship", ship["name"]], True),
        ("command-off", ["--ship", ship["name"], "--mfd", "0"], False),
        ("scenario-off", ["--scenario-file", str(scenario)], False),
        ("legacy-default", ["--ship", ship["name"]], False),
    ):
        if name == "legacy-default":
            del ship["cockpit"]["external_mfds"]
            (addon / "ship.json").write_text(json.dumps(ship))
        shot = temp / f"{name}.png"
        result = subprocess.run(
            [str(binary), "--root", str(fixture), *args, "--hide-info", "--no-clutter",
             "--no-tiles", "--no-clouds", "--gen-threads", "1",
             "--shot", str(shot), "--shot-at", "2"],
            cwd=root, env=env, capture_output=True, text=True, timeout=120,
        )
        assert result.returncode == 0 and shot.is_file(), result.stdout + result.stderr
        with Image.open(shot) as source:
            image = source.convert("RGB")
            samples = [image.getpixel((x + dx, y)) == (22, 24, 30)
                       for x in (24, 916) for dx, y in ((3, 370), (336, 370), (3, 700))]
            assert all(samples) if expected else not any(samples), (name, samples, result.stdout + result.stderr)
    print("mfd_overlay_layout_test ok")
