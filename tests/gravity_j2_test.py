#!/usr/bin/env python3
"""Check the J2 runtime setting, persistence and warp transitions."""

import os
import json
import math
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "build/kosmos"


def potential(model, point):
    x, y, z = point
    r = math.sqrt(x * x + y * y + z * z)
    latitude = y / r
    longitude = math.atan2(-z, x)
    root = math.sqrt(max(0.0, 1.0 - latitude * latitude))
    result = 0.0
    diagonal = 1.0
    for m in range(model["degree"] + 1):
        if m:
            diagonal *= (2 * m - 1) * root
        previous, current = 0.0, diagonal
        for n in range(m, model["degree"] + 1):
            if n == m + 1:
                previous, current = current, (2 * m + 1) * latitude * current
            elif n > m + 1:
                previous, current = current, ((2 * n - 1) * latitude * current - (n + m - 1) * previous) / (n - m)
            norm = math.sqrt((2 * n + 1) * (1 if m == 0 else 2) *
                             math.exp(math.lgamma(n - m + 1) - math.lgamma(n + m + 1)))
            index = n * (n + 1) // 2 + m
            result += (model["reference_radius"] / r) ** n * norm * current * (
                model["cosine"][index] * math.cos(m * longitude) +
                model["sine"][index] * math.sin(m * longitude))
    return model["gm"] * result / r


def reference_acceleration(model):
    radius = model["reference_radius"]
    point = [1.05 * radius, 0.35 * radius, -0.25 * radius]
    h = radius * 1e-5
    values = []
    for axis in range(3):
        plus, minus = point.copy(), point.copy()
        plus[axis] += h
        minus[axis] -= h
        values.append((potential(model, plus) - potential(model, minus)) / (2 * h))
    return values


def frame_angles(frame, days):
    centuries = days / 36525.0
    ra = frame["ra"] + frame.get("ra_rate", 0.0) * centuries
    dec = frame["dec"] + frame.get("dec_rate", 0.0) * centuries
    w = frame["w0"] + frame["rate"] * days
    if frame.get("mercury_libration"):
        phase = [174.7910857, 349.5821714, 164.3732571, 339.1643429, 153.9554286]
        rate = [4.092335, 8.184670, 12.277005, 16.369340, 20.461675]
        terms = [0.01067257, -0.00112309, -0.00011040, -0.00002539, -0.00000571]
        w += sum(a * math.sin(math.radians(p + r * days)) for p, r, a in zip(phase, rate, terms))
    if frame.get("lunar_libration"):
        phase = [125.045, 250.089, 260.008, 176.625, 357.529, 311.589, 134.963,
                 276.617, 34.226, 15.134, 119.743, 239.961, 25.053]
        rate = [-0.0529921, -0.1059842, 13.0120009, 13.3407154, 0.9856003, 26.4057084,
                13.0649930, 0.3287146, 1.7484877, -0.1589763, 0.0036096, 0.1643573, 12.9590088]
        ra_terms = [-3.8787, -0.1204, 0.0700, -0.0172, 0, 0.0072, 0, 0, 0, -0.0052, 0, 0, 0.0043]
        dec_terms = [1.5419, 0.0239, -0.0278, 0.0068, 0, -0.0029, 0.0009, 0, 0, 0.0008, 0, 0, -0.0009]
        w_terms = [3.5610, 0.1208, -0.0642, 0.0158, 0.0252, -0.0066, -0.0047, -0.0046,
                   0.0028, 0.0052, 0.0040, 0.0019, -0.0044]
        for i in range(13):
            angle = math.radians(phase[i] + rate[i] * days)
            ra += ra_terms[i] * math.sin(angle)
            dec += dec_terms[i] * math.cos(angle)
            w += w_terms[i] * math.sin(angle)
        w -= 1.4e-12 * days * days
    return ra, dec, w


def equatorial(ra, dec):
    ra, dec, tilt = map(math.radians, (ra, dec, 23.4392911))
    x, y, z = math.cos(dec) * math.cos(ra), math.cos(dec) * math.sin(ra), math.sin(dec)
    return (x, -y * math.sin(tilt) + z * math.cos(tilt), -y * math.cos(tilt) - z * math.sin(tilt))


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


frames = {entry["name"]: entry["gravity_frame"] for entry in json.loads(
    (ROOT / "data/sol.json").read_text())["bodies"] if "gravity_frame" in entry}

with tempfile.TemporaryDirectory() as home:
    env = dict(os.environ, HOME=home, XDG_CONFIG_HOME=str(Path(home) / ".config"))
    for body in ("Earth", "Moon", "Mars", "Ceres"):
        result = subprocess.run(
            [str(BINARY), "--root", str(ROOT), "--ship", "CVEL Deltaglider", "--orbit", "--orbit-body", body,
             "--selftest", "1", "--set", "/selftest/gravity-j2=true"],
            cwd=ROOT, env=env, capture_output=True, text=True, timeout=120,
        )
        assert result.returncode == 0 and "J2 selftest: 0 failure(s)" in result.stdout, result.stdout + result.stderr
        seen = set()
        frame_seen = set()
        for line in result.stdout.splitlines():
            if line.startswith("gravity-frame "):
                _, name, *values = line.split()
                epoch, time, *vectors = map(float, values)
                prime, pole = vectors[:3], vectors[3:]
                frame = frames[name]
                ra, dec, w = frame_angles(frame, epoch - 2451545.0 + time / 86400.0)
                expected_pole = equatorial(ra, dec)
                node = equatorial(frame.get("node_ra", frame["ra"] + 90.0) + ra - frame["ra"], 0)
                tangent = (pole[1] * node[2] - pole[2] * node[1],
                           pole[2] * node[0] - pole[0] * node[2],
                           pole[0] * node[1] - pole[1] * node[0])
                assert math.dist(pole, expected_pole) < 1e-10, (name, pole, expected_pole)
                assert abs(dot(prime, node) - math.cos(math.radians(w))) < 1e-9, name
                assert abs(dot(prime, tangent) - math.sin(math.radians(w))) < 1e-9, name
                frame_seen.add((name, time))
                continue
            if not line.startswith("gravity-model "):
                continue
            _, name, *components = line.split()
            measured = [float(component) for component in components]
            model = json.loads((ROOT / "data/gravity" / f"{name.lower()}.json").read_text())
            expected = reference_acceleration(model)
            error = math.sqrt(sum((a - b) ** 2 for a, b in zip(measured, expected)))
            scale = math.sqrt(sum(a * a for a in expected))
            assert error < max(2e-10, scale * 2e-5), (name, measured, expected, error)
            seen.add(name)
        assert seen == {"Mercury", "Venus", "Earth", "Moon", "Mars", "Ceres", "Vesta"}, seen
        assert frame_seen == {(name, t) for name in seen for t in (0.0, 86400.0)}, frame_seen
    print("gravity_j2_test ok")
