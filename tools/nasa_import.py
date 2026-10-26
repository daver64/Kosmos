#!/usr/bin/env python3
"""Turn the NASA 3D Resources models in data/nasa into Kosmos stock parts and a body mesh.

    python3 tools/nasa_import.py [--only ID ...] [--tris N] [--keep]

For every model in MODELS this unpacks the Draco/WebP glTF (tools/glb_unpack.py), then has Blender (tools/nasa_blender.py) put it at real
size, decimate it, orient it and write a plain GLB:

  flight hardware  data/parts/nasa_<id>/   part.json + GLB, in the part-kit frame (bottom at the origin, nose along -Z)
  scenery          data/parts/nasa_<id>/   GLB only, upright on the ground, for base "model" objects
  small worlds     data/bodies/<name>.glb  a unit-radius mesh that sol.json bodies name in "mesh"

Needs Blender and `pip install DracoPy pillow numpy`. The sizes and masses below are approximate figures for the real vehicles;
the source models come in assorted units, so each is fitted to a real dimension ("up" is its height, "max" its longest side).
"""
import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "data" / "nasa"
sys.path.insert(0, str(Path(__file__).resolve().parent))
import glb_unpack  # noqa: E402

# kind: kit (flight part with a part.json), prop (scenery) or body.  `lay` False keeps a model whose long axis is to be the nose axis (`align` finds that axis, `flip` swaps the ends).
MODELS = [
    dict(id="acrimsat", src="Active Cavity Irradiance Monitor Satellite (AcrimSAT) (B).glb", kind="kit", fit=("max", 2.0), dry_mass=119,
         about="ACRIMSAT: a small solar-monitoring satellite with four panels."),
    dict(id="agena", src="Agena Target Vehicle.glb", kind="kit", fit=("max", 8.0), lay=False, align=True, flip=True, dry_mass=800, propellant=6100,
         engines=[dict(position=[0, 0, -0.2], direction=[0, 0, -1], thrust=71000, isp=285, plume=2.5, type="rocket", fuel="hypergolic")],
         about="Gemini Agena target vehicle with its main engine at the bottom."),
    dict(id="lunar_module", src="Apollo Lunar Module.glb", kind="kit", fit=("up", 7.0), dry_mass=4700, propellant=10500,
         engines=[dict(position=[0, 0, -0.9], direction=[0, 0, -1], thrust=45040, isp=311, plume=2.0, type="rocket", fuel="hypergolic")],
         about="Apollo Lunar Module, descent and ascent stages together."),
    dict(id="mercury_capsule", src="Atlas 6 (Friendship 7).glb", kind="kit", fit=("up", 2.9), dry_mass=1100,
         about="Mercury capsule (Friendship 7)."),
    dict(id="pioneer10", src="Pioneer 10.glb", kind="kit", fit=("up", 6.6), dry_mass=258,
         about="Pioneer 10 with its magnetometer boom; the dish faces the nose."),
    dict(id="viking_lander", src="Viking Lander.glb", kind="kit", fit=("up", 2.3), dry_mass=600,
         about="Viking lander."),
    dict(id="astronaut", src="Astronaut.glb", kind="prop", fit=("up", 1.85)),
    dict(id="gantry", src="Gantry.glb", kind="prop", fit=("up", 45.0)),
    dict(id="habitat_unit_1", src="Habitat Demonstration Unit (part 1).glb", kind="prop", fit=("up", 4.5)),
    dict(id="habitat_unit_2", src="Habitat Demonstration Unit (part 2).glb", kind="prop", fit=("up", 4.7)),
    dict(id="headquarters", src="Headquarters Building.glb", kind="prop", fit=("up", 28.0)),
    dict(id="mission_control", src="JSC Mission Control Room.glb", kind="prop", fit=("max", 40.0)),
    dict(id="radome", src="Radome.glb", kind="prop", fit=("up", 16.0)),
    dict(id="exploration_vehicle", src="Space Exploration Vehicle.glb", kind="prop", fit=("up", 3.97)),
    dict(id="vab", src="Vehicle Assembly Building (VAB).glb", kind="prop", fit=("max", 218.0)),
    dict(id="vesta", src="Asteroid 4 Vesta (A).stl", kind="body", out="data/bodies/vesta.glb", tris=20000),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--only", nargs="*", help="convert just these ids")
    parser.add_argument("--tris", type=int, default=30000, help="triangle budget per part (default 30000)")
    parser.add_argument("--blender", default="blender")
    parser.add_argument("--keep", action="store_true", help="keep the temporary unpacked files")
    args = parser.parse_args()

    chosen = [m for m in MODELS if not args.only or m["id"] in args.only]
    work = Path(tempfile.mkdtemp(prefix="nasa_import_"))
    jobs = []
    for m in chosen:
        src = SOURCE / m["src"]
        if not src.exists():
            print(f"skip {m['id']}: {src} is missing")
            continue
        if m["kind"] == "body":
            dst = ROOT / m["out"]
            plain = src
        else:
            dst = ROOT / "data" / "parts" / f"nasa_{m['id']}" / f"nasa_{m['id']}.glb"
            plain = work / (m["id"] + ".glb")
            glb_unpack.unpack(str(src), str(plain))
        dst.parent.mkdir(parents=True, exist_ok=True)
        jobs.append({"id": m["id"], "src": str(plain), "dst": str(dst), "mode": "body" if m["kind"] == "body" else m["kind"],
                     "fit": list(m.get("fit", ("max", 1.0))), "tris": m.get("tris", args.tris), "lay": m.get("lay", True), "align": m.get("align", False), "flip": m.get("flip", False)})
    (work / "jobs.json").write_text(json.dumps(jobs))
    subprocess.run([args.blender, "-b", "-P", str(ROOT / "tools" / "nasa_blender.py"), "--", str(work / "jobs.json"), str(work / "report.json")],
                   check=True, stdout=subprocess.DEVNULL)
    report = json.loads((work / "report.json").read_text())

    for m in chosen:
        info = report.get(m["id"])
        if not info or "error" in info:
            print(f"{m['id']}: FAILED {info}")
            continue
        print(f"{m['id']}: {info['tris']} triangles, {info['size']}")
        if m["kind"] != "kit":
            continue
        # The sizes are in Blender axes, where the nose axis is Y (it exports as -Z) and X and Z are the width.
        x, y, z = info["size"]
        part = {"name": m["id"].replace("_", " ").title(), "description": m["about"], "length": round(y, 2), "radius": round(max(x, z) / 2, 2),
                "dry_mass": m["dry_mass"]}
        if m.get("propellant"):
            part["propellant"] = m["propellant"]
        if m.get("engines"):
            part["engines"] = m["engines"]
        part["model"] = f"nasa_{m['id']}.glb"
        (ROOT / "data" / "parts" / f"nasa_{m['id']}" / "part.json").write_text(json.dumps(part, indent=1) + "\n")
    if not args.keep:
        import shutil
        shutil.rmtree(work, ignore_errors=True)
    else:
        print("kept", work)


if __name__ == "__main__":
    main()
