#!/usr/bin/env python3
"""Capture re-entry with the regular recorder and sample live read-only telemetry."""
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "sdk/examples"))
from kosmos_link import CAP_READ, Link, LinkError  # noqa: E402


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: capture-reentry-telemetry.py NEW-OUTPUT-DIRECTORY")
    output = Path(sys.argv[1]).resolve()
    if output.exists():
        raise SystemExit(f"output already exists: {output}")
    config = output / "config/kosmos"
    config.mkdir(parents=True)
    (config / "native_addons.json").write_text(json.dumps({"allowed": ["promo-telemetry"]}))
    env = os.environ.copy()
    port = "19420"
    env["PROMO_TELEMETRY_PORT"] = port
    process = subprocess.Popen(
        ["bash", str(ROOT / "scripts/promo/make-beta-promo.sh"), "clip", str(output), "reentry", "18"],
        env=env,
    )
    observations = []
    link = None
    try:
        deadline = time.monotonic() + 120
        while time.monotonic() < deadline and process.poll() is None:
            try:
                link = Link("promo-telemetry", "Promo telemetry", CAP_READ, address="127.0.0.1:" + port)
                break
            except (OSError, LinkError):
                time.sleep(0.25)
        if link is None or not link.granted & CAP_READ:
            raise RuntimeError("could not connect to the re-entry telemetry with read permission")
        for _ in range(8):
            if process.poll() is not None:
                break
            prefix = "/ships/ship[0]/"
            observations.append({key: link.get(prefix + path) for key, path in (
                ("altitude_m", "altitude"), ("surface_speed_m_s", "speed"),
                ("vertical_speed_m_s", "vertical-speed"), ("heat_flux_w_m2", "thermal/flux"),
                ("thrust_n", "thrust"),
            )})
            time.sleep(0.75)
        link.sock.close()
        link = None
        result = process.wait(timeout=120)
        if result:
            raise RuntimeError(f"capture exited with status {result}")
        if not any(row["heat_flux_w_m2"] > 20000 and row["surface_speed_m_s"] > 6000 and
                   row["vertical_speed_m_s"] < 0 and row["thrust_n"] < 1 for row in observations):
            raise RuntimeError(f"capture did not show unpowered descending re-entry conditions: {observations}")
        (output / "reentry-telemetry.json").write_text(json.dumps(observations, indent=2) + "\n")
        print("Verified re-entry telemetry:", json.dumps(observations))
    finally:
        if link is not None:
            link.sock.close()
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=15)


if __name__ == "__main__":
    main()
