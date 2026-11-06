#!/usr/bin/env python3
"""Exercise Atlas staging and its docked payload through the launcher autopilot."""

import json
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCENARIO = ROOT / "scenarios/CVEL/CVEL-Atlas/atlas_iii_kennedy.json"
BINARY = ROOT / "build/kosmos"
REPORT = re.compile(
    r"^t=\s*(\d+).*stage=(\d+) fuel=\s*(\d+).*active=(\d+) docked=(\d+)",
    re.MULTILINE,
)


def check_scenario(path, active):
    result = subprocess.run(
        [str(BINARY), "--scenario-file", str(path), "--selftest", "220",
         "--set", "/autopilot/enabled=1"],
        cwd=ROOT, capture_output=True, text=True, timeout=120, check=True,
    )
    output = result.stdout + result.stderr
    assert "script error" not in output.lower() and "CRASHED" not in output, output
    reports = [tuple(map(int, match)) for match in REPORT.findall(result.stdout)]
    assert reports and all(r[3:] == (active, 1) for r in reports), output
    burning = [r for r in reports if r[1] == 3]
    assert len(burning) >= 3 and burning[-1][2] < burning[0][2] - 500, output


def main():
    check_scenario(SCENARIO, 0)
    scenario = json.loads(SCENARIO.read_text())
    scenario["ships"].insert(0, {
        "ship": "Luna E3", "body": "Earth", "orbit": {"altitude_km": 400},
    })
    scenario["active"] = 1
    scenario["ships"][2]["dock"]["to"] = 1
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "atlas-active-second.json"
        path.write_text(json.dumps(scenario))
        check_scenario(path, 1)
    print("atlas_autopilot_test ok")


if __name__ == "__main__":
    main()
