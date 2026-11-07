#!/usr/bin/env python3
"""Convert Orbiter's Deltaglider and Shuttle-A virtual cockpits for Kosmos."""

import argparse
import copy
from pathlib import Path

import msh_import

ROOT = Path(__file__).resolve().parents[1]


def screen(group, name, vertices, triangles):
    display = copy.deepcopy(group)
    display.label = name
    display.vertices = vertices
    display.triangles = triangles
    return display


def deltaglider(mesh):
    horizon = next(g for g in mesh.groups if g.label == "HORIZON")
    hsi = next(g for g in mesh.groups if g.label == "HSI")
    assert len(horizon.vertices) >= 8 and len(hsi.vertices) >= 4
    assert horizon.triangles[:6] == [(0, 5, 4), (5, 0, 1), (1, 6, 5),
                                     (6, 1, 2), (2, 7, 6), (7, 2, 3)]
    assert hsi.triangles[:2] == [(0, 1, 2), (3, 2, 1)]
    v = horizon.vertices
    vertices = []
    for x, y, z, uv, original in (
        (v[0][0], v[1][1], v[1][2], (0, 0), v[0]),
        (v[3][0], v[2][1], v[2][2], (1, 0), v[3]),
        (v[3][0], v[6][1], v[6][2], (1, 1), v[7]),
        (v[0][0], v[5][1], v[5][2], (0, 1), v[4]),
    ):
        vertices.append((x, y + original[4] * 0.001, z + original[5] * 0.001, *original[3:6], *uv))
    adi = screen(horizon, "ADI_DISPLAY", vertices, [(0, 1, 2), (0, 2, 3)])
    horizon.triangles = []
    vertices = [(v[0], v[1] + v[4] * 0.001, v[2] + v[5] * 0.001, *v[3:6], *uv)
                for v, uv in zip(hsi.vertices[:4], ((0, 0), (1, 0), (0, 1), (1, 1)))]
    hsi_display = screen(hsi, "HSI_DISPLAY", vertices, [(0, 1, 2), (3, 2, 1)])
    hsi.triangles = []
    mesh.groups.extend((adi, hsi_display))


def shuttlea(mesh):
    for index, name in {
        22: "lever_Ag22x", 23: "LMFD_DISPLAY", 24: "RMFD_DISPLAY",
        25: "ShuttleA_Thrust", 26: "ShuttleA_PodAngle",
        27: "lever_Ag27x", 28: "lever_Ag28x", 30: "lever_Ag30x",
        31: "lever_Ag31x", 32: "lever_Ag32x", 49: "lever_Ag49x", 52: "HUDDISP",
    }.items():
        assert not mesh.groups[index].label, (index, mesh.groups[index].label)
        mesh.groups[index].label = name
        if index in (23, 24):
            mesh.groups[index].vertices = [(*v[:7], 1 - v[7]) for v in mesh.groups[index].vertices]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("orbiter", type=Path, help="Orbiter source tree")
    args = parser.parse_args()
    sources = (
        ("DeltaGlider", "deltaglider_vc", deltaglider),
        ("ShuttleA", "ShuttleA_vc", shuttlea),
    )
    for vessel, name, configure in sources:
        source = args.orbiter / "Src" / "Vessel" / vessel
        mesh = msh_import.parse_msh(source / "Meshes" / f"{name}.msh")
        configure(mesh)
        target = ROOT / "addons" / ("cvel-deltaglider" if vessel == "DeltaGlider" else "cvel-shuttle-a") / "orbiter-models" / "vc"
        textures = [source / "Textures", args.orbiter / "Textures"]
        if vessel == "ShuttleA":
            textures.insert(1, source / "ShuttleA_PL" / "Textures")
        stats = msh_import.write_obj(mesh, str(target / name.lower()), [str(p) for p in textures],
                                     tex_out=str(target / "textures"), objects=True)
        print(f"{vessel} VC: {stats[0]} vertices, {stats[1]} triangles, {stats[2]} textures")


if __name__ == "__main__":
    main()
