#!/usr/bin/env python3
"""Exercise opt-in Orbiter microdetail against synthetic assets on Mars."""

import os
import resource
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageChops, ImageStat

ROOT = Path(__file__).resolve().parents[1]


def solid_dds(path):
    header = bytearray(128)
    header[:4] = b"DDS "
    for offset, value in ((4, 124), (8, 0x1007), (12, 8), (16, 8), (76, 32), (80, 4), (108, 0x1000)):
        struct.pack_into("<I", header, offset, value)
    header[84:88] = b"DXT1"
    block = struct.pack("<HHI", 1, 0, 0)
    path.write_bytes(header + block * 4)


def disable_cores():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def main():
    if len(sys.argv) not in (2, 3):
        raise SystemExit("usage: orbital_microdetail_test.py EXPERIMENTAL_KOSMOS [ORBITER_ROOT]")
    binary = Path(sys.argv[1]).resolve()
    for layer in ("Surf", "Elev"):
        if not (ROOT / "import" / "orbiter" / "Mars" / "Archive" / f"{layer}.tree").is_file():
            raise SystemExit(f"Mars Orbiter test requires {layer}.tree")
    if len(sys.argv) == 3:
        for layer in ("Surf", "Elev"):
            if not (ROOT / "import" / "orbiter" / "Moon" / "Archive" / f"{layer}.tree").is_file():
                raise SystemExit(f"Moon Orbiter test requires {layer}.tree")
        orbiter_root = Path(sys.argv[2]).resolve()
        for texture in ("D3D9Moon_A.dds", "D3D9Moon_C2.dds", "D3D9Moon_C.dds"):
            if not (orbiter_root / "Textures" / texture).is_file():
                raise SystemExit(f"Moon Orbiter microdetail test requires Textures/{texture}")
    with tempfile.TemporaryDirectory() as directory:
        temp = Path(directory)
        assets = temp / "orbiter"
        (assets / "Config").mkdir(parents=True)
        (assets / "Textures").mkdir()
        solid_dds(assets / "Textures" / "dark.dds")
        (assets / "Config" / "MicroTex.cfg").write_text(
            "BODY Mars\nNORMALS 0\nLEVEL 0 dark.dds 20\nLEVEL 1 dark.dds 3\nLEVEL 2 dark.dds 1\n",
            encoding="utf-8",
        )
        vulkan = os.environ.get("KOSMOS_TEST_VULKAN") == "1"
        env = dict(os.environ, HOME=directory, XDG_CONFIG_HOME=str(temp / "config"),
                   SDL_VIDEODRIVER="x11" if vulkan else "offscreen", SDL_AUDIODRIVER="dummy", LP_NUM_THREADS="1")
        images = {}
        runs = [("synthetic", assets, False, "1.0001"), ("disabled", assets, True, "1.0001"),
                ("high", assets, False, "1.005")]
        if len(sys.argv) == 3:
            runs.append(("orbiter", Path(sys.argv[2]).resolve(), False, "1.0001"))
        for label, texture_root, disabled, camera_radius in runs:
            shot = temp / f"{label}.png"
            command = [str(binary), "--root", str(ROOT), "--scenario", "ares_spider_mars_orbit",
                       "--cam-planet", camera_radius, "--cam-phase", "0", "--hide-info", "--no-clutter",
                       "--no-clouds", "--gen-threads", "1", "--date", "2026-11-09 12:00:00",
                       "--orbital-relief-ground", "--orbital-relief-patches", "24",
                       "--orbital-relief-meshes", "96", "--orbital-relief-textures", "32",
                       "--orbiter-microtexture-root", str(texture_root), "--shot", str(shot), "--shot-at", "8"]
            if disabled:
                command.append("--no-detail")
            if vulkan:
                command.append("--vulkan")
            result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True,
                                    timeout=120, preexec_fn=disable_cores)
            output = result.stdout + result.stderr
            assert result.returncode == 0 and shot.is_file(), output
            assert ("Mars Orbiter microdetail loaded" in output) != disabled, output
            assert "Orbiter microdetail draw failed" not in output, output
            with Image.open(shot) as image:
                images[label] = image.convert("RGB").resize((64, 36))
        difference = ImageStat.Stat(ImageChops.difference(images["synthetic"], images["disabled"])).mean
        assert max(difference) > 5.0, difference
        print(f"Mars microdetail and --no-detail image difference: {difference}")
        print("Mars microdetail available above 15 km datum altitude")
        if "orbiter" in images:
            actual = ImageStat.Stat(ImageChops.difference(images["orbiter"], images["disabled"])).mean
            assert max(actual) > 0.05, actual
            print(f"Mars Orbiter-asset image difference: {actual}")

            moon_images = {}
            for disabled in (False, True):
                shot = temp / f"moon-{disabled}.png"
                command = [str(binary), "--root", str(ROOT), "--scenario-file",
                           str(ROOT / "scenarios/CVEL/CVEL-Bases/lunar_rover_tranquility.json"),
                           "--cam-planet", "1.00001", "--cam-phase", "0", "--hide-info", "--no-clutter",
                           "--no-clouds", "--gen-threads", "1", "--orbital-relief-ground",
                           "--orbital-relief-patches", "24", "--orbital-relief-meshes", "96",
                           "--orbital-relief-textures", "32", "--orbiter-microtexture-root", str(texture_root),
                           "--shot", str(shot), "--shot-at", "8"]
                if vulkan:
                    command.append("--vulkan")
                if disabled:
                    command.append("--no-detail")
                result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True,
                                        timeout=120, preexec_fn=disable_cores)
                output = result.stdout + result.stderr
                assert result.returncode == 0 and shot.is_file(), output
                assert ("Moon Orbiter microdetail loaded" in output) != disabled, output
                assert "Orbiter microdetail draw failed" not in output, output
                with Image.open(shot) as image:
                    moon_images[disabled] = image.convert("RGB")
            moon_difference = ImageStat.Stat(ImageChops.difference(moon_images[False], moon_images[True])).mean
            assert max(moon_difference) > 0.05, moon_difference
            print(f"Moon Orbiter normal-map image difference: {moon_difference}")


if __name__ == "__main__":
    main()
