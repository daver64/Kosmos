#!/usr/bin/env python3
"""Check CVELN1 geometry and its 95-tonne payload ascent."""

import json
import math
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "build/kosmos"
ADDON = ROOT / "addons/cvel-n1"
SHIP = json.loads((ADDON / "ship.json").read_text())


def check_caps(faces, ends):
    for z, radius, direction in ends:
        triangles = []
        area = 0.0
        for points, normals in faces:
            if not all(abs(p[2] - z) < 1e-5 and math.hypot(p[0], p[1]) <= radius + 1e-5
                       for p in points):
                continue
            assert len(points) == 3
            a, b, c = points
            cross = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
            assert cross * direction > 0 and all(n[2] * direction > 0.99 for n in normals)
            area += abs(cross) / 2
            triangles.append(points)
        assert 0.99 * math.pi * radius ** 2 <= area <= math.pi * radius ** 2
        for x in range(-10, 11):
            for y in range(-10, 11):
                if x * x + y * y > 90:
                    continue
                point = (radius * x / 10, radius * y / 10)
                def side(a, b):
                    return (b[0] - a[0]) * (point[1] - a[1]) - (b[1] - a[1]) * (point[0] - a[0])
                assert any(all(side(a, b) * direction >= -1e-6
                               for a, b in zip(t, t[1:] + t[:1])) for t in triangles)


def check_meshes():
    stages = SHIP["stages"]
    assert [len(stage["engines"]) for stage in stages] == [30, 8, 0, 0, 4]
    assert stages[2]["with_next"] and not stages[3]["with_next"]
    material = (ADDON / "models/n1.mtl").read_text()
    assert "map_Kd n1tex.png" in material and (ADDON / "models/n1tex.png").is_file()
    caps = {"Block A": [(-0.15, 8.1, 1), (-23.3, 5.85, -1)],
            "Block B": [(-30.1, 5.15, 1), (-46.6, 3.94, -1)],
            "Block V": [(-50.6, 3.65, 1), (-61.7, 2.75, -1)]}
    textured_surfaces = {"Block A": {"block_a_hull", "block_a_forward_cap", "block_a_engine_deck"},
                         "Block B": {"block_b_hull", "block_b_aft_cap", "block_b_forward_cap"},
                         "Block V": {"block_v_hull", "block_v_aft_cap", "block_v_forward_cap"},
                         "Shroud left": {"shroud_left"}, "Shroud right": {"shroud_right"}}
    for stage in stages:
        path = ADDON / stage["parts"][0]["model"]
        vertices, normals, texcoords, faces, shroud_x = [], [], [], 0, []
        cap_faces = []
        engine_vertices = {}
        objects = set()
        textured_objects = set()
        current_object = ""
        current_material = ""
        for line in path.read_text().splitlines():
            fields = line.split()
            if not fields:
                continue
            if fields[0] == "v":
                vertices.append(tuple(map(float, fields[1:4])))
            elif fields[0] == "vn":
                normals.append(tuple(map(float, fields[1:4])))
            elif fields[0] == "vt":
                texcoords.append(tuple(map(float, fields[1:3])))
            elif fields[0] == "o":
                current_object = fields[1]
                objects.add(fields[1])
            elif fields[0] == "usemtl":
                current_material = fields[1]
            elif fields[0] == "f":
                faces += 1
                assert len(fields) >= 4
                face_points, face_normals, face_uvs = [], [], []
                for field in fields[1:]:
                    vertex, uv, normal = field.split("/")
                    assert 0 < int(vertex) <= len(vertices) and 0 < int(normal) <= len(normals)
                    face_points.append(vertices[int(vertex) - 1])
                    face_normals.append(normals[int(normal) - 1])
                    if current_object in textured_surfaces[stage["name"]]:
                        assert current_material == "textured" and uv and 0 < int(uv) <= len(texcoords)
                        face_uvs.append(texcoords[int(uv) - 1])
                    if current_object.startswith("shroud_"):
                        shroud_x.append(vertices[int(vertex) - 1][0])
                    if "_engine_" in current_object and not current_object.endswith("_interior"):
                        engine_vertices.setdefault(current_object, []).append(vertices[int(vertex) - 1])
                if face_uvs:
                    assert all(0 <= u <= 1 and 0 <= v <= 1 for u, v in face_uvs)
                    uv_area = sum(a[0] * b[1] - b[0] * a[1]
                                  for a, b in zip(face_uvs, face_uvs[1:] + face_uvs[:1]))
                    assert abs(uv_area) > 1e-8
                    if current_object.endswith("_hull") or current_object in {"shroud_left", "shroud_right"}:
                        assert max(u for u, _ in face_uvs) - min(u for u, _ in face_uvs) < 0.02
                        a, b = face_points[:2]
                        turn = a[0] * b[1] - a[1] * b[0]
                        assert turn * (face_uvs[1][0] - face_uvs[0][0]) < 0
                    textured_objects.add(current_object)
                cap_faces.append((face_points, face_normals))
        assert vertices and faces and (path.parent / "n1.mtl").exists()
        assert textured_objects == textured_surfaces[stage["name"]]
        if stage["name"] in caps:
            check_caps(cap_faces, caps[stage["name"]])
        if stage["name"] == "Shroud left":
            assert shroud_x and max(shroud_x) < 0.001
        elif stage["name"] == "Shroud right":
            assert shroud_x and min(shroud_x) > -0.001
        if stage["engines"]:
            prefix = {"Block A": "a", "Block B": "b", "Block V": "v"}[stage["name"]]
            assert all(f"{prefix}_engine_{i:02}" in objects for i in range(1, len(stage["engines"]) + 1))
            for i, engine in enumerate(stage["engines"], 1):
                points = engine_vertices[f"{prefix}_engine_{i:02}"]
                for axis in (0, 1):
                    centre = (min(p[axis] for p in points) + max(p[axis] for p in points)) / 2
                    assert abs(centre - engine["position"][axis]) < 0.001
                assert abs(max(p[2] for p in points) - engine["position"][2]) < 0.001
    all_z = []
    for part in ("block_a", "block_b", "block_v", "shroud_left", "shroud_right"):
        all_z.extend(float(line.split()[3]) for line in (ADDON / "models" / (part + ".obj")).read_text().splitlines()
                     if line.startswith("v "))
    assert abs(min(all_z) + 104.9) < 0.01 and abs(max(all_z)) < 0.2


