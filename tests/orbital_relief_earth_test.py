#!/usr/bin/env python3
"""Exercise the opt-in Orbiter ground path and its Earth fallback."""

import argparse
import os
import re
import subprocess
import tempfile
from pathlib import Path

from PIL import Image, ImageChops, ImageStat

ROOT = Path(__file__).resolve().parents[1]


def check_chippenham(shot, output):
    with Image.open(shot) as image:
        image = image.convert("RGB").resize((1280, 720))
        runway = list(image.crop((0, 480, 1280, 720)).getdata())
        wheel = list(image.crop((448, 424, 457, 436)).getdata())
        stripe = list(image.crop((458, 425, 467, 435)).getdata())
        hull = list(image.crop((500, 310, 780, 385)).getdata())
    runway_dark = sum(max(pixel) < 45 for pixel in runway) / len(runway)
    runway_marks = sum(min(pixel) > 80 and max(pixel) - min(pixel) < 20 for pixel in runway) / len(runway)
    wheel_dark = sum(max(pixel) < 85 for pixel in wheel) / len(wheel)
    stripe_light = sum(min(pixel) > 100 for pixel in stripe) / len(stripe)
    hull_red = sum(red > 65 and red > green * 1.35 and red > blue * 1.25 for red, green, blue in hull) / len(hull)
    assert runway_dark > 0.5 and runway_marks > 0.1, (runway_dark, runway_marks, output)
    assert wheel_dark > 0.4 and stripe_light > 0.6 and hull_red > 0.25, (wheel_dark, stripe_light, hull_red, output)
    return runway_dark, runway_marks, wheel_dark


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", help="experimental Kosmos executable")
    parser.add_argument("--vulkan", action="store_true", help="test Vulkan using an available SDL display")
    args = parser.parse_args()
    binary = Path(args.binary).resolve()
    graphics = ["--vulkan"] if args.vulkan else []
    for layer in ("Surf", "Elev"):
        path = ROOT / "import" / "orbiter" / "Earth" / "Archive" / f"{layer}.tree"
        if not path.is_file():
            raise SystemExit(f"Earth Orbiter test requires {path}")
    with tempfile.TemporaryDirectory() as directory:
        temp = Path(directory)
        video_driver = os.environ.get("SDL_VIDEODRIVER", "x11") if args.vulkan else "offscreen"
        env = dict(os.environ, HOME=directory, XDG_CONFIG_HOME=str(temp / "config"),
                   SDL_VIDEODRIVER=video_driver, SDL_AUDIODRIVER="dummy", LP_NUM_THREADS="1")
        images = {}
        for ground, enabled in ((False, True), (True, True), (False, False), (True, False)):
            shot = temp / f"earth-{ground}-{enabled}.png"
            command = [str(binary), *graphics, "--root", str(ROOT), "--scenario", "dragonfly_orbit",
                       "--cam-planet", "1.0001", "--cam-phase", "0", "--hide-info", "--no-clutter",
                       "--no-clouds", "--date", "2024-06-21 15:00:00", "--gen-threads", "1", "--orbital-relief-patches", "24",
                       "--orbital-relief-meshes", "96", "--orbital-relief-textures", "32",
                       "--set", f"/sim/settings/gfx-earth-relief={'true' if enabled else 'false'}",
                       "--perf", "--shot", str(shot), "--shot-at", "8"]
            if ground:
                command.append("--orbital-relief-ground")
            result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=100)
            output = result.stdout + result.stderr
            assert result.returncode == 0 and shot.is_file(), output
            if ground and enabled:
                assert re.search(r"\[perf\] Orbiter relief: patches [1-9]\d*", output), output
            else:
                assert not re.search(r"\[perf\] Orbiter relief: patches [1-9]\d*", output), output
            with Image.open(shot) as image:
                images[ground, enabled] = image.convert("RGB").resize((64, 36))
                assert max(ImageStat.Stat(images[ground, enabled]).mean) > 5, output
        difference = ImageStat.Stat(ImageChops.difference(images[False, True], images[True, True])).mean
        assert max(difference) > 1, difference
        fallback_difference = ImageStat.Stat(ImageChops.difference(images[False, False], images[True, False])).mean
        assert max(fallback_difference) < 3, fallback_difference
        print(f"Earth ground relief and toggle fallback ok, RGB difference {difference}")

        shot = temp / "earth-no-tiles.png"
        command = [str(binary), *graphics, "--root", str(ROOT), "--scenario", "dragonfly_orbit",
                   "--cam-planet", "1.0001", "--cam-phase", "0", "--hide-info", "--no-clutter",
                   "--no-clouds", "--no-tiles", "--date", "2024-06-21 15:00:00", "--gen-threads", "1",
                   "--orbital-relief-ground", "--set", "/sim/settings/gfx-earth-relief=true",
                   "--perf", "--shot", str(shot), "--shot-at", "8"]
        result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=100)
        output = result.stdout + result.stderr
        assert result.returncode == 0 and shot.is_file(), output
        assert "Earth Orbiter relief budgets:" not in output, output
        with Image.open(shot) as image:
            no_tiles = image.convert("RGB").resize((64, 36))
        fallback_difference = ImageStat.Stat(ImageChops.difference(images[False, True], no_tiles)).mean
        assert max(fallback_difference) < 3, fallback_difference
        print("Earth no-tiles ground fallback ok")

        for patches, meshes, textures, resumed in ((24, 96, 32, True), (2, 8, 10, False)):
            shot = temp / f"earth-camera-guard-{patches}.png"
            command = [str(binary), *graphics, "--root", str(ROOT), "--scenario", "dragonfly_orbit",
                       "--cam-planet", "1.00001", "--cam-phase", "0", "--hide-info", "--no-clutter",
                       "--no-clouds", "--date", "2024-06-21 15:00:00", "--gen-threads", "1",
                       "--orbital-relief-ground", "--orbital-relief-patches", str(patches),
                       "--orbital-relief-meshes", str(meshes), "--orbital-relief-textures", str(textures),
                       "--set", "/sim/settings/gfx-earth-relief=true", "--perf", "--shot", str(shot), "--shot-at", "15"]
            result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=100)
            output = result.stdout + result.stderr
            assert result.returncode == 0 and shot.is_file(), output
            transitions = re.findall(r"Earth Orbiter ground relief: (.+)", output)
            assert transitions and transitions[0].startswith(
                "using existing terrain until nearby surface heights agree: camera:"), output
            assert transitions[-1].startswith(
                "nearby surface heights agree" if resumed else "using existing terrain"), output
            assert re.search(r"\[perf\] Orbiter relief: patches [1-9]\d*", output), output
            print(f"Earth low-altitude camera safeguard with {patches} patches: {'resumed' if resumed else 'fallback'}")

        for case in ("startup", "default", "limited"):
            shot = temp / f"chippenham-{case}.png"
            # Vulkan's earliest readback can still contain the loading screen.
            shot_at = ("1" if args.vulkan else "0.1") if case == "startup" else "15"
            command = [str(binary), *graphics, "--root", str(ROOT), "--scenario-file",
                       str(ROOT / "scenarios/CVEL/CVEL-Deltaglider/deltaglider_chippenham.json"),
                       "--hide-info", "--no-clutter", "--no-clouds", "--date", "2020-07-30 11:50:00",
                       "--gen-threads", "1",
                       "--orbital-relief-ground", "--set", "/sim/settings/gfx-earth-relief=true",
                       "--perf", "--shot", str(shot), "--shot-at", shot_at]
            if case == "limited":
                command += ["--orbital-relief-patches", "24", "--orbital-relief-meshes", "96",
                            "--orbital-relief-textures", "32"]
            result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=120)
            output = result.stdout + result.stderr
            assert result.returncode == 0 and shot.is_file(), output
            if case != "startup":
                assert re.search(r"\[perf\] Orbiter relief: patches [1-9]\d*", output), output
            transitions = re.findall(r"Earth Orbiter ground relief: (.+)", output)
            assert transitions and transitions[-1].startswith(
                "nearby surface heights agree" if case == "default" else "using existing terrain"), output
            runway_dark, runway_marks, wheel_dark = check_chippenham(shot, output)
            print(f"Earth Chippenham {case}: "
                  f"runway {runway_dark:.1%} dark, {runway_marks:.1%} markings; gear {wheel_dark:.1%} dark")


if __name__ == "__main__":
    main()
