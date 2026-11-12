#!/usr/bin/env python3
"""Generate CVELN1's original meshes and ship specification, in the ship frame."""

import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ADDON = ROOT / "addons" / "cvel-n1"
MODELS = ADDON / "models"
TAU = 2 * math.pi
SEGMENTS = 96
TEXTURE_SIZE = (2172, 724)


def atlas_uv(x, y):
    return x / TEXTURE_SIZE[0], 1 - y / TEXTURE_SIZE[1]


def hull_uv(rect, aft, forward):
    x0, y0, x1, y1 = rect
    return lambda u, z: atlas_uv(x1 - u * (x1 - x0), y0 + (z - forward) / (aft - forward) * (y1 - y0))


def radial(radius, angle, z):
    return (radius * math.cos(angle), radius * math.sin(angle), z)


def ring(count, radius, phase=0):
    return [(radius * math.cos(phase + TAU * i / count),
             radius * math.sin(phase + TAU * i / count)) for i in range(count)]


class Mesh:
    def __init__(self):
        self.vertices = []
        self.normals = []
        self.texcoords = []
        self.faces = []
        self.name = None
        self.material = None

    def surface(self, name, material):
        self.name, self.material = name, material

    def face(self, points, uvs=None):
        a, b, c = points[:3]
        u = [b[i] - a[i] for i in range(3)]
        v = [c[i] - a[i] for i in range(3)]
        n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        length = math.sqrt(sum(x * x for x in n))
        if length < 1e-9:
            return
        self.normals.append(tuple(x / length for x in n))
        first = len(self.vertices) + 1
        self.vertices.extend(points)
        first_uv = len(self.texcoords) + 1
        if uvs is not None:
            self.texcoords.extend(uvs)
        self.faces.append((self.name, self.material, list(range(first, first + len(points))), len(self.normals),
                           list(range(first_uv, first_uv + len(points))) if uvs is not None else None))

    def shell(self, profile, start=0, stop=SEGMENTS, inside=False, uv=None):
        for (z0, r0), (z1, r1) in zip(profile, profile[1:]):
            for i in range(start, stop):
                a, b = TAU * i / SEGMENTS, TAU * (i + 1) / SEGMENTS
                p = [radial(r0, a, z0), radial(r0, b, z0), radial(r1, b, z1), radial(r1, a, z1)]
                u = ((i - 24) % SEGMENTS) / SEGMENTS
                coords = [uv(u, z0), uv(u + 1 / SEGMENTS, z0),
                          uv(u + 1 / SEGMENTS, z1), uv(u, z1)] if uv else None
                self.face(p if inside else p[::-1], coords if inside or coords is None else coords[::-1])

    def cap(self, z, radius, forward, texture=None):
        for i in range(SEGMENTS):
            a, b = TAU * i / SEGMENTS, TAU * (i + 1) / SEGMENTS
            points = [(0, 0, z), radial(radius, a, z), radial(radius, b, z)]
            coords = [atlas_uv(texture[0] + texture[2] * p[0] / radius,
                               texture[1] - texture[2] * p[1] / radius) for p in points] if texture else None
            self.face(points[::-1] if forward else points, coords[::-1] if forward and coords else coords)

    def tube(self, a, b, radius, sides=8):
        direction = tuple(b[i] - a[i] for i in range(3))
        length = math.sqrt(sum(x * x for x in direction))
        d = tuple(x / length for x in direction)
        reference = (0, 0, 1) if abs(d[2]) < 0.9 else (1, 0, 0)
        u = (d[1] * reference[2] - d[2] * reference[1],
             d[2] * reference[0] - d[0] * reference[2],
             d[0] * reference[1] - d[1] * reference[0])
        un = math.sqrt(sum(x * x for x in u))
        u = tuple(x / un for x in u)
        v = (d[1] * u[2] - d[2] * u[1], d[2] * u[0] - d[0] * u[2], d[0] * u[1] - d[1] * u[0])
        def point(end, angle):
            return tuple(end[j] + radius * (u[j] * math.cos(angle) + v[j] * math.sin(angle)) for j in range(3))
        for i in range(sides):
            x, y = TAU * i / sides, TAU * (i + 1) / sides
            self.face([point(a, x), point(a, y), point(b, y), point(b, x)])

    def nozzle(self, x, y, top, bottom, throat, rim, name):
        self.surface(name, "engine")
        for z0, r0, z1, r1 in ((top, throat, top + (bottom - top) * 0.45, rim * 0.55),
                              (top + (bottom - top) * 0.45, rim * 0.55, bottom, rim)):
            for i in range(24):
                a, b = TAU * i / 24, TAU * (i + 1) / 24
                self.face([(x + r0 * math.cos(b), y + r0 * math.sin(b), z0),
                           (x + r1 * math.cos(b), y + r1 * math.sin(b), z1),
                           (x + r1 * math.cos(a), y + r1 * math.sin(a), z1),
                           (x + r0 * math.cos(a), y + r0 * math.sin(a), z0)])
        self.surface(name + "_interior", "engine_dark")
        for i in range(24):
            a, b = TAU * i / 24, TAU * (i + 1) / 24
            self.face([(x, y, top), (x + rim * math.cos(a), y + rim * math.sin(a), bottom),
                       (x + rim * math.cos(b), y + rim * math.sin(b), bottom)])

    def write(self, name):
        path = MODELS / (name + ".obj")
        with path.open("w") as f:
            f.write("mtllib n1.mtl\n")
            for p in self.vertices:
                f.write("v %.5f %.5f %.5f\n" % p)
            for n in self.normals:
                f.write("vn %.6f %.6f %.6f\n" % n)
            for uv in self.texcoords:
                f.write("vt %.6f %.6f\n" % uv)
            active = (None, None)
            for obj, material, vertices, normal, uvs in self.faces:
                if (obj, material) != active:
                    f.write("o %s\nusemtl %s\n" % (obj, material))
                    active = (obj, material)
                if uvs is None:
                    f.write("f " + " ".join("%d//%d" % (v, normal) for v in vertices) + "\n")
                else:
                    f.write("f " + " ".join("%d/%d/%d" % (v, uv, normal) for v, uv in zip(vertices, uvs)) + "\n")


