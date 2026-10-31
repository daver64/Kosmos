#!/usr/bin/env python3
"""Adds the Main Belt Asteroids add-on for Orbiter (import/orbiter/addons/MainBelt) to Kosmos.

    tools/mainbelt_import.py          (run from anywhere; paths are relative to the repository)

Shippable part, written into data/sol.json: the asteroids Kosmos lacks (Antiope and its moon S2000 90 1, Kleopatra, Ida and
Dactyl, Gaspra, Toutatis) with mass, mean radius, spin and orbital elements from the add-on's .cfg files, and the Orbiter
elements for Ceres, Pallas, Hygiea and Vesta, which replace the rougher ones that were there. Elements are carried from the
add-on's epoch back to J2000, the epoch sol.json uses.

Not shippable, so git-ignored under data/textures-hires/: the add-on's textures (<name>.dds, found by body name like any
hi-res map) and its shaped meshes (bodies/<name>.obj, scaled to unit mean radius, drawn in place of the sphere). A body that
names a mesh the install lacks falls back to the sphere, so the game works without them. Bodies whose texture is only
64 x 64 get a flat colour averaged from it instead. Vesta keeps its NASA mesh.
Re-running is safe: bodies already in sol.json are only re-timed, never duplicated.
"""
import json
import math
import re
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import msh_import  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
ADDON = ROOT / "import/orbiter/addons/MainBelt"
SOL = ROOT / "data/sol.json"
HIRES = ROOT / "data/textures-hires"
G = 6.6743e-11

# Orbiter name -> (Kosmos name, parent, texture file or None, mesh file or None)
BODIES = [
    ("1 Ceres", "Ceres", "Sun", None, None),
    ("2 Pallas", "Pallas", "Sun", None, None),
    ("4 Vesta", "Vesta", "Sun", None, None),
    ("10 Hygiea", "Hygiea", "Sun", None, None),
    ("90 Antiope", "Antiope", "Sun", None, None),
    ("S2000 90 1", "S2000 90 1", "Antiope", None, None),
    ("216 Kleopatra", "Kleopatra", "Sun", "Kleopatra.dds", "216 Kleopatra.msh"),
    ("243 Ida", "Ida", "Sun", "Ida.dds", "243 Ida.msh"),
    ("Dactyl", "Dactyl", "Ida", None, None),
    ("951 Gaspra", "Gaspra", "Sun", "Gaspra.dds", "951 Gaspra.msh"),
    ("4179 Toutatis", "Toutatis", "Sun", "Toutatis.dds", "4179 Toutatis.msh"),
]
INSERT_AFTER = "Hygiea"


def cfg(path):
    out = {}
    for line in path.read_text(errors="replace").splitlines():
        m = re.match(r"(\w+)\s*=\s*(.+)$", line.split(";")[0].strip())
        if m:
            out[m.group(1)] = m.group(2).strip()
    return out


def orbit_of(c, parent_gm, in_au):
    d = 180 / math.pi
    wrap = lambda x: x % (2 * math.pi)
    a = float(c["SemiMajorAxis"])
    lan, lp, ml = wrap(float(c["LongAscNode"])), wrap(float(c["LongPerihelion"])), wrap(float(c["MeanLongitude"]))
    n = math.sqrt(parent_gm / a ** 3)
    m0 = wrap(wrap(ml - lp) - n * (float(c["Epoch"]) - 2000.0) * 365.25 * 86400)
    o = {"a_au": round(a / 1.495978707e11, 5)} if in_au else {"a_km": round(a / 1000, 3)}
    o.update({"e": round(float(c["Eccentricity"]), 5), "i": round(float(c["Inclination"]) * d, 3),
              "raan": round(lan * d, 2), "argp": round(wrap(lp - lan) * d, 2), "m0": round(m0 * d, 2)})
    return o


def average_colour(path):
    from PIL import Image
    image = Image.open(path).convert("RGB").resize((1, 1), Image.BOX)
    return [max(60, v) for v in image.getpixel((0, 0))]