def run_flight():
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        for name in ("data", "scenarios"):
            (root / name).symlink_to(ROOT / name, target_is_directory=True)
        addons = root / "addons"
        addons.mkdir()
        for addon in (ROOT / "addons").iterdir():
            if addon.is_dir():
                (addons / addon.name).symlink_to(addon, target_is_directory=True)
        fixture = addons / "n1-test-payload"
        fixture.mkdir()
        payload = {
            "name": "N1 test payload 95000kg", "class": "payload", "drag_area": 1,
            "payload_size": {"diameter": 3, "length": 6},
            "ports": [{"name": "Aft", "position": [0, 0, 3], "direction": [0, 0, 1], "up": [0, 1, 0]}],
            "stages": [{"name": "Cargo", "dry_mass": 95000, "propellant": 0, "length": 6, "radius": 1.5,
                        "engines": [], "parts": [{"prop": "stage", "length": 6, "radius": 1.5}]}],
        }
        (fixture / "ship.json").write_text(json.dumps(payload))
        (fixture / "addon.json").write_text(json.dumps({"id": "n1-test-payload", "ships": ["ship.json"]}))
        scenario = json.loads((ROOT / "scenarios/CVEL/CVEL-N1/cveln1_baikonur.json").read_text())
        scenario["ships"].append({"ship": payload["name"], "body": "Earth",
                                  "dock": {"to": 0, "port": 0, "own_port": 0},
                                  "surface": {"base": "baikonur"}})
        scenario_file = root / "n1-test.json"
        scenario_file.write_text(json.dumps(scenario))
        env = dict(os.environ, HOME=directory, XDG_CONFIG_HOME=str(root / "config"),
                   XDG_CACHE_HOME=str(root / "cache"), SDL_VIDEODRIVER="offscreen",
                   SDL_AUDIODRIVER="dummy", LP_NUM_THREADS="4")
        idle = subprocess.run(
            [str(BINARY), "--root", str(root), "--scenario", "cveln1_baikonur", "--selftest", "20"],
            cwd=ROOT, env=env, capture_output=True, text=True, timeout=60,
        )
        assert idle.returncode == 0 and "crashed 0" in idle.stdout, idle.stdout + idle.stderr
        empty = subprocess.run(
            [str(BINARY), "--root", str(root), "--scenario", "cveln1_baikonur", "--selftest", "700",
             "--selftest-warp", "4", "--set", "/autopilot/enabled=1",
             "--set", "/autopilot/sc/target-altitude=200", "--set", "/autopilot/sc/heading=90"],
            cwd=ROOT, env=env, capture_output=True, text=True, timeout=180,
        )
        empty_output = empty.stdout + empty.stderr
        empty_final = re.search(r"final: alt .*?apoapsis ([\d.-]+) km, periapsis ([\d.-]+) km, .*crashed (\d+)", empty_output)
        assert (empty.returncode == 0 and "In orbit" in empty_output and empty_final and empty_final[3] == "0"
                and all(190 <= float(x) <= 210 for x in empty_final.group(1, 2))), empty_output
        orbit = json.loads(json.dumps(scenario))
        for item in orbit["ships"]:
            item.pop("surface")
            item["orbit"] = {"altitude_km": 400, "inclination": 51.6}
        orbit_file = root / "n1-staging.json"
        orbit_file.write_text(json.dumps(orbit))
        staging = subprocess.run(
            [str(BINARY), "--root", str(root), "--scenario-file", str(orbit_file), "--selftest", "1",
             "--set", "/selftest/cveln1-staging=true"],
            cwd=ROOT, env=env, capture_output=True, text=True, timeout=60,
        )
        assert staging.returncode == 0 and "CVELN1 staging selftest ok" in staging.stdout, staging.stdout + staging.stderr
        result = subprocess.run(
            [str(BINARY), "--root", str(root), "--scenario-file", str(scenario_file), "--selftest", "900",
             "--selftest-warp", "4", "--set", "/autopilot/enabled=1",
             "--set", "/autopilot/sc/target-altitude=200", "--set", "/autopilot/sc/heading=90"],
            cwd=ROOT, env=env, capture_output=True, text=True, timeout=300,
        )
        output = result.stdout + result.stderr
        assert result.returncode == 0 and "script error" not in output.lower() and "CRASHED" not in output, output
        reports = re.findall(r"^t=.*?stage=(\d+) fuel=\s*(\d+) m=\s*(\d+).*?docked=(\d+)", output, re.MULTILINE)
        assert reports and {int(r[0]) for r in reports} >= {0, 1, 4}, output
        assert all(r[3] == "1" for r in reports), output
        initial_mass = sum(stage["dry_mass"] + stage["propellant"] for stage in SHIP["stages"]) + 95000
        assert initial_mass - 2000 <= int(reports[0][2]) <= initial_mass, output
        assert "In orbit" in output, output
        final = re.search(r"final: alt .*?apoapsis ([\d.-]+) km, periapsis ([\d.-]+) km, .*crashed (\d+)", output)
        assert final and final[3] == "0" and all(190 <= float(x) <= 210 for x in final.group(1, 2)), output
        print(f"95 t payload: apoapsis {final[1]} km, periapsis {final[2]} km")


if __name__ == "__main__":
    check_meshes()
    run_flight()
    print("CVELN1 test passed")
