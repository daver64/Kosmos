#!/usr/bin/env python3
"""Make an isolated addon root for the ordnance selftests."""

import json
import sys
from pathlib import Path

source = Path(__file__).resolve().parents[1]
root = Path(sys.argv[1])
root.mkdir()
for name in ("data", "scenarios"):
    (root / name).symlink_to(source / name, target_is_directory=True)
addons = root / "addons"
addons.mkdir()
for addon in (source / "addons").iterdir():
    if addon.is_dir():
        (addons / addon.name).symlink_to(addon, target_is_directory=True)
fixture = addons / "ordnance-fixture"
fixture.mkdir()
ship = json.loads((source / "addons/cvel-deltaglider/ship.json").read_text())
ship["name"] = "Ordnance test craft"
ship["hardpoints"] = [
    {"name": "Bomb", "equipment": "bomb", "count": 1, "position": [0, 0, 0]},
    {"name": "Atom", "equipment": "fatman", "count": 1, "position": [0, 0, 0]},
]
(fixture / "ship.json").write_text(json.dumps(ship))
(fixture / "addon.json").write_text(json.dumps({"id": "ordnance-fixture", "ships": ["ship.json"]}))
scenario = {
    "name": "Ordnance test",
    "ships": [{"ship": ship["name"], "body": "Earth",
               "surface": {"latitude": 62.0, "longitude": -19.0, "heading": 0,
                           "altitude": 2438, "speed": 220}}],
}
(root / "ordnance-test.json").write_text(json.dumps(scenario))
