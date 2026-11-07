#!/usr/bin/env python3
"""Verify the bundled files attributed to NASA are the documented originals."""

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
}


def main():
    for name, expected in EXPECTED.items():
        path = TEXTURES / name
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        assert actual == expected, f"{path}: SHA-256 {actual}, expected {expected}"
    print("NASA texture provenance checks passed")


if __name__ == "__main__":
    main()
