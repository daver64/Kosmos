#!/usr/bin/env python3
"""Verify the flight recording format and rejection of invalid recordings."""

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
    recording = Path(home) / "flight.json"

    def run(setting):
        result = subprocess.run(
            [str(BINARY), "--ship", "CVEL Deltaglider", "--orbit", "--selftest", "1",
             "--selftest-timed-burn", "--set", setting],
            cwd=ROOT, env=env, capture_output=True, text=True, timeout=120,
        )
        assert result.returncode == 0 and "timed burn selftest: 0 failure(s)" in result.stdout, result.stdout + result.stderr
        return result.stdout

    output = run(f"/selftest/flight-recording-path={recording}")
    assert "ok  : flight recording persists frames, controls, camera and scenario" in output, output
    data = json.loads(recording.read_text())
    assert data["format"] == "kosmos-flight-recording" and len(data["frames"]) == 2
    data["frames"][1]["elapsed"] = 0
    invalid = Path(home) / "invalid.json"
    invalid.write_text(json.dumps(data))
    output = run(f"/selftest/invalid-flight-recording={invalid}")
    assert "ok  : invalid flight recording is rejected with a reason" in output, output
    data["frames"][1]["elapsed"] = 1
    data["frames"][0]["scenario"]["active"] = 999
    invalid.write_text(json.dumps(data))
    output = run(f"/selftest/invalid-flight-recording={invalid}")
    assert "ok  : invalid flight recording is rejected with a reason" in output, output
    for index, args in enumerate((["--ship", "CVEL Deltaglider", "--orbit"],
                                  ["--scenario", "shuttle_a_cargo_helsinki"],
                                  ["--ship", "CVEL Proton-K", "--orbit"])):
        playback = Path(home) / f"playback-{index}.json"
        result = subprocess.run(
            [str(BINARY), *args, "--selftest", "1", "--set", f"/selftest/flight-replay-path={playback}"],
            cwd=ROOT, env=env, capture_output=True, text=True, timeout=120,
        )
        assert result.returncode == 0 and "flight playback selftest: 0 failure(s)" in result.stdout, result.stdout + result.stderr
        reopened = subprocess.run(
            [str(BINARY), *args, "--selftest", "1", "--set", f"/selftest/open-flight-replay-path={playback}"],
            cwd=ROOT, env=env, capture_output=True, text=True, timeout=120,
        )
        assert reopened.returncode == 0 and "flight playback reopen selftest: 0 failure(s)" in reopened.stdout, reopened.stdout + reopened.stderr
    print("flight_recording_test ok")
