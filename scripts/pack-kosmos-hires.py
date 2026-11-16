#!/usr/bin/env python3
"""Package original Celestia Earth tiles and add them to an existing catalogue."""

import argparse
import gzip
import hashlib
import io
import json
import os
from pathlib import Path
import tarfile
import tempfile


ROOT = Path(__file__).resolve().parents[1]
FILENAME = "kosmos-hires-maps.tar.gz"
PACK_ID = "kosmos-hires-maps"
PREFIX = "data/kosmos-hires/maps"
LIMIT = 2 * 1024**3


def tile_files(folder, suffix, max_level):
    for level in range(max_level + 1):
        for row in range(1 << level):
            for col in range(1 << (level + 1)):
                yield folder / f"level{level}" / f"tx_{col}_{row}.{suffix}"


def build(source, catalogue_path, output):
    source, catalogue_path, output = map(Path, (source, catalogue_path, output))
    catalogue = json.loads(catalogue_path.read_text(encoding="utf-8"))
    if not isinstance(catalogue, dict) or not isinstance(catalogue.get("packs"), list):
        raise ValueError("The existing catalogue must contain a packs array")
    if any(not isinstance(item, dict) for item in catalogue["packs"]):
        raise ValueError("Invalid pack in existing catalogue")
    for item in catalogue["packs"]:
        if item.get("id") == PACK_ID or item.get("file") == FILENAME:
            raise ValueError("The existing catalogue already contains this pack; remove its old entry first")
    earth = source / "earth"
    normal = source / "earth_normal"
    earth_tiles = earth / "JMII DDS"
    normal_medres = normal / "textures/medres"
    normal_tiles = normal_medres / "earth-normals-hs-32k"
    files = [
        (earth / "JMII DDS.ctx", "earth/JMII DDS.ctx"),
        (earth_tiles / "level5/ReadMe.txt", "earth/ReadMe.txt"),
        (normal / "README-earth-normals-hs-32k.txt", "normal/README-earth-normals-hs-32k.txt"),
        (normal_medres / "earth-normals-hs-32k.ctx", "normal/earth-normals-hs-32k.ctx"),
        (ROOT / "data/CREDITS-kosmos-hires.txt", "CREDITS-kosmos-hires.txt"),
        (ROOT / "data/CREDITS-textures.md", "CREDITS-textures.md"),
    ]
    files.extend((path, "earth/JMII DDS/" + path.relative_to(earth_tiles).as_posix())
                 for path in tile_files(earth_tiles, "dds", 4))
    files.extend((path, "normal/earth-normals-hs-32k/" + path.relative_to(normal_tiles).as_posix())
                 for path in tile_files(normal_tiles, "jpg", 5))
    for path, _ in files:
        if not path.is_file() or path.stat().st_size == 0:
            raise ValueError(f"Missing or empty source file: {path}")

    output.mkdir(parents=True, exist_ok=True)
    if (output / "packs.json").exists() or (output / FILENAME).exists():
        raise ValueError("Output already contains packs.json or the Kosmos archive")
    with tempfile.NamedTemporaryFile(dir=output, prefix=".kosmos-hires-", delete=False) as temp:
        temporary = Path(temp.name)
    try:
        with temporary.open("wb") as stream, gzip.GzipFile(filename="", mode="wb", fileobj=stream, compresslevel=1, mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode="w") as archive:
                for path, name in files:
                    entry = tarfile.TarInfo(f"{PREFIX}/{name}")
                    entry.size = path.stat().st_size
                    entry.mode = 0o644
                    with path.open("rb") as content:
                        archive.addfile(entry, content)
                entry = tarfile.TarInfo(f"{PREFIX}/.complete")
                entry.size = 0
                entry.mode = 0o644
                archive.addfile(entry, io.BytesIO(b""))
        size = temporary.stat().st_size
        if size >= LIMIT:
            raise ValueError(f"{FILENAME} exceeds GitHub's 2 GiB asset limit")
        sha = hashlib.sha256()
        with temporary.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                sha.update(block)
        catalogue["packs"].append({
            "id": PACK_ID,
            "name": "Kosmos high-resolution maps",
            "description": "Jestr's Celestia Earth surface tiles and Harald Schmidt's normal tiles. Non-commercial use only.",
            "file": FILENAME,
            "url": f"https://github.com/daver64/Kosmos/releases/latest/download/{FILENAME}",
            "marker": f"{PREFIX}/.complete",
            "remove": PREFIX,
            "size": size,
            "sha256": sha.hexdigest(),
        })
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=output, prefix=".kosmos-hires-", delete=False) as temp:
            manifest = Path(temp.name)
            json.dump(catalogue, temp, indent=2)
            temp.write("\n")
        try:
            os.chmod(temporary, 0o644)
            os.chmod(manifest, 0o644)
            os.replace(temporary, output / FILENAME)
            os.replace(manifest, output / "packs.json")
        finally:
            manifest.unlink(missing_ok=True)
    finally:
        temporary.unlink(missing_ok=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_directory", help="Celestia hires directory containing the original Earth and normal tiles")
    parser.add_argument("existing_catalogue", help="packs.json from the latest published release")
    parser.add_argument("output_directory", help="new directory for the archive and complete catalogue")
    args = parser.parse_args()
    build(args.source_directory, args.existing_catalogue, args.output_directory)
