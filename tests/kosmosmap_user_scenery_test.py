#!/usr/bin/env python3
"""Exercise KosmosMap's user scenery priority and installation fallback."""

import os
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
PROGRAM = ROOT / "addons/kosmosmap/bin/kosmosmap"


def png(path, colour):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(b"\0" + bytes(colour)))
        + chunk(b"IEND", b"")
    )


def main():
    with tempfile.TemporaryDirectory() as temp:
        temp = Path(temp)
        root = temp / "install"
        user = temp / "config/kosmos/scenery"
        installed_image = root / "data/textures/mars.png"
        user_image = user / "data/textures-hires/mars.png"
        installed_tree = root / "import/orbiter/Mars/Elev.tree"
        user_tree = user / "import/orbiter/planets/Mars/Archive/Elev.tree"
        png(installed_image, (0, 0, 255))
        png(user_image, (255, 0, 0))
        for path in (installed_tree, user_tree):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"not an Orbiter tree")
        env = os.environ.copy()
        env["XDG_CONFIG_HOME"] = str(temp / "config")

        def capture():
            return subprocess.run(
                [str(PROGRAM), "--root", str(root), "--body", "Mars", "--no-link",
                 "--imagery", "--size", "64x64", "--shot", str(temp / "map.png")],
                env=env, text=True, capture_output=True, check=True,
            ).stdout

        output = capture()
        assert str(user_tree) in output and "user image mars.png" in output, output
        user_tree.unlink()
        user_image.unlink()
        output = capture()
        assert str(installed_tree) in output and "image mars.png" in output, output
        assert "user image" not in output, output
    print("KosmosMap user scenery priority and fallback: ok")


if __name__ == "__main__":
    main()
