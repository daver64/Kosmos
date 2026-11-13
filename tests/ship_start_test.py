#!/usr/bin/env python3
"""Verify that explicit ship starts use the Chippenham pad, even for Kestrel."""

import json
import os
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "build/kosmos"
sys.path.insert(0, str(ROOT / "sdk/examples"))
from kosmos_link import CAP_READ, Link


def start_and_read(config, *args):
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]
    env = dict(os.environ, HOME=str(config.parent), XDG_CONFIG_HOME=str(config),
               SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy", LP_NUM_THREADS="4")
    command = [str(BINARY), "--root", str(ROOT), "--link", "--link-port", str(port),
               "--no-tiles", "--no-relief", "--no-clouds", "--no-clutter", *args]
    with tempfile.TemporaryFile(mode="w+") as output:
        process = subprocess.Popen(command, cwd=ROOT, env=env, stdout=output, stderr=output)
        try:
            deadline = time.monotonic() + 120
            while True:
                try:
                    link = Link("shipedit-launch-test", "ShipEdit launch test", CAP_READ,
                                address=f"127.0.0.1:{port}")
                    break
                except OSError:
                    if process.poll() is not None or time.monotonic() >= deadline:
                        output.seek(0)
                        raise AssertionError("KosmosLink did not start:\n" + output.read())
                    time.sleep(0.25)
            try:
                assert link.granted & CAP_READ, "KosmosLink read permission was not granted"
                ships = link.ships()
            finally:
                link.sock.close()
            flown = [ship for ship in ships if ship["flags"] & 1]
            assert len(flown) == 1, ships
            return flown[0], ships
        finally:
            process.terminate()
            try:
                process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


def main():
    with tempfile.TemporaryDirectory() as directory:
        config = Path(directory) / "config"
        kosmos = config / "kosmos"
        (kosmos / "scenarios").mkdir(parents=True)
        (kosmos / "native_addons.json").write_text(json.dumps({"allowed": ["shipedit-launch-test"]}))
        (kosmos / "scenarios/default.json").write_text(json.dumps({
            "name": "Custom default",
            "ships": [{"ship": "Ares Spider", "body": "Mars", "orbit": {"altitude_km": 200}}],
        }))
        default, _ = start_and_read(config)
        assert default["name"] == "Ares Spider" and default["body"] == "Mars", default
        selected, _ = start_and_read(config, "--ship", "0")
        assert selected["name"] == "Ares Spider" and selected["body"] == "Mars", selected
        ship, ships = start_and_read(config, "--ship", "Kestrel")
        assert ship["name"] == "Kestrel" and ship["body"] == "Earth", ship
        assert abs(ship["latitude"] - 51.4585) < 0.02 and abs(ship["longitude"] + 2.1190) < 0.02, ship
        assert ship["altitude"] < 500 and len([s for s in ships if s["body"] == "Earth"]) == 1, ships
    print("ship_start_test ok")


if __name__ == "__main__":
    main()
