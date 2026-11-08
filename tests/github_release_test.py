#!/usr/bin/env python3
"""Exercise release publication without network access or real builds."""

import argparse
import copy
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch
import zipfile


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("publish_github_release", ROOT / "scripts/publish-github-release.py")
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


def pack(data=b"scenery", url=None):
    return {"id": "earth", "file": "earth.tar.gz", "marker": "data/earth/.complete",
            "remove": "data/earth", "size": len(data), "sha256": hashlib.sha256(data).hexdigest(),
            "url": url or release.BASE + "latest/download/earth.tar.gz"}


class FakeGitHub:
    def __init__(self):
        self.tag = "v0.51"
        self.pack = pack()
        self.latest = {"tag_name": self.tag, "draft": False, "prerelease": False,
                       "assets": [{"name": "earth.tar.gz", "size": self.pack["size"]}]}
        self.contents = {self.tag: {
            "packs.json": json.dumps({"packs": [self.pack]}).encode(),
            "SHA256SUMS.txt": (self.pack["sha256"] + "  earth.tar.gz\n").encode()}}
        self.commands = []
        self.corrupt = False

    def call(self, *args, capture=False):
        self.commands.append(args)
        if args[:2] == ("release", "create"):
            self.contents[args[2]] = {}
        if args[:2] == ("release", "upload"):
            files = args[3:args.index("--repo")]
            self.contents[args[2]].update({Path(path).name: Path(path).read_bytes() for path in files})
        if args[:2] == ("release", "edit"):
            self.tag = args[2]
        return ""

    def api(self, path):
        if "/commits/" in path:
            return {"sha": "a" * 40}
        if "/matching-refs/" in path:
            return []
        if "/git/ref/tags/" in path:
            return {"object": {"type": "commit", "sha": "a" * 40}}
        if path.endswith("/latest"):
            if self.tag == "v0.51":
                return copy.deepcopy(self.latest)
            return {"tag_name": self.tag, "draft": False}
        tag = path.rsplit("/", 1)[-1]
        if tag == "v0.51":
            return copy.deepcopy(self.latest)
        return {"draft": True, "assets": [{"name": name, "size": len(data)}
                                           for name, data in self.contents[tag].items()]}

    def draft(self, tag):
        return {"isDraft": True, "targetCommitish": "a" * 40,
                "assets": [{"name": name, "size": len(data)} for name, data in self.contents[tag].items()]}

    def releases(self):
        return iter([self.latest])

    def download(self, tag, name, directory):
        data = self.contents[tag][name]
        if self.corrupt and tag == "v0.52" and name.endswith(".zip"):
            data = b"corrupted"
        path = directory / name
        path.write_bytes(data)
        return path


