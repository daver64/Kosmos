#!/usr/bin/env python3
"""Verify an engine plume is visible and rendered in a bounded batch."""

import json
import math
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

root = Path(__file__).resolve().parents[1]
binary = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build/kosmos"

spec = json.loads((root / "addons/cvel-fregat/ship.json").read_text())
engine = spec["stages"][0]["engines"][0]
part = spec["stages"][0]["parts"][0]
model = root / "addons/cvel-fregat" / part["model"]
vertices, uvs, engine_points = [], [], []
materials, active_material = set(), ""


def ship_point(point):
    scale = part.get("scale", [1, 1, 1])
    if isinstance(scale, (int, float)):
        scale = [scale] * 3
    p = [point[i] * scale[i] for i in range(3)]
    for axis, degrees in enumerate(part.get("rotate", [0, 0, 0])):
        a, b = (axis + 1) % 3, (axis + 2) % 3
        c, s = math.cos(math.radians(degrees)), math.sin(math.radians(degrees))
        p[a], p[b] = c * p[a] - s * p[b], s * p[a] + c * p[b]
    return tuple(p[i] + part.get("offset", [0, 0, 0])[i] for i in range(3))


for line in model.read_text().splitlines():
    fields = line.split()
    if not fields:
        continue
    if fields[0] == "v":
        vertices.append(ship_point(tuple(map(float, fields[1:4]))))
    elif fields[0] == "vt":
        uvs.append(tuple(map(float, fields[1:3])))
    elif fields[0] == "mtllib":
        material_file = model.parent / fields[1]
    elif fields[0] == "usemtl":
        active_material = fields[1]
        materials.add(active_material)
    elif fields[0] == "f":
        for token in fields[1:]:
            indices = token.split("/")
            assert 0 < int(indices[0]) <= len(vertices) and len(indices) > 1 and 0 < int(indices[1]) <= len(uvs)
            if active_material == "Engine_Interior":
                engine_points.append(vertices[int(indices[0]) - 1])

defined_materials = set()
for line in material_file.read_text().splitlines():
    fields = line.split()
    if not fields:
        continue
    if fields[0] == "newmtl":
        defined_materials.add(fields[1])
    elif fields[0] in {"map_Kd", "map_Bump"}:
        with Image.open(material_file.parent / fields[-1]) as texture:
            texture.verify()
assert materials <= defined_materials and engine_points
assert abs(max(p[2] for p in engine_points) - engine["position"][2]) < 0.02
assert all(min(p[i] for p in engine_points) <= engine["position"][i] <= max(p[i] for p in engine_points)
           for i in (0, 1))
ports = {port["name"]: port for port in spec["ports"]}
assert ports["Aft"]["position"][2] + 1e-6 >= max(p[2] for p in vertices)
assert ports["Payload"]["position"][2] - 1e-6 <= min(p[2] for p in vertices)
assert spec["payload_size"]["diameter"] >= 2 * max(math.hypot(p[0], p[1]) for p in vertices)
assert spec["payload_size"]["length"] >= max(p[2] for p in vertices) - min(p[2] for p in vertices)
assert engine["fuel"] == "hypergolic"
# Full-throttle vacuum dimensions before particle spreading, using the hypergolic style.
assert 1.0 <= engine["plume"] * 0.8 * 1.6 <= 2.0
assert 0.2 <= engine["plume_width"] * 0.85 * 4.3 <= 0.5

with tempfile.TemporaryDirectory() as home:
    temp = Path(home)
    env = dict(os.environ, HOME=home, XDG_CONFIG_HOME=str(temp / "config"),
               XDG_CACHE_HOME=str(temp / "cache"), SDL_VIDEODRIVER="offscreen",
               SDL_AUDIODRIVER="dummy", LIBGL_ALWAYS_SOFTWARE="1", LP_NUM_THREADS="4")
    shot = temp / "exhaust.png"
    result = subprocess.run(
        [str(binary), "--root", str(root), "--ship", "CVEL Fregat", "--orbit-body", "Mars",
         "--orbit-alt", "200", "--no-clutter", "--no-tiles", "--no-clouds", "--gen-threads", "1",
         "--date", "2024-06-21 15:00:00", "--perf", "--set", "/controls/throttle=1",
         "--shot", str(shot), "--shot-at", "6"],
        cwd=root, env=env, capture_output=True, text=True, timeout=120,
    )
    output = result.stdout + result.stderr
    assert result.returncode == 0 and shot.is_file(), output
    counts = [tuple(map(int, match)) for match in
              re.findall(r"\[perf\] 3D draws (\d+), instanced draws (\d+), instances (\d+)", output)]
    assert any(meshes < 250 and draws >= 1 and instances >= 100 for meshes, draws, instances in counts), counts
    assert "exhaust instancing unavailable" not in output, output
    with Image.open(shot) as source:
        pixels = source.convert("RGB").crop((675, 275, 840, 420)).getdata()
        assert sum(r > 220 and g > 170 and b > 80 for r, g, b in pixels) > 100, output
    print(f"exhaust_render_test ok: {counts}")
