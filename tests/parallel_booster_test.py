#!/usr/bin/env python3
"""Check parallel fuel consumption and powered booster separation."""

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
    addon = root / "addons" / "parallel-test"
    addon.mkdir(parents=True)
    source = json.loads((ROOT / "addons/kestrel/ship.json").read_text())
    stages = source["stages"][:2]
    for stage in stages:
        stage["parts"] = []
    stages[0]["propellant"] = 5000
    stages[1]["ignite_with_previous"] = True
    ship = {
        "name": "Parallel staging test", "class": "rocket", "script": "monitor.lua",
        "torque_accel": 0.35, "max_rate": 0.5, "drag_area": 4,
        "launcher_ascent": {"kick_speed": 10000, "kick_pitch": 85,
                            "turn_end_km": 45, "turn_end_pitch": 35, "climb_speed": 150},
        "stages": stages,
    }
    (addon / "ship.json").write_text(json.dumps(ship))
    (addon / "addon.json").write_text(json.dumps({"id": "parallel-test", "ships": ["ship.json"]}))
    launcher = (ROOT / "data/autopilot/launcher.lua").read_text()
    monitor = """
local launcher_update = update
local elapsed, reported = 0, {}
function update(dt)
  launcher_update(dt)
  elapsed = elapsed + dt
  local base = "/ships/ship[" .. props.get("/sim/active-ship") .. "]/"
  local stage = props.get(base .. "stage")
  local key = stage == 0 and elapsed > 2 and "burn" or stage == 1 and "separated"
  if key and not reported[key] then
    reported[key] = true
    print("PARALLEL", key, props.get(base .. "stage[0]/fuel"),
          props.get(base .. "stage[1]/fuel"), props.get("/controls/throttle"))
  end
end
"""
    (addon / "monitor.lua").write_text(launcher + monitor)
    env = dict(os.environ, HOME=directory, XDG_CONFIG_HOME=str(temp / "config"),
               XDG_CACHE_HOME=str(temp / "cache"), SDL_VIDEODRIVER="offscreen",
               SDL_AUDIODRIVER="dummy", LP_NUM_THREADS="4")
    result = subprocess.run(
        [str(BINARY), "--root", str(root), "--ship", ship["name"], "--selftest", "20",
         "--set", "/autopilot/enabled=true"],
        cwd=ROOT, env=env, capture_output=True, text=True, timeout=60,
    )
    output = result.stdout + result.stderr
    assert result.returncode == 0 and "script error" not in output.lower(), output
    samples = re.findall(r"PARALLEL\s+(burn|separated)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)", output)
    samples = {key: tuple(map(float, values)) for key, *values in samples}
    assert "burn" in samples and "separated" in samples, output
    assert 0 < samples["burn"][0] < 5000 and 0 < samples["burn"][1] < 30000, output
    assert samples["burn"][2] == 1 and samples["separated"][2] == 1, output
    assert samples["separated"][1] < samples["burn"][1], output
    print("parallel_booster_test passed")
