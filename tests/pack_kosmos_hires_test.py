#!/usr/bin/env python3
"""Test the managed map archive and its release catalogue without real artwork."""

import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import tarfile
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


builder = load("pack_kosmos_hires", "scripts/pack-kosmos-hires.py")
release = load("publish_github_release", "scripts/publish-github-release.py")


class ManagedMapsTest(unittest.TestCase):
    def test_build_merge_and_remove_preserves_overrides(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            source = root / "source"
            earth = source / "earth/JMII DDS"
            normal = source / "earth_normal/textures/medres/earth-normals-hs-32k"
            files = list(builder.tile_files(earth, "dds", 4)) + list(builder.tile_files(normal, "jpg", 5))
            files += [source / "earth/JMII DDS.ctx", earth / "level5/ReadMe.txt",
                      source / "earth_normal/README-earth-normals-hs-32k.txt",
                      normal.parent / "earth-normals-hs-32k.ctx"]
            for path in files:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(path.name.encode())
            for name in ("ida.dds", "earth.dds", "moon.dds", "mars.dds", "earth_clouds.dds", "earth_lights.dds"):
                (source / name).write_bytes(b"excluded")
            old = {"id": "orbiter", "file": "orbiter.tar.gz", "marker": "data/orbiter/.complete",
                   "remove": "data/orbiter", "size": 7, "sha256": hashlib.sha256(b"orbiter").hexdigest(),
                   "url": release.BASE + "download/v0.51/orbiter.tar.gz"}
            catalogue = root / "existing.json"
            catalogue.write_text(json.dumps({"packs": [old]}))
            output = root / "out"
            builder.build(source, catalogue, output)
            result, uploads = release.prepare_catalogue({"packs": [old]}, output, "v0.63")
            self.assertEqual(result["packs"][0], old)
            self.assertEqual(uploads, [output / builder.FILENAME])
            pack = result["packs"][1]
            self.assertEqual(pack["url"], release.BASE + "download/v0.63/" + builder.FILENAME)
            self.assertEqual(pack["sha256"], hashlib.sha256(uploads[0].read_bytes()).hexdigest())
            override = root / "install/data/textures-hires/earth.jpg"
            override.parent.mkdir(parents=True)
            override.write_bytes(b"custom")
            install = root / "install"
            with tarfile.open(uploads[0]) as archive:
                names = archive.getnames()
                self.assertEqual(names[-1], builder.PREFIX + "/.complete")
                self.assertTrue(all(name.startswith(builder.PREFIX + "/") for name in names))
                for name in ("ida.dds", "earth.dds", "moon.dds", "mars.dds", "earth_clouds.dds", "earth_lights.dds"):
                    self.assertNotIn(builder.PREFIX + "/" + name, names)
                archive.extractall(install, filter="data")
            self.assertEqual((install / pack["marker"]).read_bytes(), b"")
            self.assertEqual((install / builder.PREFIX / "earth/JMII DDS/level0/tx_0_0.dds").read_bytes(), b"tx_0_0.dds")
            self.assertIn("non-commercial", (install / builder.PREFIX / "CREDITS-kosmos-hires.txt").read_text())
            shutil.rmtree(install / pack["remove"])
            self.assertEqual(override.read_bytes(), b"custom")
            self.assertFalse((install / pack["marker"]).exists())
            output2 = root / "out2"
            builder.build(source, catalogue, output2)
            self.assertEqual((output / builder.FILENAME).read_bytes(), (output2 / builder.FILENAME).read_bytes())

    def test_rejects_missing_tiles(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            source = root / "source"
            source.mkdir()
            catalogue = root / "existing.json"
            catalogue.write_text('{"packs": []}')
            with self.assertRaisesRegex(ValueError, "Missing or empty source file"):
                builder.build(source, catalogue, root / "out")
            self.assertFalse((root / "out/packs.json").exists())

    def test_missing_changed_archive_is_not_reused(self):
        with tempfile.TemporaryDirectory() as work:
            directory = Path(work)
            old = {"id": "orbiter", "file": "orbiter.tar.gz", "marker": "data/orbiter/.complete",
                   "remove": "data/orbiter", "size": 7, "sha256": hashlib.sha256(b"orbiter").hexdigest(),
                   "url": release.BASE + "download/v0.51/orbiter.tar.gz"}
            changed = dict(old, sha256="0" * 64)
            (directory / "packs.json").write_text(json.dumps({"packs": [changed]}))
            with self.assertRaisesRegex(ValueError, "does not match"):
                release.prepare_catalogue({"packs": [old]}, directory, "v0.63")


if __name__ == "__main__":
    unittest.main()
