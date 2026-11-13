#!/usr/bin/env python3
"""Exercise a ShipEdit-style launcher with the shared gravity-turn profile."""

import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "build/kosmos"

with tempfile.TemporaryDirectory() as directory:
    temp = Path(directory)
    root = temp / "root"
    root.mkdir()
    for name in ("data", "scenarios"):
        (root / name).symlink_to(ROOT / name, target_is_directory=True)
    addon = root / "addons" / "test-rocket"
    addon.mkdir(parents=True)
    source = json.loads((ROOT / "addons/kestrel/ship.json").read_text())
    ship = {
        "name": "ShipEdit ascent test", "class": "rocket",
        "script": "@launcher.lua", "cockpit": {"mfds": True},
        "launcher_ascent": {"kick_speed": 50, "kick_pitch": 85,
                            "turn_end_km": 45, "turn_end_pitch": 35, "climb_speed": 150},
        "torque_accel": 0.35, "max_rate": 0.5, "drag_area": 4,
        "stages": source["stages"],
    }
    (addon / "ship.json").write_text(json.dumps(ship))
    (addon / "addon.json").write_text(json.dumps({"id": "test-rocket", "ships": ["ship.json"]}))
    env = dict(os.environ, HOME=directory, XDG_CONFIG_HOME=str(temp / "config"),
               XDG_CACHE_HOME=str(temp / "cache"), SDL_VIDEODRIVER="offscreen",
               SDL_AUDIODRIVER="dummy", LP_NUM_THREADS="4")
    result = subprocess.run(
        [str(BINARY), "--root", str(root), "--ship", ship["name"], "--selftest", "1200",
         "--selftest-warp", "4", "--set", "/autopilot/enabled=true"],
        cwd=ROOT, env=env, capture_output=True, text=True, timeout=180,
    )
    output = result.stdout + result.stderr
    assert result.returncode == 0 and "script error" not in output.lower(), output
    assert "In orbit" in output, output
    stages = {int(s) for s in re.findall(r"^t=.*?stage=(\d+)", output, re.MULTILINE)}
    assert stages == {0, 1}, output
    final = re.search(r"final: alt .*?apoapsis ([\d.-]+) km, periapsis ([\d.-]+) km, .*crashed (\d+)", output)
    assert final and final[3] == "0" and 190 <= float(final[1]) <= 210 and 190 <= float(final[2]) <= 210, output
    ship["launcher_ascent"]["kick_speed"] = 10000
    (addon / "ship.json").write_text(json.dumps(ship))
    result = subprocess.run(
        [str(BINARY), "--root", str(root), "--ship", ship["name"], "--selftest", "60",
         "--selftest-warp", "4", "--set", "/autopilot/enabled=true"],
        cwd=ROOT, env=env, capture_output=True, text=True, timeout=60,
    )
    output = result.stdout + result.stderr
    assert result.returncode == 0 and "Liftoff" in output and "Pitch kick" not in output and "Gravity turn" not in output, output
    ship["launcher_ascent"]["kick_speed"] = 50
    ship["stages"][1]["propellant"] = 18000
    extra = json.loads(json.dumps(ship["stages"][1]))
    extra["name"] = "Third powered stage"
    extra["propellant"] = 18000
    ship["stages"].insert(2, extra)
    (addon / "ship.json").write_text(json.dumps(ship))
    result = subprocess.run(
        [str(BINARY), "--root", str(root), "--ship", ship["name"], "--selftest", "1500",
         "--selftest-warp", "4", "--set", "/autopilot/enabled=true"],
        cwd=ROOT, env=env, capture_output=True, text=True, timeout=180,
    )
    output = result.stdout + result.stderr
    assert result.returncode == 0 and "script error" not in output.lower(), output
    stages = {int(s) for s in re.findall(r"^t=.*?stage=(\d+)", output, re.MULTILINE)}
    assert {0, 1, 2}.issubset(stages) and 3 not in stages, output
    assert "In orbit" in output, output
    ship["stages"].insert(2, {"name": "Inert adapter", "dry_mass": 10, "propellant": 10,
                              "length": 1, "radius": 1, "engines": [], "parts": []})
    (addon / "ship.json").write_text(json.dumps(ship))
    result = subprocess.run(
        [str(BINARY), "--root", str(root), "--ship", ship["name"], "--selftest", "1500",
         "--selftest-warp", "4", "--set", "/autopilot/enabled=true"],
        cwd=ROOT, env=env, capture_output=True, text=True, timeout=180,
    )
    output = result.stdout + result.stderr
    stages = {int(s) for s in re.findall(r"^t=.*?stage=(\d+)", output, re.MULTILINE)}
    assert result.returncode == 0 and {0, 1, 3}.issubset(stages) and 4 not in stages and "In orbit" in output, output
    scenario = temp / "orbit.json"
    scenario.write_text(json.dumps({"name": "Already in orbit", "ships": [
        {"ship": ship["name"], "body": "Earth", "orbit": {"altitude_km": 200}}]}))
    result = subprocess.run(
        [str(BINARY), "--root", str(root), "--scenario-file", str(scenario), "--selftest", "5",
         "--set", "/autopilot/enabled=true"],
        cwd=ROOT, env=env, capture_output=True, text=True, timeout=60,
    )
    assert "Ascent not started: requires a fuelled rocket at rest on Earth" in result.stdout + result.stderr
    ship["launcher_ascent"]["turn_end_km"] = 0
    (addon / "ship.json").write_text(json.dumps(ship))
    result = subprocess.run(
        [str(BINARY), "--root", str(root), "--ship", ship["name"], "--selftest", "1"],
        cwd=ROOT, env=env, capture_output=True, text=True, timeout=60,
    )
    assert "invalid launcher_ascent settings" in result.stdout + result.stderr
    print("shipedit_ascent_test passed")
