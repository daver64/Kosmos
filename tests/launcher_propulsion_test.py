#!/usr/bin/env python3
"""Check historical propulsion corrections across the affected ship variants."""

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def stage(path, name):
    ship = json.loads((ROOT / "addons" / path).read_text())
    return next(item for item in ship["stages"] if item["name"] == name)


for path in ("cvel-saturn-v/ship.json", "cvel-saturn-v/payload.json"):
    engines = stage(path, "S-IC first stage")["engines"]
    assert len(engines) == 5
    assert all(engine["fuel"] == "kerosene" and engine["thrust"] == 7740632 for engine in engines), path

for path in ("cvel-saturn-v/ship.json", "cvel-saturn-v23/ship.json",
             "cvel-saturn-mlv1/ship.json", "cvel-saturn-mlv1/ship_nerva.json",
             "cvel-saturn-mlv2/ship.json", "cvel-saturn-mlv3/ship.json"):
    engines = stage(path, "Service module")["engines"]
    assert len(engines) == 1
    assert engines[0]["fuel"] == "hypergolic" and engines[0]["thrust"] == 91187
    assert engines[0]["isp"] == 314.5, path

engines = stage("cvel-buran/ship.json", "Buran")["engines"]
assert len(engines) == 2
assert all(engine["fuel"] == "kerosene" and engine["thrust"] == 90000 and engine["isp"] == 362
           for engine in engines)

engines = stage("cvel-delta-iv/ship.json", "Second stage")["engines"]
assert len(engines) == 1 and engines[0]["fuel"] == "hydrogen" and engines[0]["thrust"] == 110000

engines = stage("cvel-centaur/ship.json", "Centaur G")["engines"]
assert len(engines) == 2 and all(engine["fuel"] == "hydrogen" and engine["thrust"] == 73400
                                 for engine in engines)

engines = stage("cvel-titan/titan1.json", "Stage 1")["engines"]
assert len(engines) == 2 and all(engine["fuel"] == "kerosene" and engine["thrust"] == 733954
                                 for engine in engines)
engines = stage("cvel-titan/titan3b.json", "Agena")["engines"]
assert len(engines) == 1 and engines[0]["fuel"] == "hypergolic"
assert engines[0]["thrust"] == 71200 and engines[0]["isp"] == 293

print("launcher_propulsion_test ok")
