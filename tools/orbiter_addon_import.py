#!/usr/bin/env python3
"""Start a Kosmos addon from an Orbiter add-on.

    orbiter_addon_import.py <Orbiter add-on root> --id cvel-foo [--name "Foo"] [--out addons]

The add-on root is the folder holding Config/, Meshes/, Textures/ and, for source-available add-ons, a
`.cpp` that calls SetClassCaps. The tool
  * converts every Meshes/*.msh to OBJ/MTL/PNG under <out>/<id>/models (tools/msh_import.py),
  * reads the vessel's C++ source for what it can find by pattern (mass, propellant, thrusters, docking
    ports, camera offset) and the Config/*.cfg for the mass and size, and
  * writes <out>/<id>/addon.json and a draft ship.json, with every value it could not find marked "TODO".

Orbiter's Z points forward and Kosmos's points backward (the nose is at -Z), so every position and direction
read from the source has Z negated. Review the draft by hand: the tool does not follow thruster groups,
stages, animations, panels or anything computed at run time. See docs/importing-orbiter-addons.md.
"""
import argparse
import json
import os
import re
import subprocess
import sys

G0 = 9.80665
NUM = r"(-?\d+(?:\.\d*)?(?:[eE][-+]?\d+)?)"
VEC = rf"_V\(\s*{NUM}\s*,\s*{NUM}\s*,\s*{NUM}\s*\)"
ARG = rf"(?:{VEC}|[A-Za-z_][\w\.]*(?:->)?\w*)"


def read_text(path):
    with open(path, "r", errors="replace") as f:
        return f.read()


def find_sources(root):
    found = []
    for base, _dirs, files in os.walk(root):
        for name in files:
            if name.lower().endswith(".cpp"):
                found.append(os.path.join(base, name))
    return sorted(found)


def defines(text):
    table = {}
    for m in re.finditer(rf"^\s*#define\s+(\w+)\s+{NUM}", text, re.M):
        table[m.group(1)] = float(m.group(2))
    return table


def value(token, table):
    token = token.strip()
    try:
        return float(token)
    except ValueError:
        return table.get(token)


def flip(v):
    """Orbiter (+Z forward, left-handed) to Kosmos (-Z forward): negate Z."""
    return [round(v[0], 4), round(v[1], 4), round(-v[2] if v[2] else 0.0, 4)]


def scan_source(text):
    table = defines(text)
    out = {"thrusters": [], "ports": [], "propellant": [], "mass": None, "camera": None, "size": None, "meshes": []}
    for m in re.finditer(rf"CreateThruster\s*\(\s*{VEC}[^,]*,\s*{VEC}\s*,\s*([\w\.]+)\s*,\s*[\w\[\]\.]+\s*,\s*([\w\.]+)", text):
        pos = [float(m.group(i)) for i in (1, 2, 3)]
        direction = [float(m.group(i)) for i in (4, 5, 6)]
        thrust, isp = value(m.group(7), table), value(m.group(8), table)
        if thrust is not None:
            out["thrusters"].append({"position": pos, "direction": direction, "thrust": thrust, "isp_ms": isp})
    for m in re.finditer(rf"CreateDock\s*\(\s*{VEC}\s*,\s*{VEC}\s*,\s*{VEC}", text):
        v = [float(m.group(i)) for i in range(1, 10)]
        out["ports"].append({"position": v[0:3], "direction": v[3:6], "up": v[6:9]})
    for m in re.finditer(r"CreatePropellantResource\s*\(\s*([\w\.]+)", text):
        amount = value(m.group(1), table)
        if amount is not None:
            out["propellant"].append(amount)
    m = re.search(r"SetEmptyMass\s*\(\s*([\w\.]+)", text)
    if m:
        out["mass"] = value(m.group(1), table)
    if out["mass"] is None:
        for name in ("MASS", "EMPTY_MASS", "EMPTYMASS", "DRYMASS"):
            if name in table:
                out["mass"] = table[name]
                break
    m = re.search(rf"SetCameraOffset\s*\(\s*{VEC}", text)
    if m:
        out["camera"] = [float(m.group(i)) for i in (1, 2, 3)]
    m = re.search(r"SetSize\s*\(\s*([\w\.]+)", text)
    if m:
        out["size"] = value(m.group(1), table)
    out["meshes"] = re.findall(r'AddMesh\s*\(\s*"([^"]+)"', text)
    return out


