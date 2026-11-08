#!/usr/bin/env python3
"""Exercise the Energia-M payload handoff in physics and on rails."""

import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "build/kosmos"
SCENARIO = ROOT / "scenarios/CVEL/CVEL-Energia/energia_m_baikonur.json"


def main():
    scenario = json.loads(SCENARIO.read_text())
    ships = scenario["ships"]
    assert [ship["ship"] for ship in ships] == [
        "CVEL Energia-M", "CVEL Centaur G", "CVEL Fregat", "Luna E3"
    ]
    for ship in ships:
        ship.pop("surface", None)
        ship["orbit"] = {"altitude_km": 400, "inclination": 51.6}
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "handoff.json"
        path.write_text(json.dumps({"active": 0, "ships": ships}))
        for rails in (False, True):
            command = [str(BINARY), "--root", str(ROOT), "--scenario-file", str(path),
                       "--selftest", "1", "--set", "/selftest/centaur-handoff=true"]
            if rails:
                command.extend(["--set", "/selftest/centaur-handoff-rails=true"])
            result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=30)
            assert result.returncode == 0 and "Centaur handoff selftest ok" in result.stdout, result.stdout + result.stderr
    print("centaur_handoff_test ok")


if __name__ == "__main__":
    main()