def truss(mesh, name, z0, z1, r0, r1):
    mesh.surface(name, "truss")
    for z, r in ((z0, r0), (z1, r1)):
        for i in range(32):
            mesh.tube(radial(r, TAU * i / 32, z), radial(r, TAU * (i + 1) / 32, z), 0.10)
    for i in range(32):
        a, b = TAU * i / 32, TAU * (i + 1) / 32
        mesh.tube(radial(r0, a, z0), radial(r1, b, z1), 0.09)
        mesh.tube(radial(r0, b, z0), radial(r1, a, z1), 0.09)


def stringers(mesh, name, z0, z1, r0, r1, count):
    mesh.surface(name, "detail")
    for i in range(count):
        angle = TAU * i / count
        mesh.tube(radial(r0 + 0.08, angle, z0), radial(r1 + 0.08, angle, z1), 0.10, 6)


def strakes(mesh, z0, z1, r0, r1):
    mesh.surface("block_a_strakes", "detail")
    for i in range(16):
        a = TAU * i / 16
        left, right = a - 0.05, a + 0.05
        mesh.face([radial(r0 + 0.18, right, z0), radial(r0 + 0.18, left, z0),
                   radial(r1 + 0.18, left, z1), radial(r1 + 0.18, right, z1)])
        for angle in (left, right):
            mesh.face([radial(r0, angle, z0), radial(r0 + 0.18, angle, z0),
                       radial(r1 + 0.18, angle, z1), radial(r1, angle, z1)])


