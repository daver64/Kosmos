#!/usr/bin/env python3
"""Verify bundled NASA images and documented derivatives match their hashes."""

import hashlib
from pathlib import Path

TEXTURES = Path(__file__).resolve().parents[1] / "data/textures"
EXPECTED = {
    "earth.jpg": "1684c4f8f51970dcb4a7451302bf3be17bed657aed9fece6f80d7b191e8afa3d",
    "mars.jpg": "12ec6bf02ebd42a246edc778cb2ce8c595b4d9f0892badf0bfff8abb2303c780",
    "europa.jpg": "50d01ca5c01352d1c507bb17fefd1c46ba11260305656250062c6cdac89c8ebf",
    "ganymede.jpg": "1aa9d5a6f20e72230c13ce3d78592fc056e22f1e1ee39ea7acfa7e08d09fe173",
    "phobos.jpg": "8ec807b353eb28f44b5aa24b8a887935ee5563f5b63bde2430a9988220f71463",
    "callisto.jpg": "b9f4fa5db94debabd12814853fd132db3449889646a490b6f95a897b2c47b121",
    "io.jpg": "79239daa0ac6438d02a7b536f3eb8d044098b8cf33c8f2f91dbab4f5f054bfa7",
    "deimos.jpg": "204e89cc7dc128e9fa6ae5f14cc23dc7917a4a8ccab9feff1a9544d22b354dce",
    "jupiter.jpg": "ac2d387f8ea56a59f45dea37993c779a730f49d7d9fdbc8d36c307a351b1f1a1",
    "rhea.jpg": "3e2f95a4891bb9feec768b1ebe992624bf2e425443aaf8b9af0e3d35329ff2e1",
    "dione.jpg": "c55d228900524557b6aff323f4b9391fc9816433e213a24ccf348c9b4291f89e",
    "tethys.jpg": "84f70e3fe6fa48661df4d973f78a540771e629ebbc68e032539d2e28e1d8eea1",
    "mimas.jpg": "261e08814b6d8203b874123a5e73fa3d6237737c6599eb88191e1a43514f40a2",
    "enceladus.jpg": "114a87475bd6bd277e0de00b46773c947678400efd8849603fe11c5b4ddb8e27",
    "iapetus.jpg": "9acb66830defdeffcf7938976c737d772600d9bc808e8cf45829690e8d1c7405",
}


def main():
    for name, expected in EXPECTED.items():
        path = TEXTURES / name
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        assert actual == expected, f"{path}: SHA-256 {actual}, expected {expected}"
    print("NASA texture provenance checks passed")


if __name__ == "__main__":
    main()
