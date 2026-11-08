#!/usr/bin/env python3
import importlib.util
import hashlib
import json
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch
import zipfile

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("orbiter_pack", ROOT / "scripts/pack-orbiter-defaults.py")
pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack)


class DefaultPackTest(unittest.TestCase):
    @staticmethod
    def fake_layer(_archive, directory, _layer, name):
        output = directory / f"{name}.dds"
        output.write_bytes(name.encode())
        return output

    @staticmethod
    def fake_tiles(_archive, directory):
        outputs = []
        for name in ("colour", "specular"):
            output = directory / name
            output.mkdir()
            (output / "tiles.ctx").write_text(name)
            outputs.append(output)
        return outputs

    def make_source(self, path, omit=""):
        with zipfile.ZipFile(path, "w") as archive:
            archive.writestr("LICENSE", "Orbiter licence")
            for body, layers in {"Earth": pack.EARTH, **pack.OTHER}.items():
                for layer in layers:
                    name = f"Textures/{body}/Archive/{layer}.tree"
                    if name != omit:
                        archive.writestr(name, name)

    def test_archives_have_all_layers_and_licence(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.make_source(root / "source.zip")
            with patch.object(pack, "convert_layer", self.fake_layer), patch.object(pack, "convert_earth_tiles", self.fake_tiles):
                pack.build(root / "source.zip", root / "out")
            for name, bodies in (("earth", {"Earth": pack.EARTH}), ("planets", pack.OTHER)):
                with tarfile.open(root / "out" / f"kosmos-orbiter-{name}.tar.gz") as archive:
                    prefix = f"data/orbiter-defaults/{name}/"
                    expected = {prefix + "LICENSE-Orbiter-2024.txt", prefix + ".complete"}
                    if name == "earth":
                        expected.add(prefix + "earth_clouds.dds")
                        expected.add(prefix + "earth_lights.dds")
                        expected.update({prefix + "colour", prefix + "colour/tiles.ctx",
                                         prefix + "specular", prefix + "specular/tiles.ctx"})
                        self.assertEqual(archive.extractfile(prefix + "earth_clouds.dds").read(), b"earth_clouds")
                        self.assertEqual(archive.extractfile(prefix + "earth_lights.dds").read(), b"earth_lights")
                    expected.update(f"{prefix}{body}/Archive/{layer}.tree"
                                    for body, layers in bodies.items() for layer in layers)
                    self.assertEqual(set(archive.getnames()), expected)
                    self.assertEqual(archive.extractfile(prefix + "LICENSE-Orbiter-2024.txt").read(),
                                     b"Orbiter licence")
                    for body, layers in bodies.items():
                        for layer in layers:
                            path = f"{body}/Archive/{layer}.tree"
                            self.assertEqual(archive.extractfile(prefix + path).read(),
                                             f"Textures/{path}".encode())
            manifest = json.loads((root / "out" / "packs.json").read_text())
            self.assertEqual(len(manifest["packs"]), 2)
            for item in manifest["packs"]:
                archive = root / "out" / item["file"]
                self.assertEqual(item["size"], archive.stat().st_size)
                self.assertEqual(item["sha256"], hashlib.sha256(archive.read_bytes()).hexdigest())
                self.assertEqual(item["marker"], item["remove"] + "/.complete")
                self.assertEqual(archive.stat().st_mode & 0o777, 0o644)
            self.assertEqual((root / "out" / "packs.json").stat().st_mode & 0o777, 0o644)

    def test_missing_layer_rejected_without_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.make_source(root / "source.zip", "Textures/Moon/Archive/Elev.tree")
            with self.assertRaisesRegex(ValueError, "Moon/Archive/Elev.tree"):
                pack.build(root / "source.zip", root / "out")
            self.assertEqual(list((root / "out").iterdir()), [])

    def test_mask_alpha_becomes_water_specular(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.make_source(root / "source.zip")
            (root / "work").mkdir()

            def fake_converter(args, check):
                self.assertTrue(check)
                tiles = Path(args[3])
                (tiles / "level0").mkdir(parents=True)
                (tiles / "tiles.ctx").write_text('TileType "dds"\n')
                image = Image.new("RGBA", (2, 1))
                image.putdata([(30, 40, 50, 0), (200, 150, 100, 255)])
                image.save(tiles / "level0" / "tx_0_0.dds")

            with zipfile.ZipFile(root / "source.zip") as archive, patch.object(pack.subprocess, "run", fake_converter):
                colour, specular = pack.convert_earth_tiles(archive, root / "work")
            self.assertTrue((colour / "level0" / "tx_0_0.dds").is_file())
            self.assertFalse((specular / "level0" / "tx_0_0.dds").exists())
            self.assertIn('TileType "png"', (specular / "tiles.ctx").read_text())
            with Image.open(specular / "level0" / "tx_0_0.png") as image:
                self.assertEqual(list(image.getdata()), [(255, 255, 255), (0, 0, 0)])

    def test_asset_size_limit(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.make_source(root / "source.zip")
            original = pack.LIMIT
            try:
                pack.LIMIT = 1
                with patch.object(pack, "convert_layer", self.fake_layer), patch.object(pack, "convert_earth_tiles", self.fake_tiles):
                    with self.assertRaisesRegex(ValueError, "exceeding GitHub"):
                        pack.build(root / "source.zip", root / "out")
                self.assertEqual(list((root / "out").iterdir()), [])
            finally:
                pack.LIMIT = original


if __name__ == "__main__":
    unittest.main()