def make_meshes():
    MODELS.mkdir(parents=True, exist_ok=True)
    (MODELS / "n1.mtl").write_text(
        "newmtl hull\nKd 0.72 0.73 0.72\nKs 0.12 0.12 0.12\nNs 24\n"
        "newmtl detail\nKd 0.47 0.48 0.47\nKs 0.1 0.1 0.1\n"
        "newmtl truss\nKd 0.32 0.33 0.32\nKs 0.1 0.1 0.1\n"
        "newmtl engine\nKd 0.31 0.30 0.28\nKs 0.2 0.2 0.2\n"
        "newmtl engine_dark\nKd 0.10 0.10 0.10\n"
        "newmtl interior\nKd 0.38 0.38 0.36\n"
        "newmtl tower\nKd 0.76 0.76 0.74\n"
        "newmtl textured\nKd 1 1 1\nKs 0.12 0.12 0.12\nNs 24\nmap_Kd n1tex.png\n")

    stage1 = Mesh()
    stage1.surface("block_a_hull", "textured")
    stage1.shell([(0, 8.44), (-2.8, 7.9), (-23.3, 5.85)],
                 uv=hull_uv((8, 5, 1710, 718), 0, -23.3))
    stage1.surface("block_a_forward_cap", "textured")
    stage1.cap(-23.3, 5.85, True, (1887, 170, 162))
    truss(stage1, "block_a_interstage", -23.3, -30.1, 5.85, 5.15)
    stringers(stage1, "block_a_ribs", -3.2, -17.5, 7.86, 6.42, 24)
    strakes(stage1, -4.0, -16.0, 7.78, 6.58)
    stage1.surface("block_a_engine_deck", "textured")
    stage1.cap(-0.15, 8.1, False, (1887, 522, 162))
    stage1.surface("block_a_deck_rim", "detail")
    stage1.shell([(0, 8.44), (-0.15, 8.1)], inside=True)
    a_engines = ring(24, 6.35) + ring(6, 2.8, math.pi / 6)
    for i, (x, y) in enumerate(a_engines):
        stage1.nozzle(x, y, -2.25, -0.02, 0.23, 0.61, "a_engine_%02d" % (i + 1))
    stage1.write("block_a")

    stage2 = Mesh()
    stage2.surface("block_b_hull", "textured")
    stage2.shell([(-30.1, 5.15), (-32.0, 5.01), (-46.6, 3.94)],
                 uv=hull_uv((12, 96, 450, 245), -30.1, -46.6))
    stage2.surface("block_b_aft_cap", "textured")
    stage2.cap(-30.1, 5.15, False, (1887, 522, 162))
    stage2.surface("block_b_forward_cap", "textured")
    stage2.cap(-46.6, 3.94, True, (1887, 170, 162))
    truss(stage2, "block_b_interstage", -46.6, -50.6, 3.94, 3.65)
    stringers(stage2, "block_b_pipes", -32.0, -44.0, 5.01, 4.13, 8)
    b_engines = ring(8, 2.75, math.pi / 8)
    for i, (x, y) in enumerate(b_engines):
        stage2.nozzle(x, y, -31.4, -28.2, 0.26, 0.76, "b_engine_%02d" % (i + 1))
    stage2.write("block_b")

    stage3 = Mesh()
    stage3.surface("block_v_hull", "textured")
    stage3.shell([(-50.6, 3.65), (-52.0, 3.57), (-61.7, 2.75)],
                 uv=hull_uv((12, 96, 450, 245), -50.6, -61.7))
    stage3.surface("block_v_aft_cap", "textured")
    stage3.cap(-50.6, 3.65, False, (1887, 522, 162))
    stage3.surface("block_v_forward_cap", "textured")
    stage3.cap(-61.7, 2.75, True, (1887, 170, 162))
    stringers(stage3, "block_v_pipes", -52.0, -59.0, 3.57, 2.98, 4)
    v_engines = [(x, y) for x in (-1.35, 1.35) for y in (-1.35, 1.35)]
    for i, (x, y) in enumerate(v_engines):
        stage3.nozzle(x, y, -51.6, -48.6, 0.24, 0.72, "v_engine_%02d" % (i + 1))
    stage3.write("block_v")

    profile = [(-61.7, 2.75), (-62.2, 3.05), (-82.0, 3.05), (-92.5, 1.35), (-94.0, 0.55)]
    for half, start, stop in (("left", 24, 72), ("right", 72, 120)):
        fairing = Mesh()
        fairing.surface("shroud_" + half, "textured")
        fairing.shell(profile, start, stop, uv=hull_uv((12, 96, 450, 245), -61.7, -94.0))
        fairing.surface("shroud_interior_" + half, "interior")
        fairing.shell([(z, max(0.01, r - 0.10)) for z, r in profile], start, stop, inside=True)
        fairing.surface("shroud_edges_" + half, "detail")
        for angle in (TAU * start / SEGMENTS, TAU * stop / SEGMENTS):
            for (z0, r0), (z1, r1) in zip(profile, profile[1:]):
                fairing.face([radial(r0, angle, z0), radial(r1, angle, z1),
                              radial(r1 - 0.10, angle, z1), radial(r0 - 0.10, angle, z0)])
        if half == "right":
            fairing.surface("escape_tower", "tower")
            fairing.shell([(-94.0, 0.50), (-96.5, 0.24), (-103.8, 0.24), (-104.9, 0.02)])
            for i in range(4):
                a = TAU * i / 4
                fairing.tube(radial(0.48, a, -94.0), radial(0.24, a, -96.5), 0.05)
        fairing.write("shroud_" + half)
    return a_engines, b_engines, v_engines


