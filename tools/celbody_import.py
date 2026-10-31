#!/usr/bin/env python3
"""Adds the minor bodies Orbiter defines (import/orbiter/Celbody) that data/sol.json lacks.

Physical data (mass, size, albedo, rotation) is read from the Orbiter .cfg files. Orbital elements come from Nereid's own cfg;
the SPICE-driven moons have none, so ORBITS below holds JPL mean elements (J2000, degrees, relative to the ecliptic).
Textures: Orbiter's hydra.dds and nix.dds are copied to data/textures/ (named after the body, so they are found by name).
Run from the repository root; bodies already in sol.json are left alone.
"""
import json, math, re, shutil, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CEL = ROOT / "import/orbiter/Celbody"
SOL = ROOT / "data/sol.json"
G = 6.6743e-11

# name -> (parent, insert after, orbit)
ORBITS = {
    "Himalia": ("Jupiter", "Amalthea", {"a_km": 11461000, "e": 0.1623, "i": 27.5, "raan": 57.2, "argp": 331.0, "m0": 60.0}),
    "Hydra": ("Pluto", "Charon", {"a_km": 64738, "e": 0.0059, "i": 0.2, "raan": 223.0, "argp": 340.0, "m0": 120.0}),
    "Nix": ("Pluto", "Hydra", {"a_km": 48694, "e": 0.002, "i": 0.1, "raan": 223.0, "argp": 200.0, "m0": 30.0}),
}
NEREID = ("Neptune", "Proteus")


def cfg(path):
    out = {}
    for line in path.read_text(errors="replace").splitlines():
        line = line.split(";")[0].strip()
        m = re.match(r"(\w+)\s*=\s*(.+)$", line)
        if m:
            out[m.group(1)] = m.group(2).strip()
    return out


def body_from(name, c, parent, orbit, synchronous=False):
    gm = float(c["Mass"]) * G
    spin = 2 * math.pi / float(c["SidRotPeriod"])
    colour = [max(60, round(float(v) * 255)) for v in c["AlbedoRGB"].split()]  # floor: true albedos are nearly black
    b = {"name": name, "kind": "moon", "parent": parent, "gm": float(f"{gm:.4e}"), "radius": float(c["Size"]),
         "spin": float(f"{spin:.4e}"), "colour": colour, "orbit": orbit}
    return b


def nereid_orbit(c, parent_gm):
    d = 180 / math.pi
    wrap = lambda x: x % (2 * math.pi)
    a = float(c["SemiMajorAxis"])
    lan, lp, ml = wrap(float(c["LongAscNode"])), wrap(float(c["LongPerihelion"])), wrap(float(c["MeanLongitude"]))
    m_epoch = wrap(ml - lp)
    n = math.sqrt(parent_gm / a ** 3)
    m0 = wrap(m_epoch - n * (float(c["Epoch"]) - 2000.0) * 365.25 * 86400)
    return {"a_km": round(a / 1000), "e": round(float(c["Eccentricity"]), 4), "i": round(float(c["Inclination"]) * d, 2),
            "raan": round(lan * d, 1), "argp": round(wrap(lp - lan) * d, 1), "m0": round(m0 * d, 1)}


def fmt(b):
    o = b["orbit"]
    orbit = ", ".join(f'"{k}": {v}' for k, v in o.items())
    head = ", ".join(f'"{k}": {json.dumps(v)}' for k, v in b.items() if k != "orbit")
    return f"    {{{head},\n     \"orbit\": {{{orbit}}}}},\n"


def main():
    text = SOL.read_text()
    have = {b["name"] for b in json.loads(text)["bodies"]}
    neptune_gm = next(b["gm"] for b in json.loads(text)["bodies"] if b["name"] == "Neptune")
    new = []
    if "Nereid" not in have:
        parent, after = NEREID
        c = cfg(CEL / "Nereid/Config/Nereid.cfg")
        new.append((after, body_from("Nereid", c, parent, nereid_orbit(c, neptune_gm))))
    for name, (parent, after, orbit) in ORBITS.items():
        if name not in have:
            new.append((after, body_from(name, cfg(CEL / "SPICE/Config" / f"{name}.cfg"), parent, orbit)))
    for after, b in new:
        lines = text.splitlines(keepends=True)
        i = next(k for k, l in enumerate(lines) if l.startswith(f'    {{"name": "{after}"'))
        j = i
        while not lines[j].rstrip().endswith("}},"):
            j += 1
        lines.insert(j + 1, fmt(b))
        text = "".join(lines)
        print("added", b["name"])
    SOL.write_text(text)
    json.loads(text)
    tex = ROOT / "import/orbiter/Textures"
    for n in ("hydra", "nix"):
        dst = ROOT / "data/textures" / f"{n}.dds"
        if (tex / f"{n}.dds").exists() and not dst.exists():
            shutil.copy(tex / f"{n}.dds", dst)
            print("texture", dst.name)


if __name__ == "__main__":
    sys.exit(main())
