#!/usr/bin/env python3
"""Exercise scenario startup and reject missing scenario dependencies."""

import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "build/kosmos"
GALE = ROOT / "scenarios/CVEL/CVEL-Ares/ares_spider_gale_crater.json"
ORBIT = ROOT / "scenarios/CVEL/CVEL-Ares/ares_spider_mars_orbit.json"


def run(*args):
    return subprocess.run(
        [str(BINARY), *args, "--selftest", "1"],
        cwd=ROOT, capture_output=True, text=True, timeout=120,
    )


def main():
    scenario = json.loads(GALE.read_text())
    assert scenario["ships"][scenario["active"]] == {
        "ship": "Ares Spider", "body": "Mars", "surface": {"base": "gale"},
    }
    result = run("--scenario", "ares_spider_gale_crater")
    assert result.returncode == 0 and "soi Mars" in result.stdout, result.stdout + result.stderr
    assert "m=  30000" in result.stdout, result.stdout
    orbit = json.loads(ORBIT.read_text())
    assert orbit["ships"][orbit["active"]] == {
        "ship": "Ares Spider", "body": "Mars",
        "orbit": {"altitude_km": 200, "inclination": 0, "phase": 0},
    }
    result = run("--scenario", "ares_spider_mars_orbit")
    assert result.returncode == 0 and "soi Mars" in result.stdout, result.stdout + result.stderr
    assert "alt=199." in result.stdout and "m=  30000" in result.stdout, result.stdout

    result = run("--scenario", "missing-scenario-regression-test")
    assert result.returncode != 0 and 'scenario "missing-scenario-regression-test" not found' in result.stderr, result.stderr
    assert "final:" not in result.stdout, result.stdout

    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "missing-dependency.json"
        for entries, error in [
            ([{"ship": "Missing Active Ship", "body": "Earth", "surface": {}}],
             'unknown ship "Missing Active Ship"'),
            ([{"ship": "Ares Spider", "body": "Mars", "surface": {}},
              {"ship": "Missing Docked Ship", "body": "Mars", "surface": {},
               "dock": {"to": 0, "port": 0, "own_port": 0}}],
             'unknown ship "Missing Docked Ship"'),
            ([{"ship": "Ares Spider", "body": "Missing Planet", "surface": {}}],
             'unknown body "Missing Planet"'),
        ]:
            path.write_text(json.dumps({"ships": entries}))
            result = run("--scenario-file", str(path))
            assert result.returncode != 0 and error in result.stderr, result.stdout + result.stderr
            assert "final:" not in result.stdout, result.stdout
    print("scenario_start_test ok")


if __name__ == "__main__":
    main()