class ReleaseTest(unittest.TestCase):
    def test_titan_materials_use_existing_single_case_textures(self):
        models = ROOT / "addons/cvel-titan/models"
        textures = models / "textures"
        for stem in ("titan21m.png", "titan22m.png"):
            matches = [path for path in textures.iterdir() if path.name.casefold() == stem]
            self.assertEqual([path.name for path in matches], [stem])
        references = []
        for material in models.glob("*.mtl"):
            for line in material.read_text().splitlines():
                if line.startswith("map_Kd ") and line.split(None, 1)[1].lower().endswith(
                        ("titan21m.png", "titan22m.png")):
                    texture = line.split(None, 1)[1]
                    references.append(texture)
                    self.assertTrue((models / texture).is_file(), f"{material}: {texture}")
        self.assertIn("textures/titan21m.png", references)
        self.assertIn("textures/titan22m.png", references)

    def test_windows_zip_rejects_case_and_duplicate_collisions(self):
        examples = [
            ("textures/TITAN21M.png", "textures/titan21m.png"),
            ("same.png", "same.png"),
            ("dir/image.png", "DIR/IMAGE.PNG"),
            ("texture", "TEXTURE/child.png"),
            ("folder/child.png", "FOLDER"),
            ("image.png", "IMAGE.PNG."),
        ]
        for first, second in examples:
            with self.subTest(first=first, second=second), tempfile.TemporaryFile() as temp:
                with zipfile.ZipFile(temp, "w") as archive:
                    archive.writestr(first, b"one")
                    with patch("warnings.warn"):
                        archive.writestr(second, b"two")
                temp.seek(0)
                with zipfile.ZipFile(temp) as archive, self.assertRaisesRegex(ValueError, "collision"):
                    release.validate_windows_zip(archive)

    def test_windows_zip_allows_shared_directories(self):
        with tempfile.TemporaryFile() as temp:
            with zipfile.ZipFile(temp, "w") as archive:
                archive.writestr("textures/", b"")
                archive.writestr("textures/a.png", b"a")
                archive.writestr("textures/b.png", b"b")
            temp.seek(0)
            with zipfile.ZipFile(temp) as archive:
                release.validate_windows_zip(archive)

    def test_release_pagination_uses_supported_gh_api(self):
        github = release.GitHub("gh")
        first = [{"tag_name": f"v0.{number}"} for number in range(100)]
        second = [{"tag_name": "v0.100", "draft": True}]
        with patch.object(github, "call", side_effect=[json.dumps(first), json.dumps(second)]) as call:
            self.assertEqual(list(github.releases()), first + second)
        self.assertEqual(call.call_args_list[0].args, ("api", f"repos/{release.REPO}/releases?per_page=100&page=1"))
        self.assertEqual(call.call_args_list[1].args, ("api", f"repos/{release.REPO}/releases?per_page=100&page=2"))

    def test_release_pagination_rejects_invalid_response(self):
        github = release.GitHub("gh")
        with patch.object(github, "call", return_value='{"error": "failed"}'):
            with self.assertRaisesRegex(ValueError, "Unexpected GitHub releases response"):
                list(github.releases())

    def test_pins_and_checks_previous_scenery(self):
        fake = FakeGitHub()
        with tempfile.TemporaryDirectory() as temp:
            result = release.previous_catalogue(fake, fake.latest, Path(temp))
        self.assertEqual(result["packs"][0]["url"], release.BASE + "download/v0.51/earth.tar.gz")
        fake.pack["sha256"] = "0" * 64
        fake.contents["v0.51"]["packs.json"] = json.dumps({"packs": [fake.pack]}).encode()
        with tempfile.TemporaryDirectory() as temp, self.assertRaisesRegex(ValueError, "checksum"):
            release.previous_catalogue(fake, fake.latest, Path(temp))

    def test_reuses_unchanged_and_uploads_changed(self):
        previous = {"packs": [pack(url=release.BASE + "download/v0.51/earth.tar.gz")]}
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            (directory / "earth.tar.gz").write_bytes(b"scenery")
            (directory / "packs.json").write_text(json.dumps({"packs": [pack()]}))
            result, uploads = release.prepare_catalogue(previous, directory, "v0.52")
            self.assertEqual(uploads, [])
            self.assertEqual(result["packs"][0]["url"], previous["packs"][0]["url"])
            (directory / "earth.tar.gz").write_bytes(b"new")
            (directory / "packs.json").write_text(json.dumps({"packs": [pack(b"new")]}))
            result, uploads = release.prepare_catalogue(previous, directory, "v0.52")
            self.assertEqual(uploads, [directory / "earth.tar.gz"])
            self.assertEqual(result["packs"][0]["url"], release.BASE + "download/v0.52/earth.tar.gz")
            (directory / "earth.tar.gz").write_bytes(b"bad")
            with self.assertRaisesRegex(ValueError, "does not match"):
                release.prepare_catalogue(previous, directory, "v0.52")

    def test_rejects_untrusted_catalogue(self):
        for url in ("https://example.com/earth.tar.gz", release.BASE + "download/v0.51/../earth.tar.gz"):
            with self.assertRaises(ValueError):
                release.pinned_tag(url, "earth.tar.gz", "v0.51")
        bad = pack()
        bad["marker"] = "../outside"
        with self.assertRaises(ValueError):
            release.validate_catalogue({"packs": [bad]})

    def run_publish(self, corrupt=False):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "VERSION").write_text("0.52\n")
            (root / "SIMLIB_COMMIT").write_text("b" * 40 + "\n")
            simlib = root / "simlib"
            simlib.mkdir()
            notes = root / "docs/release-notes"
            notes.mkdir(parents=True)
            (notes / "0.52.md").write_text("# Test release\n")
            out = root / "build/package"
            out.mkdir(parents=True)
            win = root / "build-win/package"
            win.mkdir(parents=True)
            linux_files = {"kosmos": b"linux", "kosmos-launcher": b"launcher", "VERSION.txt": b"0.52\n"}
            with tarfile.open(out / "kosmos-beta-0.52-Linux.tar.gz", "w:gz") as archive:
                for name, data in linux_files.items():
                    entry = tarfile.TarInfo("kosmos-beta-0.52-Linux/" + name)
                    entry.size = len(data)
                    archive.addfile(entry, io.BytesIO(data))
            with zipfile.ZipFile(win / "kosmos-beta-0.52-Windows.zip", "w") as archive:
                for name, data in {"kosmos.exe": b"exe", "kosmos-launcher.exe": b"launcher",
                                   "VERSION.txt": b"0.52\n"}.items():
                    archive.writestr("kosmos-beta-0.52-Windows/" + name, data)
            docs = root / "sdk/docs"
            docs.mkdir(parents=True)
            for name in ("PROTOCOL.md", "MANUAL.md"):
                (docs / name).write_bytes(name.encode())
            sdk_files = {f"kosmos-sdk/docs/{name}": name.encode() for name in ("PROTOCOL.md", "MANUAL.md")}
            for system in ("linux-x86_64", "windows-x86_64.exe"):
                sdk_files["kosmos-sdk/planetbake/bin/planetbake-" + system] = b"binary"
            with tarfile.open(out / "kosmos-sdk.tar.gz", "w:gz") as archive:
                for name, data in sdk_files.items():
                    entry = tarfile.TarInfo(name)
                    entry.size = len(data)
                    archive.addfile(entry, io.BytesIO(data))
            with zipfile.ZipFile(out / "kosmos-sdk.zip", "w") as archive:
                for name, data in sdk_files.items():
                    archive.writestr(name, data)
            fake = FakeGitHub()
            fake.corrupt = corrupt
            commands = []

            def run(args, **kwargs):
                commands.append(args)
                if args[:2] == ["git", "rev-parse"]:
                    return "a" * 40
                if args[:2] == ["git", "-C"] and "rev-parse" in args:
                    return "b" * 40
                return ""

            args = argparse.Namespace(gh="gh", packs_dir=None)
            with patch.object(release, "ROOT", root), patch.object(release, "run", run), \
                    patch.object(release, "GitHub", return_value=fake), \
                    patch.dict("os.environ", {"KOSMOS_SIMLIB_DIR": str(simlib)}):
                if corrupt:
                    with self.assertRaisesRegex(ValueError, "verification"):
                        release.publish(args)
                    self.assertEqual((root / "VERSION").read_text(), "0.52\n")
                    self.assertFalse(any(c[:2] == ("release", "edit") for c in fake.commands))
                else:
                    release.publish(args)
                    self.assertEqual((root / "VERSION").read_text(), "0.53\n")
                    catalogue = json.loads(fake.contents["v0.52"]["packs.json"])
                    self.assertEqual(catalogue["packs"][0]["url"], release.BASE + "download/v0.51/earth.tar.gz")
                    self.assertNotIn("earth.tar.gz", fake.contents["v0.52"])
                    self.assertTrue(any(c[:2] == ("release", "edit") for c in fake.commands))
                    self.assertTrue(any("run-tests.sh" in str(c) for c in commands))

    def test_full_publish_reuses_packs(self):
        self.run_publish()

    def test_verification_failure_keeps_draft(self):
        self.run_publish(corrupt=True)


if __name__ == "__main__":
    unittest.main()
