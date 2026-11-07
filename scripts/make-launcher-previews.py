#!/usr/bin/env python3
"""Generate fresh launcher previews from the current packaged simulator."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
UNRESTORED = {
    "Ares Spider", "CVEL Buran", "CVEL Energia", "CVEL Centaur G",
    "CVEL Transtage", "Luna E3",
    "CVEL Shuttle-A Fuel Pod",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path, help="path to a packaged Kosmos executable")
    parser.add_argument("--force", action="store_true", help="replace existing generated previews")
    parser.add_argument("--scenario", action="append", default=[], help="capture only the named scenario ID")
    args = parser.parse_args()
    executable = args.executable.resolve(strict=True)
    scenarios = sorted((ROOT / "scenarios").rglob("*.json"))
    scenarios += sorted((ROOT / "addons").glob("*/scenarios/**/*.json"))
    ships = {}
    for manifest in (ROOT / "addons").glob("*/addon.json"):
        addon = json.loads(manifest.read_text())
        for filename in addon.get("ships", []):
            ship = json.loads((manifest.parent / filename).read_text())
            ships[ship["name"]] = ship
    if args.scenario:
        wanted = set(args.scenario)
        scenarios = [path for path in scenarios if path.stem in wanted]
        missing = wanted - {path.stem for path in scenarios}
        if missing:
            parser.error(f"unknown scenario IDs: {', '.join(sorted(missing))}")
    env = os.environ.copy()
    env["MAGICK_THREAD_LIMIT"] = "1"
    env["OMP_NUM_THREADS"] = "1"
    with tempfile.TemporaryDirectory(prefix="kosmos-previews-") as directory:
        temp = Path(directory)
        env["HOME"] = str(temp)
        env["XDG_CONFIG_HOME"] = str(temp / "config")
        for scenario in scenarios:
            data = json.loads(scenario.read_text())
            if not isinstance(data.get("ships"), list):
                raise ValueError(f"{scenario}: missing ships")
            unrestored = sorted({entry["ship"] for entry in data["ships"]} & UNRESTORED)
            if unrestored:
                print(f"skipping {scenario.relative_to(ROOT)}: unrestored models: {', '.join(unrestored)}", flush=True)
                continue
            active = data.get("active", 0)
            if not isinstance(active, int) or not 0 <= active < len(data["ships"]):
                raise ValueError(f"{scenario}: invalid active ship")
            surface = data["ships"][active].get("surface", {})
            base = surface.get("base", "").lower()
            if base == "kennedy":
                print(f"skipping {scenario.relative_to(ROOT)}: bundled Earth imagery does not resolve Kennedy's coast",
                      flush=True)
                continue
            date = "2026-06-01 15:00:00" if base in ("kennedy", "vandenberg") else "2026-06-01 06:00:00"
            ship_name = data["ships"][active]["ship"]
            if ship_name not in ships:
                raise ValueError(f"{scenario}: unknown ship {ship_name}")
            output = scenario.parent / "previews" / f"{scenario.stem}.jpg"
            if output.exists() and not args.force:
                print(f"keeping {output.relative_to(ROOT)}", flush=True)
                continue
            screenshot = temp / "shot.png"
            screenshot.unlink(missing_ok=True)
            command = [
                str(executable), "--scenario-file", str(scenario), "--hide-info",
                "--no-clouds", "--gen-threads", "1", "--date", date,
                "--set", "/sim/settings/weather-rain=0",
                "--set", "/sim/settings/link-listen=0",
                "--set", "/sim/scenario-view=chase",
                "--cam-yaw", "0.8", "--cam-pitch", "0.1",
                "--shot", str(screenshot), "--shot-at", "6",
            ]
            if "camera_distance" in ships[ship_name]:
                command.extend(("--cam-distance", str(ships[ship_name]["camera_distance"] * 1.6)))
            print(f"capturing {scenario.relative_to(ROOT)}", flush=True)
            result = subprocess.run(command, cwd=executable.parent, env=env,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    text=True, timeout=180)
            if result.returncode or not screenshot.is_file():
                raise RuntimeError(f"{scenario}: screenshot failed ({result.returncode}):\n{result.stdout}")
            with Image.open(screenshot) as image:
                image.load()
                image = image.convert("RGB")
                image.thumbnail((640, 360), Image.Resampling.LANCZOS)
                output.parent.mkdir(parents=True, exist_ok=True)
                image.save(output, "JPEG", quality=90)
            print(f"saved {output.relative_to(ROOT)}", flush=True)


if __name__ == "__main__":
    main()
