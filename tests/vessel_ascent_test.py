#!/usr/bin/env python3
"""Exercise the ship-specific ascent modes with headless surface-to-orbit flights."""

import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "build/kosmos"
FINAL = re.compile(
    r"final: alt ([-\d.]+) km, apoapsis ([-\d.]+) km, periapsis ([-\d.]+) km, "
    r"ecc ([\d.]+), crashed (\d+), soi (\S+)"
)
PRESSURE = re.compile(r"ascent: Climb q=([\d.]+) Pa throttle=([\d.]+)")
RESULT = re.compile(
    r"ascent result: Ascent complete: orbit reached ap=([-\d.]+) km "
    r"pe=([-\d.]+) km bound=(\d+) fuel=([\d.]+)"
)


def run(args, seconds):
    with tempfile.TemporaryDirectory() as home:
        env = dict(os.environ, HOME=home, XDG_CONFIG_HOME=str(Path(home) / ".config"), KOSMOS_SELFTEST_DT="0.1")
        return subprocess.run(
            [str(BINARY), *args, "--selftest", str(seconds)],
            cwd=ROOT, capture_output=True, text=True, timeout=600, env=env, check=True,
        )


def main():
    variants_only = "--variants" in sys.argv[2:]
    additional_only = "--additional" in sys.argv[2:]
    standard_only = "--standard" in sys.argv[2:]
    if not variants_only and not additional_only:
        checks = run(["--scenario", "ares_spider_gale_crater", "--selftest-vessel-ascent"], 1)
        assert "vessel ascent selftest: 0 failure(s)" in checks.stdout, checks.stdout + checks.stderr
        for args in (["--ship", "CVEL Deltaglider"], ["--scenario", "deltaglider_chippenham"]):
            checks = run([*args, "--selftest-vessel-ascent"], 1)
            assert "vessel ascent selftest: 0 failure(s)" in checks.stdout, checks.stdout + checks.stderr

    standard = [
        ("Ares", ["--scenario", "ares_spider_gale_crater"], "Mars", 200, 90),
        ("Shuttle-A", ["--ship", "CVEL Shuttle-A"], "Earth", 200, 90),
        ("Deltaglider", ["--ship", "CVEL Deltaglider"], "Earth", 200, 90),
    ]
    variants = [
        ("Deltaglider", ["--scenario", "deltaglider_chippenham"], "Earth", 200, 90),
        ("Shuttle-A", ["--ship", "CVEL Shuttle-A (Nuclear)"], "Earth", 200, 90),
        ("Shuttle-A", ["--ship", "CVEL Shuttle-AQ"], "Earth", 200, 90),
    ]
    additional = [
        ("Ares", ["--scenario", "ares_spider_gale_crater"], "Mars", 150, 70),
        ("Shuttle-A", ["--ship", "CVEL Shuttle-A"], "Earth", 250, 110),
        ("Deltaglider", ["--ship", "CVEL Deltaglider"], "Earth", 150, 90),
        ("Deltaglider", ["--ship", "CVEL Deltaglider"], "Earth", 200, 110),
    ]
    if variants_only:
        cases = variants
    elif additional_only:
        cases = additional
    elif standard_only:
        cases = standard
    else:
        cases = standard + variants + additional
    for profile, args, body, target, heading in cases:
        result = run([*args, "--set", f"/autopilot/vessel-ascent/target-km={target}",
                      "--set", f"/autopilot/vessel-ascent/heading={heading}",
                      "--set", "/autopilot/vessel-ascent/request=" + profile], 1800)
        output = result.stdout + result.stderr
        match = FINAL.search(result.stdout)
        achieved = RESULT.search(result.stdout)
        assert match and achieved, output
        periapsis, eccentricity = map(float, match.group(3, 4))
        assert match.group(5) == "0" and match.group(6) == body, output
        achieved_ap, achieved_pe, fuel = map(float, achieved.group(1, 2, 4))
        assert achieved.group(3) == "1" and target - 5 <= achieved_ap <= target + 15, output
        assert target - 5.1 <= achieved_pe <= target + 5 and fuel > 100, output
        assert periapsis > 0 and eccentricity < 1, output
        if heading != 90:
            headings = [float(value) for value in re.findall(
                r"ascent: (?:Climb|Orbit burn).* heading=([-\d.]+)", result.stdout)]
            assert any(abs((value - heading + 180) % 360 - 180) < 5 for value in headings), output
        if profile == "Deltaglider" and args[0] == "--ship":
            pressure = [(float(q), float(throttle)) for q, throttle in PRESSURE.findall(result.stdout)]
            assert any(q > 70000 and throttle < 0.999 for q, throttle in pressure), output
        print(f"{profile} {args[-1]}: {body} orbit {achieved_pe:.1f} x {achieved_ap:.1f} km, fuel {fuel:.0f}")

    if variants_only or additional_only:
        print("vessel_ascent_test selected flights ok")
        return
    for profile, args in [
        ("Ares", ["--ship", "Ares Spider"]),
        ("Shuttle-A", ["--ship", "CVEL Shuttle-A", "--orbit-body", "Mars"]),
    ]:
        result = run([*args, "--set", "/autopilot/vessel-ascent/request=" + profile], 1)
        assert "Ascent not started:" in result.stderr, result.stdout + result.stderr
    print("vessel_ascent_test ok")


if __name__ == "__main__":
    main()