def engine(x, y, z, thrust, isp, isp_sl, plume, width, density=0.7, glow=0.6):
    return {"position": [round(x, 5), round(y, 5), z], "direction": [0, 0, -1],
            "thrust": thrust, "isp": isp, "isp_sl": isp_sl, "plume": plume,
            "plume_width": width, "plume_glow": glow, "plume_density": density,
            "type": "rocket", "fuel": "kerosene"}


def make_ship(a, b, v):
    def stage(name, dry, fuel, z, length, radius, model, engines):
        return {"name": name, "dry_mass": dry, "propellant": fuel, "z": z, "length": length,
                "radius": radius, "separation_speed": 2, "engines": engines,
                "parts": [{"model": "models/" + model + ".obj"}]}

    def fairing(side, sign, with_next):
        return {"name": "Shroud " + side, "dry_mass": 2500, "propellant": 0,
                "z": -81, "length": 43.2, "radius": 3.05, "separation_speed": 2,
                "separation_dir": [sign, 0, -0.15], "with_next": with_next, "engines": [],
                "parts": [{"model": "models/shroud_" + side + ".obj"}]}

    ship = {
        "name": "CVELN1", "class": "launcher", "script": "@launcher.lua",
        "description": "N1-inspired three-stage cargo launcher. Split fairing and escape tower are an approximation.",
        "torque_accel": 0.12, "max_rate": 0.25, "rcs_thrust": 0,
        "drag_area": 90, "camera_distance": 140, "camera_target": [0, 0, -52],
        "ports": [{"name": "Payload", "position": [0, 0, -62.3], "direction": [0, 0, -1],
                   "up": [0, 1, 0], "stage": 4}],
        "payload_bays": [{"name": "Cargo shroud", "shape": "cylinder", "radius": 2.7,
                          "length": 20, "port": "Payload"}],
        "stages": [
            stage("Block A", 180800, 1750000, -15.05, 30.1, 8.44, "block_a",
                  [engine(x, y, -0.02, 1672000, 330, 297, 85, 1.05, 3.5, 0.8) for x, y in a]),
            stage("Block B", 52200, 505000, -40.35, 20.5, 5.15, "block_b",
                  [engine(x, y, -28.2, 1672000, 330, 280, 35, 0.8) for x, y in b]),
            fairing("left", -1, True),
            fairing("right", 1, False),
            stage("Block V", 13700, 175000, -56.15, 11.1, 3.65, "block_v",
                  [engine(x, y, -48.6, 400000, 350, 280, 17, 0.65) for x, y in v]),
        ],
    }
    ship["stages"][0]["gear"] = [[6, 0, 0], [-6, 0, 0], [0, 6, 0], [0, -6, 0]]
    (ADDON / "ship.json").write_text(json.dumps(ship, indent=2) + "\n")


if __name__ == "__main__":
    make_ship(*make_meshes())