def write_mesh(msh, out):
    """One group, no material: the body's own texture is applied. Unit mean radius about the centroid, Orbiter's Z flipped."""
    mesh = msh_import.parse_msh(str(msh))
    verts = [v for g in mesh.groups for v in g.vertices]
    cx, cy, cz = (sum(v[k] for v in verts) / len(verts) for k in range(3))
    mean = sum(math.dist(v[:3], (cx, cy, cz)) for v in verts) / len(verts)
    lines = ["# from an Orbiter .msh by tools/mainbelt_import.py: unit mean radius, +Y spin axis"]
    base = 1
    for g in mesh.groups:
        for v in g.vertices:
            lines.append("v %.5f %.5f %.5f" % ((v[0] - cx) / mean, (v[1] - cy) / mean, -(v[2] - cz) / mean))
        for v in g.vertices:
            lines.append("vn %.4f %.4f %.4f" % (v[3], v[4], -v[5]))
        for v in g.vertices:
            lines.append("vt %.5f %.5f" % (v[6], 1.0 - v[7]))
        for a, b, c in g.triangles:
            a, b, c = a + base, b + base, c + base
            lines.append(f"f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c}")
        base += len(g.vertices)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines) + "\n")


def body_line(b):
    o = b["orbit"]
    orbit = ", ".join(f'"{k}": {json.dumps(v)}' for k, v in o.items())
    head = ", ".join(f'"{k}": {json.dumps(v)}' for k, v in b.items() if k != "orbit")
    return f"    {{{head},\n     \"orbit\": {{{orbit}}}}},\n"


def main():
    text = SOL.read_text()
    bodies = json.loads(text)["bodies"]
    have = {b["name"]: b for b in bodies}
    sun_gm = have["Sun"]["gm"]
    masses = {}
    new = []
    for orbiter_name, name, parent, texture, mesh in BODIES:
        c = cfg(ADDON / "Config" / f"{orbiter_name}.cfg")
        gm = float(c["Mass"]) * G
        masses[name] = gm
        parent_gm = sun_gm if parent == "Sun" else masses[parent]
        orbit = orbit_of(c, parent_gm, parent == "Sun")
        if name in have:
            have[name]["orbit"] = orbit
            continue
        textured = texture and (ADDON / "Textures" / texture).exists()
        flat = ADDON / "Textures" / f"{orbiter_name}.tex"
        colour = average_colour(flat) if flat.exists() else (average_colour(ADDON / "Textures" / texture) if textured else [140, 135, 130])
        entry = {"name": name, "kind": "moon" if parent != "Sun" else "asteroid", "parent": parent, "gm": float(f"{gm:.4e}"),
                 "radius": float(c["Size"]), "spin": float(f"{2 * math.pi / float(c['SidRotPeriod']):.4e}")}
        if mesh and (ADDON / "Meshes" / mesh).exists():
            entry["mesh"] = f"textures-hires/bodies/{name.lower()}.obj"
        entry["colour"] = colour
        entry["orbit"] = orbit
        new.append(entry)

    # Existing bodies get their new orbit by rewriting just that object on their lines.
    for name in [b[1] for b in BODIES if b[1] in have]:
        o = have[name]["orbit"]
        orbit = "{" + ", ".join(f'"{k}": {json.dumps(v)}' for k, v in o.items()) + "}"
        text, n = re.subn(r'("name": "%s",.*?"orbit": )\{[^}]*\}' % re.escape(name), lambda m: m.group(1) + orbit, text, count=1, flags=re.S)
        if n != 1:
            raise SystemExit(f"could not update the orbit of {name}")
    if new:
        lines = text.splitlines(keepends=True)
        i = next(k for k, l in enumerate(lines) if l.startswith(f'    {{"name": "{INSERT_AFTER}"'))
        while not lines[i].rstrip().endswith("}},"):
            i += 1
        lines[i + 1:i + 1] = [body_line(b) for b in new]
        text = "".join(lines)
    json.loads(text)
    SOL.write_text(text)
    print("sol.json: added", ", ".join(b["name"] for b in new) or "nothing", "; orbits refreshed for the rest")

    for orbiter_name, name, parent, texture, mesh in BODIES:
        if texture and (ADDON / "Textures" / texture).exists():
            HIRES.mkdir(exist_ok=True)
            shutil.copy(ADDON / "Textures" / texture, HIRES / f"{name.lower()}.dds")
            print("texture", name.lower() + ".dds")
        if mesh and (ADDON / "Meshes" / mesh).exists():
            write_mesh(ADDON / "Meshes" / mesh, HIRES / "bodies" / f"{name.lower()}.obj")
            print("mesh", f"bodies/{name.lower()}.obj")


if __name__ == "__main__":
    sys.exit(main())
