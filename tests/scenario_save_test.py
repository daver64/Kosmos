#!/usr/bin/env python3
"""Check saved flight state through scenario serialization and restoration."""

import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "build/kosmos"

with tempfile.TemporaryDirectory() as home:
    env = dict(os.environ, HOME=home, XDG_CONFIG_HOME=str(Path(home) / ".config"))
    approach = Path(home) / "approach.json"
    approach.write_text(json.dumps({
        "name": "Save approach",
        "ships": [{"ship": "CVEL Deltaglider", "body": "Earth",
                   "surface": {"latitude": 51.0, "longitude": -2.0, "heading": 90,
                               "altitude": 1500, "speed": 120}}],
    }))
    for index, args in enumerate((
        ["--ship", "CVEL Proton-K", "--orbit"],
        ["--ship", "CVEL Deltaglider", "--orbit-body", "Mars", "--orbit-alt", "20000"],
        ["--scenario", "ares_spider_gale_crater"],
        ["--scenario", "shuttle_a_cargo_helsinki"],
        ["--scenario-file", str(approach)],
    )):
        saved = Path(home) / f"saved-{index}.json"
        result = subprocess.run(
            [str(BINARY), *args, "--selftest-save", "--selftest", "1",
             "--set", f"/selftest/write-saved-flight={saved}"],
            cwd=ROOT, capture_output=True, text=True, timeout=120, env=env,
        )
        assert result.returncode == 0 and "save selftest: 0 failure(s)" in result.stdout, result.stdout + result.stderr
        reopened = subprocess.run(
            [str(BINARY), "--scenario-file", str(saved), "--selftest-save", "--selftest", "1",
             "--set", f"/selftest/verify-saved-flight={saved}"],
            cwd=ROOT, capture_output=True, text=True, timeout=120, env=env,
        )
        assert reopened.returncode == 0 and "save startup selftest: 0 failure(s)" in reopened.stdout, reopened.stdout + reopened.stderr
        print(f"scenario save: {' '.join(args)} passed", flush=True)

    invalid = Path(home) / "invalid-save.json"
    invalid.write_text(json.dumps({
        "ships": [{"ship": "CVEL Deltaglider", "body": "Earth",
                   "orbit": {"altitude_km": 200},
                   "flight_state": {"active_stage": 999, "stage_propellant": [],
                                    "rcs_propellant": 0, "gear_position": 1,
                                    "animation_positions": [], "health": 1, "shield": 0}}],
    }))
    result = subprocess.run(
        [str(BINARY), "--scenario-file", str(invalid), "--selftest", "1"],
        cwd=ROOT, capture_output=True, text=True, timeout=120, env=env,
    )
    assert result.returncode != 0 and "saved flight state does not match" in result.stderr, result.stdout + result.stderr
    print("scenario save: incompatible state rejected", flush=True)
