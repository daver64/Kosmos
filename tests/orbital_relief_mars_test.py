#!/usr/bin/env python3
"""Check experimental Mars orbital relief and the existing lower-altitude fallback."""

import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageChops, ImageStat

ROOT = Path(__file__).resolve().parents[1]


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: orbital_relief_mars_test.py EXPERIMENTAL_KOSMOS")
    binary = Path(sys.argv[1]).resolve()
    for layer in ("Surf", "Elev"):
        path = ROOT / "import" / "orbiter" / "Mars" / "Archive" / f"{layer}.tree"
        if not path.is_file():
            raise SystemExit(f"Mars Orbiter test requires {path}")

    with tempfile.TemporaryDirectory() as directory:
        temp = Path(directory)
        env = dict(os.environ, HOME=directory, XDG_CONFIG_HOME=str(temp / "config"),
                   SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy", LP_NUM_THREADS="1")
        transition = {}
        baseline_green = 0
        for radius, orbital in ((4.15, True), (1.05, False), (1.469, False), (1.473, True)):
            shot = temp / f"mars-{radius}.png"
            command = [str(binary), "--root", str(ROOT), "--scenario", "ares_spider_mars_orbit",
                       "--cam-planet", str(radius), "--cam-phase", "0", "--hide-info", "--no-clutter",
                       "--no-clouds", "--gen-threads", "1", "--date", "2026-11-09 12:00:00", "--orbital-relief-patches", "24",
                       "--orbital-relief-meshes", "96", "--orbital-relief-textures", "32",
                       "--perf", "--shot", str(shot), "--shot-at", "8"]
            result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=100)
            output = result.stdout + result.stderr
            assert result.returncode == 0 and shot.is_file(), output
            if orbital:
                assert "Mars Orbiter relief budgets:" in output, output
                assert re.search(r"Mars Orbiter relief: patches [1-9]\d*", output), output
            else:
                assert "Mars Orbiter relief budgets:" not in output, output
            with Image.open(shot) as image:
                if radius == 4.15:
                    baseline_green = sum(1 for r, g, b in image.convert("RGB").getdata()
                                         if g > 40 and g > r * 1.5 and g > b * 1.5)
                rgb = image.convert("RGB").resize((64, 64))
                assert len(rgb.getcolors(4096)) > 8, output
                red, _, blue = rgb.getpixel((32, 32))
                assert red > blue + 5, (radius, red, blue, output)
                if radius in (1.469, 1.473):
                    transition[radius] = image.convert("RGB").resize((64, 36))
            print(f"Mars {'orbital relief' if orbital else 'ground fallback'} ok")

        difference = ImageStat.Stat(ImageChops.difference(transition[1.469], transition[1.473])).mean
        assert max(difference) < 12.0, f"Mars handoff colour mismatch: {difference}"
        print(f"Mars handoff mean absolute RGB differences: {difference}")

        for radius in (1.05, 1.0001):
            shot = temp / f"ground-relief-{radius}.png"
            command = [str(binary), "--root", str(ROOT), "--scenario", "ares_spider_mars_orbit",
                       "--cam-planet", str(radius), "--cam-phase", "0", "--hide-info", "--no-clutter",
                       "--no-clouds", "--gen-threads", "1", "--date", "2026-11-09 12:00:00", "--orbital-relief-ground",
                       "--orbital-relief-patches", "24", "--orbital-relief-meshes", "96",
                       "--orbital-relief-textures", "32", "--perf", "--shot", str(shot), "--shot-at", "8"]
            result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=100)
            output = result.stdout + result.stderr
            assert result.returncode == 0 and shot.is_file(), output
            assert "Mars Orbiter relief budgets:" in output, output
            assert re.search(r"Mars Orbiter relief: patches [1-9]\d*", output), output
            with Image.open(shot) as image:
                rgb = image.convert("RGB")
                red, _, blue = rgb.getpixel((rgb.width // 2, rgb.height // 2))
                assert red > blue + 5, (radius, red, blue, output)
                dark = sum(1 for pixel in rgb.getdata() if max(pixel) < 8) / (rgb.width * rgb.height)
                assert dark < 0.005, (radius, dark, output)
            print(f"Mars experimental ground relief at radius {radius} ok")

        shot = temp / "gale-guard.png"
        command = [str(binary), "--root", str(ROOT), "--scenario-file",
                   str(ROOT / "scenarios/CVEL/CVEL-Ares/ares_spider_gale_crater.json"),
                   "--hide-info", "--no-clutter", "--no-clouds", "--gen-threads", "1",
                   "--orbital-relief-ground", "--orbital-relief-patches", "24", "--orbital-relief-meshes", "96",
                   "--orbital-relief-textures", "32", "--perf", "--shot", str(shot), "--shot-at", "15"]
        result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=120)
        output = result.stdout + result.stderr
        assert result.returncode == 0 and shot.is_file(), output
        assert "Mars Orbiter ground relief: using existing terrain" in output, output
        assert re.search(r"Mars Orbiter relief: patches [1-9]\d*", output), output
        with Image.open(shot) as image:
            lander = list(image.convert("RGB").resize((1280, 720)).crop((560, 210, 720, 330)).getdata())
        assert sum(min(pixel) > 80 for pixel in lander) / len(lander) > 0.3, output
        print("Mars Gale Crater ground safeguard and visible lander ok")

        scenario = temp / "instrument-mars.json"
        scenario.write_text(json.dumps({
            "name": "Instrument Mars relief test",
            "date": "2026-11-09 12:00:00",
            "active": 0,
            "ships": [{
                "ship": "NASA Voyager", "body": "Mars", "orbit": {"altitude_km": 2000},
                "state": {"r": [5400000, 0, 0], "v": [0, 0, -2816], "q": [0, 0, 1, 0]},
            }],
        }))
        near_scenario = temp / "instrument-mars-near.json"
        near_data = json.loads(scenario.read_text())
        near_data["ships"][0]["orbit"]["altitude_km"] = 200
        near_data["ships"][0]["state"]["r"] = [3590000, 0, 0]
        near_scenario.write_text(json.dumps(near_data))
        for camera_radius, instrument_scenario in ((None, scenario), (1.05, scenario), (4.15, near_scenario)):
            shot = temp / f"instrument-scene-{camera_radius}.png"
            capture_env = dict(env, XDG_CONFIG_HOME=str(temp / f"instrument-config-{camera_radius}"))
            command = [str(binary), "--root", str(ROOT), "--scenario-file", str(instrument_scenario),
                       "--hide-info", "--no-clutter", "--no-clouds", "--gen-threads", "1",
                       "--orbital-relief-patches", "24", "--orbital-relief-meshes", "96",
                       "--orbital-relief-textures", "32", "--set-at", "4",
                       "/ships/ship[0]/instruments/instrument[0]/shoot=1",
                       "--shot", str(shot), "--shot-at", "8"]
            if camera_radius is not None:
                command.extend(["--cam-planet", str(camera_radius), "--cam-phase", "0"])
            result = subprocess.run(command, cwd=ROOT, env=capture_env, capture_output=True, text=True, timeout=100)
            output = result.stdout + result.stderr
            assert result.returncode == 0 and shot.is_file(), output
            assert "Mars Orbiter relief budgets:" in output, output
            captures = list((Path(capture_env["XDG_CONFIG_HOME"]) / "kosmos" / "instrument-shots" / "NASA_Voyager").glob("nac-*.png"))
            assert len(captures) == 1, output
            with Image.open(captures[0]) as image:
                assert image.size == (800, 800), image.size
                red, _, blue = image.convert("RGB").getpixel((400, 400))
                assert red > blue + 5, (camera_radius, red, blue, output)
            print(f"Mars instrument capture with camera radius {camera_radius} ok")

        bases = temp / "config" / "kosmos" / "bases"
        bases.mkdir(parents=True)
        (bases / "plane.stl").write_text(
            "solid plane\n"
            "facet normal 0 0 1\nouter loop\n"
            "vertex -800000 -800000 0\nvertex 800000 -800000 0\nvertex 800000 800000 0\n"
            "endloop\nendfacet\n"
            "facet normal 0 0 1\nouter loop\n"
            "vertex -800000 -800000 0\nvertex 800000 800000 0\nvertex -800000 800000 0\n"
            "endloop\nendfacet\nendsolid plane\n"
        )
        Image.new("RGB", (4, 4), (0, 255, 0)).save(bases / "green.png")
        (bases / "colour-test.json").write_text(json.dumps({"bases": [{
            "id": f"colour-test-{longitude}", "body": "Mars", "latitude": 0, "longitude": longitude,
            "flat_radius": 0, "blend": 0,
            "terrain_meshes": [{"file": "plane.stl", "image": "green.png", "blend": 0}],
        } for longitude in range(-180, 180, 45)]}))
        shot = temp / "mesh-colour.png"
        command = [str(binary), "--root", str(ROOT), "--scenario", "ares_spider_mars_orbit",
                   "--cam-planet", "4.15", "--cam-phase", "0", "--hide-info", "--no-clutter",
                   "--no-clouds", "--gen-threads", "1", "--date", "2026-11-09 12:00:00", "--orbital-relief-patches", "24",
                   "--orbital-relief-meshes", "96", "--orbital-relief-textures", "32",
                   "--perf", "--shot", str(shot), "--shot-at", "8"]
        result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=100)
        output = result.stdout + result.stderr
        assert result.returncode == 0 and shot.is_file(), output
        assert output.count("terrain mesh plane.stl:") == 8 and output.count(", coloured") >= 8, output
        assert re.search(r"Mars Orbiter relief: patches [1-9]\d*", output), output
        with Image.open(shot) as image:
            green = sum(1 for r, g, b in image.convert("RGB").getdata()
                        if g > 40 and g > r * 1.5 and g > b * 1.5)
        assert green > baseline_green + 100, (baseline_green, green, output)
        print(f"Mars rendered terrain mesh colour ok ({baseline_green} -> {green} green pixels)")


if __name__ == "__main__":
    main()
