#!/usr/bin/env python3
"""Build separate standard Orbiter scenery archives from the Orbiter 2024 release ZIP."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile


LIMIT = 2 * 1024**3
EARTH = ("Cloud", "Elev", "Elev_mod", "Mask", "Surf")
OTHER = {
    "Callisto": ("Surf",), "Deimos": ("Elev", "Surf"), "Dione": ("Surf",),
    "Enceladus": ("Surf",), "Europa": ("Surf",), "Ganymede": ("Surf",),
    "Iapetus": ("Surf",), "Io": ("Elev", "Surf"), "Jupiter": ("Cloud", "Surf"),
    "Mars": ("Elev", "Surf"), "Mimas": ("Surf",), "Moon": ("Elev", "Surf"),
    "Phobos": ("Elev", "Surf"), "Rhea": ("Surf",), "Tethys": ("Surf",),
    "Vesta": ("Elev", "Surf"),
}


def convert_layer(archive, directory, layer, name):
    tree = directory / f"{layer}.tree"
    with archive.open(f"Textures/Earth/Archive/{layer}.tree") as source, tree.open("wb") as target:
        shutil.copyfileobj(source, target, 1024 * 1024)
    converter = Path(__file__).resolve().parents[1] / "tools/orbiter_import.py"
    subprocess.run([sys.executable, str(converter), str(tree), "--body", name,
                    "--level", "6", "--out", str(directory)], check=True)
    return directory / f"{name}.dds"


def convert_earth_tiles(archive, directory):
    from PIL import Image, ImageOps

    tools = Path(__file__).resolve().parents[1] / "tools"
    outputs = []
    for layer, name in (("Surf", "colour"), ("Mask", "specular")):
        tree = directory / f"{layer}.tree"
        if not tree.exists():
            with archive.open(f"Textures/Earth/Archive/{layer}.tree") as source, tree.open("wb") as target:
                shutil.copyfileobj(source, target, 1024 * 1024)
        tiles = directory / name
        subprocess.run([sys.executable, str(tools / "orbiter_tiles.py"), str(tree), str(tiles),
                        "--max-level", "6"], check=True)
        if layer == "Mask":
            for tile in tiles.glob("level*/*.dds"):
                with Image.open(tile) as image:
                    water = ImageOps.invert(image.convert("RGBA").getchannel("A"))
                    water.convert("RGB").save(tile.with_suffix(".png"))
                tile.unlink()
            descriptor = tiles / "tiles.ctx"
            descriptor.write_text(descriptor.read_text().replace('TileType "dds"', 'TileType "png"'))
        outputs.append(tiles)
    return outputs


def build(source, output):
    groups = {"earth": {"Earth": EARTH}, "planets": OTHER}
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(source) as archive:
        names = archive.namelist()
        required = ["LICENSE"]
        for bodies in groups.values():
            required.extend(f"Textures/{body}/Archive/{layer}.tree"
                            for body, layers in bodies.items() for layer in layers)
        for name in required:
            if names.count(name) != 1:
                raise ValueError(f"Expected exactly one {name} in the Orbiter release")
        manifest = []
        for group, bodies in groups.items():
            filename = f"kosmos-orbiter-{group}.tar.gz"
            target = output / filename
            with tempfile.NamedTemporaryFile(dir=output, prefix=".orbiter-", delete=False) as temp:
                temporary = Path(temp.name)
            try:
                with tempfile.TemporaryDirectory(dir=output, prefix=".orbiter-cloud-") as work:
                    converted = ([convert_layer(archive, Path(work), "Cloud", "earth_clouds"),
                                  convert_layer(archive, Path(work), "Mask", "earth_lights")]
                                 if group == "earth" else [])
                    tile_sets = convert_earth_tiles(archive, Path(work)) if group == "earth" else []
                    with tarfile.open(temporary, "w:gz", compresslevel=1) as pack:
                        for name in ["LICENSE"] + [
                            f"Textures/{body}/Archive/{layer}.tree"
                            for body, layers in bodies.items() for layer in layers
                        ]:
                            info = archive.getinfo(name)
                            prefix = f"data/orbiter-defaults/{group}/"
                            destination = (prefix + "LICENSE-Orbiter-2024.txt" if name == "LICENSE"
                                           else prefix + name[len("Textures/"):])
                            entry = tarfile.TarInfo(destination)
                            entry.size = info.file_size
                            entry.mode = 0o644
                            with archive.open(info) as content:
                                pack.addfile(entry, content)
                        for image in converted:
                            pack.add(image, arcname=f"data/orbiter-defaults/earth/{image.name}")
                        for tiles in tile_sets:
                            pack.add(tiles, arcname=f"data/orbiter-defaults/earth/{tiles.name}")
                        entry = tarfile.TarInfo(f"data/orbiter-defaults/{group}/.complete")
                        entry.size = 0
                        entry.mode = 0o644
                        pack.addfile(entry)
                size = temporary.stat().st_size
                if size >= LIMIT:
                    raise ValueError(f"{filename} is {size} bytes, exceeding GitHub's 2 GiB asset limit")
                digest = hashlib.sha256()
                with temporary.open("rb") as content:
                    for block in iter(lambda: content.read(1024 * 1024), b""):
                        digest.update(block)
                os.replace(temporary, target)
                print(f"{digest.hexdigest()}  {filename}  ({size} bytes)")
                manifest.append({
                    "id": f"orbiter-default-{group}",
                    "name": f"Orbiter standard scenery: {group.title()}",
                    "description": "Standard Orbiter 2024 planetary archives. Surface and elevation are used by Kosmos; Earth includes converted clouds and night lights. Other layers are retained for future support.",
                    "file": filename,
                    "url": f"https://github.com/daver64/Kosmos/releases/latest/download/{filename}",
                    "marker": f"data/orbiter-defaults/{group}/.complete",
                    "remove": f"data/orbiter-defaults/{group}",
                    "size": size,
                    "sha256": digest.hexdigest(),
                })
            finally:
                temporary.unlink(missing_ok=True)
        manifest_path = output / "packs.json"
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=output, prefix=".orbiter-", delete=False) as temp:
            temporary = Path(temp.name)
        try:
            with temporary.open("w", encoding="utf-8") as temp:
                json.dump({"packs": manifest}, temp, indent=2)
                temp.write("\n")
            os.replace(temporary, manifest_path)
        finally:
            temporary.unlink(missing_ok=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("orbiter_zip", help="Unmodified Orbiter 2024 release ZIP")
    parser.add_argument("output_directory")
    args = parser.parse_args()
    build(args.orbiter_zip, args.output_directory)
