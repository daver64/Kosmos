#!/usr/bin/env python3
"""Verify every Solar System planet and moon has a working Luna E3 orbit."""

import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
binary = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build/kosmos"
addon = json.loads((root / "addons/probe/addon.json").read_text())
assert "ship.json" in addon["ships"] and "ship_rtg.json" in addon["ships"]
solar = json.loads((root / "addons/probe/ship.json").read_text())
rtg = json.loads((root / "addons/probe/ship_rtg.json").read_text())
assert [item["type"] for item in solar["systems"]].count("solar") == 1
assert [item["type"] for item in rtg["systems"]].count("rtg") == 1
assert not any(item["type"] == "solar" for item in rtg["systems"])
assert [item for item in solar["systems"] if item["type"] != "solar"] == [
    item for item in rtg["systems"] if item["type"] != "rtg"
]
solar["systems"] = rtg["systems"]
solar["name"] = rtg["name"]
solar["description"] = rtg["description"]
solar["stages"][0]["dry_mass"] += 20
assert solar == rtg
bodies = {body["name"]: body for body in json.loads((root / "data/sol.json").read_text())["bodies"]}
earth_axis = bodies["Earth"]["orbit"]["a_au"]


def expected_probe(body_name):
    body = bodies[body_name]
    while body.get("parent") not in (None, "Sun"):
        body = bodies[body["parent"]]
    return "Luna E3 RTG" if body.get("orbit", {}).get("a_au", 0) > earth_axis else "Luna E3"


for path in (root / "scenarios").rglob("*.json"):
    scenario = json.loads(path.read_text())
    for ship in scenario.get("ships", []):
        if ship.get("ship") in ("Luna E3", "Luna E3 RTG"):
            assert ship["ship"] == expected_probe(ship["body"]), path

expected = {name for name, body in bodies.items() if body["kind"] in ("planet", "moon")}
scenarios = {}
for folder, kind in (("Planets", "planet"), ("Moons", "moon")):
    for path in (root / "scenarios" / folder).rglob("*.json"):
        scenario = json.loads(path.read_text())
        assert len(scenario["ships"]) == 1 and scenario["active"] == 0, path
        ship = scenario["ships"][0]
        name = ship["body"]
        assert name in expected and bodies[name]["kind"] == kind, path
        assert name not in scenarios and ship["ship"] == expected_probe(name), path
        assert ship["orbit"]["altitude_km"] > 0, path
        if kind == "moon":
            assert path.parent.name == bodies[name]["parent"], path
        else:
            assert path.parent.name == "Planets", path
        scenarios[name] = path
assert set(scenarios) == expected, f"missing: {sorted(expected - set(scenarios))}"
assert len({path.stem for path in scenarios.values()}) == len(scenarios)

with tempfile.TemporaryDirectory() as home:
    env = dict(os.environ, HOME=home, XDG_CONFIG_HOME=str(Path(home) / ".config"),
               SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy", LP_NUM_THREADS="4")
    listing = subprocess.run([str(binary), "--root", str(root), "--list-scenarios"],
                             cwd=root, env=env, capture_output=True, text=True, timeout=120)
    assert listing.returncode == 0, listing.stdout + listing.stderr
    for name, path in scenarios.items():
        group = "Planets" if bodies[name]["kind"] == "planet" else "Moons/" + bodies[name]["parent"]
        assert any(line.startswith(path.stem) and group in line for line in listing.stdout.splitlines()), (path, group)
        result = subprocess.run(
            [str(binary), "--root", str(root), "--scenario", path.stem, "--selftest", "10"],
            cwd=root, env=env, capture_output=True, text=True, timeout=120,
        )
        output = result.stdout + result.stderr
        assert result.returncode == 0 and f"crashed 0, soi {name}\n" in output, output
        match = re.search(r"final: alt\s+([-\d.]+) km, apoapsis\s+([-\d.]+) km, periapsis\s+([-\d.]+) km, ecc\s+([\d.]+)", output)
        assert match and all(float(match.group(i)) > 0 for i in (1, 2, 3)) and float(match.group(4)) < 1, output
print(f"planet_moon_scenarios_test ok ({len(scenarios)} scenarios)")
