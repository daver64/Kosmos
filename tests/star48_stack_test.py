#!/usr/bin/env python3
"""Check both Star 48 payload stacks, their burns and staging handoffs."""

import os
import json
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
binary = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build/kosmos"
addon = root / "addons/cvel-star48"
for filename, expected in (
    ("star48b.json", (126, 2011, 66000, 292)),
    ("star37fm.json", (81, 1066, 47900, 273)),
):
    ship = json.loads((addon / filename).read_text())
    stage = ship["stages"][0]
    engine = stage["engines"][0]
    assert (stage["dry_mass"], stage["propellant"], engine["thrust"], engine["isp"]) == expected
    mesh = addon / stage["parts"][0]["model"]
    lines = mesh.read_text().splitlines()
    assert any(line.startswith("v ") for line in lines) and any(line.startswith("f ") for line in lines)
    material = mesh.parent / next(line.split(maxsplit=1)[1] for line in lines if line.startswith("mtllib "))
    for line in material.read_text().splitlines():
        if line.startswith("map_Kd "):
            assert (material.parent / line.split(maxsplit=1)[1]).is_file()
with tempfile.TemporaryDirectory() as home:
    env = dict(os.environ, HOME=home, XDG_CONFIG_HOME=str(Path(home) / ".config"))
    for scenario in ("star48b_orbit", "delta_iv_star48_kennedy"):
        result = subprocess.run(
            [str(binary), "--root", str(root), "--scenario", scenario, "--selftest", "1",
             "--set", "/selftest/star48-stack=true"],
            cwd=root, env=env, capture_output=True, text=True, timeout=60,
        )
        if result.returncode or "Star 48 stack selftest ok" not in result.stdout:
            raise SystemExit(result.stdout + result.stderr)
print("star48_stack_test ok")
