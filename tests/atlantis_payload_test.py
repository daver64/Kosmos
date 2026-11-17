#!/usr/bin/env python3
"""Check Atlantis cargo-door clearance and both payload handoff scenarios."""

import json
import math
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/kosmos").resolve()
ADDON = ROOT / "addons/cvel-atlantis"
spec = json.loads((ADDON / "ship.json").read_text())
part = spec["stages"][0]["parts"][0]
animations = json.loads((ADDON / part["animations"]).read_text())["animations"]
doors = [animation for animation in animations if animation["property"] == "bay-doors"]
assert len(doors) == 2
assert {frozenset(door["objects"]) for door in doors} == {
    frozenset(("cargodooroutR", "cargodoorinR", "radiatorFR", "radiatorBR")),
    frozenset(("cargodooroutL", "cargodoorinL", "radiatorFL", "radiatorBL")),
}

vertices = []
groups = {}
group = ""
for line in (ADDON / part["model"]).read_text().splitlines():
    fields = line.split()
    if not fields:
        continue
    if fields[0] == "o":
        group = fields[1]
    elif fields[0] == "v":
        vertices.append(tuple(map(float, fields[1:4])))
    elif fields[0] == "f":
        groups.setdefault(group, []).extend(vertices[int(v.split("/")[0]) - 1] for v in fields[1:])
for door in doors:
    center = door["center"]
    angle = math.radians(door["interpolation"][-1][1])
    opened_x = [center[0] + math.cos(angle) * (v[0] - center[0]) -
                math.sin(angle) * (v[1] - center[1])
                for name in door["objects"] for v in groups[name]]
    assert opened_x and (min(opened_x) > 2.3 if center[0] > 0 else max(opened_x) < -2.3)

with tempfile.TemporaryDirectory() as directory:
    temp = Path(directory)
    env = dict(os.environ, HOME=directory, XDG_CONFIG_HOME=str(temp / "config"),
               XDG_CACHE_HOME=str(temp / "cache"), SDL_VIDEODRIVER="offscreen",
               SDL_AUDIODRIVER="dummy", LP_NUM_THREADS="4")
    kennedy = json.loads((ROOT / "scenarios/CVEL/CVEL-NASA/atlantis_kennedy.json").read_text())
    assert [entry["ship"] for entry in kennedy["ships"]] == [
        "CVEL Atlantis launch stack", "CVEL Atlantis", "CVEL Centaur G", "CVEL Fregat", "Luna E3"
    ]
    for entry in kennedy["ships"]:
        entry.pop("surface", None)
        entry["orbit"] = {"altitude_km": 400, "inclination": 28.5}
    path = temp / "atlantis-launch-handoff.json"
    path.write_text(json.dumps(kennedy))
    for scenario_arg in (["--scenario-file", str(path)], ["--scenario", "atlantis_payload_orbit"]):
        result = subprocess.run(
            [str(BINARY), "--root", str(ROOT), *scenario_arg, "--selftest", "1",
             "--set", "/selftest/atlantis-payload=true"],
            cwd=ROOT, env=env, capture_output=True, text=True, timeout=180)
        output = result.stdout + result.stderr
        assert result.returncode == 0 and "Atlantis payload selftest ok" in output, output
print("Atlantis payload test ok")
