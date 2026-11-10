#!/usr/bin/env python3
"""Exercise Soyuz base targeting and cancellation in an isolated configuration."""

import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "build/kosmos"

with tempfile.TemporaryDirectory() as home:
    env = dict(os.environ, HOME=home, XDG_CONFIG_HOME=f"{home}/config",
               XDG_CACHE_HOME=f"{home}/cache", SDL_VIDEODRIVER="offscreen",
               SDL_AUDIODRIVER="dummy", LP_NUM_THREADS="4")
    original = Path(home) / "soyuz-original-orbit.json"
    original.write_text(json.dumps({
        "name": "Soyuz original orbit",
        "ships": [{"ship": "CVEL Soyuz TMA", "body": "Earth",
                   "orbit": {"altitude_km": 400, "inclination": 51.6, "phase": 180}}],
    }))
    for fault in ("", "weather", "gravity", "fuel", "base", "stage", "addon", "orbit",
                  "stage-change", "sas", "competing", "cancel", "disarm"):
        args = [str(BINARY), "--scenario-file", str(original), "--selftest", "1",
                "--set", "/selftest/soyuz-autoland=true"]
        if fault:
            args += ["--set", f"/selftest/soyuz-fault={fault}"]
        result = subprocess.run(args, cwd=BINARY.parent, env=env, capture_output=True,
                                text=True, timeout=1200)
        output = result.stdout + result.stderr
        assert result.returncode == 0, output
        if not fault:
            match = re.search(
                r"Soyuz autoland result: Autoland: Soyuz chute deployed within 100 km \(\d+ km from base\) "
                r"distance ([\d.]+) stage 2 crashed 0 throttle 0.0", output)
            assert match and float(match[1]) <= 100000.0, output
            for phase in ("Soyuz planning", "Soyuz search", "Soyuz waiting", "Soyuz checking", "Soyuz deorbit", "Soyuz entry"):
                assert f"Soyuz autoland phase: {phase}" in output, output
        print(f"Soyuz autoland {fault or 'Kennedy'}: passed", flush=True)

    scenario = Path(home) / "soyuz-alternative.json"
    scenario.write_text(json.dumps({
        "name": "Soyuz alternative orbit",
        "ships": [{"ship": "CVEL Soyuz TMA", "body": "Earth",
                   "orbit": {"altitude_km": 400, "inclination": 35, "phase": 90}}],
    }))
    args = [str(BINARY), "--scenario-file", str(scenario), "--selftest", "1",
            "--set", "/selftest/soyuz-autoland=true", "--set", "/selftest/soyuz-base=kennedy"]
    result = subprocess.run(args, cwd=BINARY.parent, env=env, capture_output=True,
                            text=True, timeout=1200)
    output = result.stdout + result.stderr
    assert result.returncode == 0, output
    match = re.search(r"Soyuz autoland result: Autoland: Soyuz chute deployed within 100 km \(\d+ km from base\) distance ([\d.]+)", output)
    assert match and float(match[1]) <= 100000.0, output
    print("Soyuz autoland Kennedy, 35-degree orbit: passed", flush=True)

    args = [str(BINARY), "--scenario-file", str(original), "--selftest", "1",
            "--set", "/selftest/soyuz-autoland=true", "--set", "/selftest/soyuz-base=baikonur"]
    result = subprocess.run(args, cwd=BINARY.parent, env=env, capture_output=True,
                            text=True, timeout=1200)
    output = result.stdout + result.stderr
    assert result.returncode == 0, output
    match = re.search(r"Soyuz autoland result: Autoland: Soyuz chute deployed within 100 km \(\d+ km from base\) distance ([\d.]+)", output)
    assert match and float(match[1]) <= 100000.0, output
    print("Soyuz autoland Baikonur: passed", flush=True)

    for base in ("twickenham", "chippenham"):
        args = [str(BINARY), "--scenario", "soyuz_tma_orbit", "--selftest", "1",
                "--set", "/selftest/soyuz-autoland=true", "--set", f"/selftest/soyuz-base={base}"]
        result = subprocess.run(args, cwd=BINARY.parent, env=env, capture_output=True,
                                text=True, timeout=1200)
        output = result.stdout + result.stderr
        assert result.returncode == 0, output
        match = re.search(
            r"Soyuz autoland result: Autoland: Soyuz chute deployed within 100 km \(\d+ km from base\) "
            r"distance ([\d.]+) stage 2 crashed 0 throttle 0.0", output)
        assert match and float(match[1]) <= 100000.0, output
        departure = re.search(r"Soyuz autoland phase: Soyuz deorbit time ([\d.]+)", output)
        assert departure and 0 < float(departure[1]) < 5545, output
        print(f"Soyuz autoland {base}: passed", flush=True)

    args = [str(BINARY), "--scenario-file", str(scenario), "--selftest", "1",
            "--set", "/selftest/soyuz-autoland=true"]
    args += ["--set", "/selftest/soyuz-base=kourou", "--set", "/selftest/soyuz-fault=unreachable"]
    result = subprocess.run(args, cwd=BINARY.parent, env=env, capture_output=True,
                            text=True, timeout=1200)
    assert result.returncode == 0, result.stdout + result.stderr
    print("Soyuz autoland unsuitable window: passed", flush=True)

    docked = Path(home) / "soyuz-docked.json"
    orbit = {"altitude_km": 400, "inclination": 51.6, "phase": 180}
    docked.write_text(json.dumps({
        "name": "Docked Soyuz", "active": 1,
        "ships": [{"ship": "CVEL Soyuz TMA", "body": "Earth", "orbit": orbit},
                  {"ship": "CVEL Soyuz TMA", "body": "Earth", "orbit": orbit,
                   "dock": {"to": 0, "port": 0, "own_port": 0}}],
    }))
    args = [str(BINARY), "--scenario-file", str(docked), "--selftest", "1",
            "--set", "/selftest/soyuz-autoland=true", "--set", "/selftest/soyuz-fault=dock"]
    result = subprocess.run(args, cwd=BINARY.parent, env=env, capture_output=True,
                            text=True, timeout=1200)
    assert result.returncode == 0, result.stdout + result.stderr
    print("Soyuz autoland docked: passed", flush=True)
