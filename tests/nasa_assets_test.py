#!/usr/bin/env python3
"""Check NASA asteroid scenarios, lunar terrain and Gateway docking."""

import json
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
binary = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build/kosmos"
asteroids = {
    "bennu_probe": "Bennu", "geographos_probe": "Geographos",
    "itokawa_probe": "Itokawa", "eros_probe": "Eros",
    "kleopatra_probe": "Kleopatra", "toutatis_probe": "Toutatis",
    "mithra_probe": "Mithra", "golevka_probe": "Golevka",
    "1996_hw1_probe": "1996 HW1", "vesta_probe": "Vesta",
}
bodies = {body["name"]: body for body in json.loads((root / "data/sol.json").read_text())["bodies"]}
for scenario, name in asteroids.items():
    body = bodies[name]
    assert body["kind"] == "asteroid" and body["gm"] > 0 and body["radius"] > 0
    mesh = root / "data" / body["mesh"]
    with mesh.open("rb") as stream:
        magic, version, length = struct.unpack("<4sII", stream.read(12))
        assert magic == b"glTF" and version == 2 and length == mesh.stat().st_size, mesh
        json_length, chunk_type = struct.unpack("<I4s", stream.read(8))
        assert chunk_type == b"JSON", mesh
        document = json.loads(stream.read(json_length))
        assert document.get("meshes") and document["meshes"][0]["primitives"], mesh
    source = json.loads((root / "scenarios/Asteroids" / f"{scenario}.json").read_text())
    assert source["ships"][source["active"]]["body"] == name
    assert source["ships"][source["active"]]["ship"] == "Luna E3"

base = json.loads((root / "data/bases/apollo11.json").read_text())["bases"][0]
terrain = base["terrain_meshes"][0]
assert base["id"] == "apollo11" and base["body"] == "Moon"
assert abs(terrain["scale"] * 255.5 - 30000) < 1
assert abs(terrain["vscale"] - 1 / 60) < 1e-8
assert (root / "data" / terrain["file"]).is_file()
assert (root / "addons/nasa-gateway/gateway.glb").is_file()

with tempfile.TemporaryDirectory() as home:
    env = dict(os.environ, HOME=home, XDG_CONFIG_HOME=str(Path(home) / ".config"),
               SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy", LP_NUM_THREADS="4")
    for scenario, name in [*asteroids.items(), ("gateway_shuttle_moon", "Moon")]:
        result = subprocess.run(
            [str(binary), "--root", str(root), "--scenario", scenario, "--selftest", "2"],
            cwd=root, env=env, capture_output=True, text=True, timeout=120,
        )
        output = result.stdout + result.stderr
        assert result.returncode == 0 and f"crashed 0, soi {name}" in output, output
        if scenario == "gateway_shuttle_moon":
            assert "active=1 docked=1 links=1" in output, output
    apollo = Path(home) / "apollo-site.json"
    apollo.write_text(json.dumps({"active": 0, "ships": [
        {"ship": "CVEL Saturn V", "body": "Moon", "surface": {"base": "apollo11"}}
    ]}))
    result = subprocess.run(
        [str(binary), "--root", str(root), "--scenario-file", str(apollo), "--selftest", "2"],
        cwd=root, env=env, capture_output=True, text=True, timeout=120,
    )
    output = result.stdout + result.stderr
    assert result.returncode == 0 and "crashed 0, soi Moon" in output, output
print("nasa_assets_test ok")
