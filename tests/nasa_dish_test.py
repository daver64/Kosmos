#!/usr/bin/env python3
"""Check the reusable NASA dish and its four spaceport placements."""

import json
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
binary = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build/kosmos"
model = "parts/nasa_dish_70m/nasa_dish_70m.glb"
data = (root / "data" / model).read_bytes()
magic, version, length = struct.unpack_from("<4sII", data)
assert magic == b"glTF" and version == 2 and length == len(data)
chunk_length, chunk_type = struct.unpack_from("<I4s", data, 12)
assert chunk_type == b"JSON"
document = json.loads(data[20:20 + chunk_length])
assert not document.get("extensionsRequired")
assert all("bufferView" in image for image in document.get("images", []))
primitive = document["meshes"][0]["primitives"][0]
position = document["accessors"][primitive["attributes"]["POSITION"]]
minimum, maximum = position["min"], position["max"]
assert abs(maximum[0] - minimum[0] - 70) < 0.1
assert abs(maximum[2] - minimum[2] - 70) < 0.3
assert abs(minimum[1]) < 0.01 and 60 < maximum[1] < 70

with tempfile.TemporaryDirectory() as home:
    env = dict(os.environ, HOME=home, XDG_CONFIG_HOME=str(Path(home) / ".config"),
               SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy", LP_NUM_THREADS="4")
    for base_id in ("chippenham", "twickenham", "helsinki", "wagga"):
        base = json.loads((root / "data/bases" / f"{base_id}.json").read_text())["bases"][0]
        dishes = [obj for obj in base["objects"] if obj.get("model") == model]
        assert len(dishes) == 1 and dishes[0]["type"] == "model"
        assert dishes[0].get("scale", 1) == 1
        scenario = Path(home) / f"{base_id}.json"
        scenario.write_text(json.dumps({"active": 0, "ships": [
            {"ship": "Kestrel", "body": "Earth", "surface": {"base": base_id}}
        ]}))
        shot = Path(home) / f"{base_id}.png"
        result = subprocess.run(
            [str(binary), "--root", str(root), "--scenario-file", str(scenario),
             "--base-cam", f"{base_id}:3", "--date", "2024-06-21 12:00:00",
             "--no-clouds", "--gen-threads", "1", "--shot", str(shot), "--shot-at", "2"],
            cwd=root, env=env, capture_output=True, text=True, timeout=120,
        )
        output = result.stdout + result.stderr
        assert result.returncode == 0 and shot.is_file(), output
        assert "cannot load base model" not in output and "model '" + model + "' not found" not in output, output
print("nasa_dish_test ok")
