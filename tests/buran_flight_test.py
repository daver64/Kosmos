#!/usr/bin/env python3
"""Verify Buran handoff, bay-door geometry and high-speed plasma batching."""

import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/kosmos").resolve()
ADDON = ROOT / "addons/cvel-buran"
SCENARIO = ROOT / "scenarios/CVEL/CVEL-Buran/buran_energia_baikonur.json"

spec = json.loads((ADDON / "ship.json").read_text())
part = spec["stages"][0]["parts"][0]
single_sided = set(part["single_sided"])
assert single_sided == {f"group{i}" for i in range(27, 39)}
animations = json.loads((ADDON / part["animations"]).read_text())["animations"]
doors = [set(animation["objects"]) for animation in animations if animation["property"] == "bay-doors"]
assert len(doors) == 2 and {"group33", "group34"} <= doors[0]

vertices = []
faces = {}
group = ""
for line in (ADDON / part["model"]).read_text().splitlines():
    fields = line.split()
    if not fields:
        continue
    if fields[0] == "o":
        group = fields[1]
    elif fields[0] == "v":
        vertices.append(tuple(float(value) for value in fields[1:4]))
    elif fields[0] == "f" and group in single_sided:
        points = tuple(vertices[int(value.split("/")[0]) - 1] for value in fields[1:])
        faces.setdefault(group, set()).add(tuple(sorted(points)))
for left, right in ((27, 28), (29, 30), (31, 32), (35, 36), (37, 38)):
    assert len(faces[f"group{left}"] & faces[f"group{right}"]) >= 20, (left, right)

with tempfile.TemporaryDirectory() as directory:
    temp = Path(directory)
    env = dict(os.environ, HOME=directory, XDG_CONFIG_HOME=str(temp / "config"),
               XDG_CACHE_HOME=str(temp / "cache"), SDL_VIDEODRIVER="offscreen",
               SDL_AUDIODRIVER="dummy", LP_NUM_THREADS="4")

    def run(args):
        result = subprocess.run([str(BINARY), "--root", str(ROOT), *args], cwd=ROOT, env=env,
                                capture_output=True, text=True, timeout=180)
        assert result.returncode == 0, result.stdout + result.stderr
        return result.stdout + result.stderr

    output = run(["--scenario", "buran_energia_baikonur", "--selftest", "1",
                  "--set", "/selftest/energia-autopilot=true"])
    assert "Energia autopilot selftest ok" in output, output
    scenario = json.loads(SCENARIO.read_text())
    assert [entry["ship"] for entry in scenario["ships"]] == [
        "CVEL Energia", "CVEL Buran", "CVEL Centaur G", "CVEL Fregat", "Luna E3"
    ]
    orbital = json.loads(json.dumps(scenario))
    for entry in orbital["ships"]:
        entry.pop("surface", None)
        entry["orbit"] = {"altitude_km": 400, "inclination": 51.6}
    path = temp / "buran-payload.json"
    path.write_text(json.dumps(orbital))
    output = run(["--scenario-file", str(path), "--selftest", "1",
                  "--set", "/selftest/buran-payload=true"])
    assert "Buran payload selftest ok" in output, output
    scenario["active"] = 1
    path = temp / "buran-docked.json"
    path.write_text(json.dumps(scenario))
    output = run(["--scenario-file", str(path), "--selftest", "1",
                  "--set", "/selftest/buran-docked-gear=true"])
    assert "Buran docked gear selftest ok" in output, output

    shot = temp / "plasma.png"
    output = run(["--ship", "CVEL Buran", "--orbit", "--alone", "--orbit-alt", "65",
                  "--date", "2024-06-21 15:00:00", "--no-clutter", "--no-tiles",
                  "--no-clouds", "--gen-threads", "1", "--perf",
                  "--set", "/sim/settings/gfx-max-fps=240",
                  "--shot", str(shot), "--shot-at", "6"])
    counts = [tuple(map(int, match)) for match in
              re.findall(r"\[perf\] 3D draws (\d+), instanced draws (\d+), instances (\d+)", output)]
    assert shot.is_file() and any(draws > 0 and instances >= 100 for _, draws, instances in counts), output
    assert "plasma instancing unavailable" not in output, output
    print(f"Buran flight test ok: plasma render stats {counts}", flush=True)
