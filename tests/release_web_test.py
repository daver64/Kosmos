#!/usr/bin/env python3
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
COMMON = ROOT / "scripts/release-web-common.sh"
SYNC = ROOT / "scripts/sync-release-archives.sh"


def common(function, *args):
    subprocess.run(
        ["sh", "-c", '. "$1"; shift; "$@"', "test", str(COMMON), function, *map(str, args)],
        check=True,
    )


class ReleaseWebTest(unittest.TestCase):
    def test_stamp_preserves_historical_release_note_archive_names(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "kosmos").mkdir()
            page = root / "kosmos/download.html"
            notes = root / "kosmos/release-notes.html"
            page.write_text('<span class="ver" data-ver="kosmos">0.50</span> kosmos-beta-0.50-Linux.tar.gz')
            notes.write_text('<span class="ver" data-ver="kosmos">0.50</span> kosmos-beta-0.32-Windows.zip')

            subprocess.run(["sh", str(ROOT / "scripts/publish-web.sh"), "--stamp", "0.51"],
                           env=dict(os.environ, WEB_ROOT=temp), check=True)

            self.assertIn('data-ver="kosmos">0.51', page.read_text())
            self.assertIn("kosmos-beta-0.51-Linux.tar.gz", page.read_text())
            self.assertIn('data-ver="kosmos">0.51', notes.read_text())
            self.assertIn("kosmos-beta-0.32-Windows.zip", notes.read_text())

    def test_old_website_binary_publishers_refuse_to_run(self):
        with tempfile.TemporaryDirectory() as temp:
            env = dict(os.environ, WEB_ROOT=temp)
            for command in (["sh", str(ROOT / "scripts/publish-web.sh"), "archive.tar.gz"],
                            ["sh", str(ROOT / "scripts/build-sdk.sh"), "--publish"],
                            ["sh", str(ROOT / "scripts/make-packs-json.sh")]):
                result = subprocess.run(command, env=env, capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("GitHub", result.stderr)
            self.assertEqual(list(Path(temp).iterdir()), [])

    def test_prunes_old_archives_and_orphan_checksums_per_platform(self):
        with tempfile.TemporaryDirectory() as temp:
            download = Path(temp)
            current = "kosmos-beta-0.49-Linux.tar.gz"
            old = "kosmos-beta-0.48-Linux.tar.gz"
            legacy = "kosmos-beta-Linux.tar.gz"
            windows = "kosmos-beta-0.48-Windows.zip"
            for name in (current, old, windows, "kosmos-sdk.zip"):
                (download / name).write_text(name)
            (download / legacy).symlink_to("missing.tar.gz")
            names = (current, old, legacy, "kosmos-beta-0.47-Linux.tar.gz", windows, "kosmos-sdk.zip")
            (download / "SHA256SUMS.txt").write_text("".join(f"{'a' * 64}  {n}\n" for n in names))

            common("prune_release_archives", download, current)

            self.assertTrue((download / current).is_file())
            self.assertFalse((download / old).exists())
            self.assertFalse(os.path.lexists(download / legacy))
            self.assertTrue((download / windows).is_file())
            remaining = (download / "SHA256SUMS.txt").read_text()
            self.assertEqual([line.split()[1] for line in remaining.splitlines()],
                             [current, windows, "kosmos-sdk.zip"])

    def test_sdk_sizes_are_updated_for_small_and_large_archives(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)
            archive = path / "kosmos-sdk.zip"
            page = path / "download.html"
            page.write_text(">kosmos-sdk.zip</a></td><td>29 KB")
            archive.write_bytes(b"x" * 2048)
            common("update_download_size", page, archive)
            self.assertIn(">2 KB", page.read_text())
            with archive.open("wb") as file:
                file.truncate(12 * 1048576)
            common("update_download_size", page, archive)
            self.assertIn(">12 MB", page.read_text())

    def test_remote_mirror_removes_only_stale_game_archives(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)
            source, destination = path / "source", path / "destination"
            source.mkdir()
            destination.mkdir()
            current = "kosmos-beta-0.49-Linux.tar.gz"
            (source / current).write_text("new")
            (destination / "kosmos-beta-0.48-Linux.tar.gz").write_text("old")
            (destination / "kosmos-beta-Windows.zip").symlink_to("missing.zip")
            (destination / "kosmos-sdk.zip").write_text("sdk")
            (destination / "notes").mkdir()
            (destination / "notes/0.48.md").write_text("notes")

            subprocess.run(["sh", str(SYNC), str(source), str(destination)], check=True)

            self.assertEqual((destination / current).read_text(), "new")
            self.assertFalse((destination / "kosmos-beta-0.48-Linux.tar.gz").exists())
            self.assertFalse(os.path.lexists(destination / "kosmos-beta-Windows.zip"))
            self.assertEqual((destination / "kosmos-sdk.zip").read_text(), "sdk")
            self.assertEqual((destination / "notes/0.48.md").read_text(), "notes")


if __name__ == "__main__":
    unittest.main()
