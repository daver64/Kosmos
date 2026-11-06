#!/usr/bin/env python3
"""Check that every bundled scenario's ships are supplied by an installed addon."""

import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
ships = set()
for manifest in (root / "addons").glob("*/addon.json"):
    for filename in json.loads(manifest.read_text()).get("ships", []):
        ship = json.loads((manifest.parent / filename).read_text())
        ships.add(ship["name"])

scenarios = list((root / "scenarios").rglob("*.json"))
scenarios += list((root / "addons").glob("*/scenarios/**/*.json"))
errors = []
for path in scenarios:
    scenario = json.loads(path.read_text())
    for ship in scenario["ships"]:
        if ship["ship"] not in ships:
            errors.append(f"{path.relative_to(root)}: missing ship {ship['ship']}")

if errors:
    raise SystemExit("\n".join(errors))
print(f"scenario_addons_test ok ({len(scenarios)} scenarios, {len(ships)} ship classes)")