def scan_cfg(root):
    info = {}
    for base, _dirs, files in os.walk(os.path.join(root, "Config")):
        for name in files:
            if not name.lower().endswith(".cfg"):
                continue
            text = read_text(os.path.join(base, name))
            entry = {}
            for key in ("MeshName", "Mass", "Size", "ClassName"):
                m = re.search(rf"^\s*{key}\s*=\s*([^\s;]+)", text, re.M | re.I)
                if m:
                    entry[key] = m.group(1)
            info[name] = entry
    return info


def convert_meshes(root, models):
    tool = os.path.join(os.path.dirname(os.path.abspath(__file__)), "msh_import.py")
    if not os.path.isdir(os.path.join(root, "Meshes")):
        print("no Meshes folder; skipping mesh conversion")
        return
    subprocess.run([sys.executable, tool, root, "--out", models], check=True)


def draft(name, scan, cfg):
    engines, rcs = [], 0.0
    for t in scan["thrusters"]:
        if t["thrust"] <= 500:
            rcs = max(rcs, t["thrust"] * 2)
            continue
        engine = {"position": flip(t["position"]), "direction": flip(t["direction"]),
                  "thrust": t["thrust"], "isp": round(t["isp_ms"] / G0, 1) if t["isp_ms"] else "TODO",
                  "plume": "TODO", "type": "rocket", "fuel": "TODO: kerosene, hydrogen, methane or hypergolic"}
        engines.append(engine)
    mass = scan["mass"]
    if mass is None:
        for entry in cfg.values():
            if "Mass" in entry:
                mass = float(entry["Mass"])
                break
    ship = {
        "name": name,
        "class": "TODO: launcher, capsule, tug, probe, lander, station or aircraft",
        "torque_accel": 0.4,
        "max_rate": 0.4,
        "rcs_thrust": rcs or "TODO",
        "drag_area": "TODO",
        "camera_distance": round((scan["size"] or 10) * 1.5, 1),
        "stages": [{
            "name": name,
            "dry_mass": mass if mass is not None else "TODO",
            "propellant": sum(scan["propellant"]) if scan["propellant"] else "TODO",
            "z": 0,
            "length": round((scan["size"] or 5) * 2, 1),
            "radius": round((scan["size"] or 5) / 2, 1),
            "engines": engines,
            "parts": [{"model": f"models/{m}.obj", "offset": [0, 0, 0]} for m in (scan["meshes"] or ["TODO"])],
        }],
    }
    if scan["ports"]:
        ship["ports"] = [{"name": "Docking", "position": flip(p["position"]), "direction": flip(p["direction"]),
                          "up": [round(c, 4) for c in p["up"]]} for p in scan["ports"]]
    if scan["camera"]:
        ship["cockpit"] = {"eye": flip(scan["camera"])}
    return ship


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("root", help="Orbiter add-on root")
    ap.add_argument("--id", required=True, help="addon id, also its folder name (for example cvel-soyuz-tma)")
    ap.add_argument("--name", help="ship name (default: the id)")
    ap.add_argument("--out", default="addons", help="folder that holds addons (default addons)")
    args = ap.parse_args()

    dest = os.path.join(args.out, args.id)
    os.makedirs(dest, exist_ok=True)
    convert_meshes(args.root, os.path.join(dest, "models"))

    scan = {"thrusters": [], "ports": [], "propellant": [], "mass": None, "camera": None, "size": None, "meshes": []}
    for path in find_sources(args.root):
        folder = os.path.dirname(path)
        headers = "".join(read_text(os.path.join(folder, h)) for h in sorted(os.listdir(folder)) if h.lower().endswith(".h"))
        found = scan_source(headers + read_text(path))
        if not found["thrusters"] and not found["ports"] and found["mass"] is None:
            continue
        print(f"read {os.path.relpath(path, args.root)}: {len(found['thrusters'])} thrusters, {len(found['ports'])} ports")
        for key, val in found.items():
            if key in ("thrusters", "ports", "propellant", "meshes"):
                scan[key] += val
            elif scan[key] is None:
                scan[key] = val
    cfg = scan_cfg(args.root)
    name = args.name or args.id
    with open(os.path.join(dest, "addon.json"), "w") as f:
        json.dump({"id": args.id, "name": name, "version": "0.1", "author": "TODO", "description": "TODO", "ships": ["ship.json"]}, f, indent=2)
        f.write("\n")
    with open(os.path.join(dest, "ship.json"), "w") as f:
        json.dump(draft(name, scan, cfg), f, indent=1)
        f.write("\n")
    print(f"wrote {dest}/addon.json and ship.json; search them for TODO")


if __name__ == "__main__":
    main()
