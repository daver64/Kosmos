#!/usr/bin/env python3
"""Exercise the release/website workflow without publishing or using a server."""

import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("turnkey", ROOT / "scripts/turnkey.py")
turnkey = importlib.util.module_from_spec(spec)
spec.loader.exec_module(turnkey)


class TurnkeyTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.web = self.root / "web"
        (self.web / "kosmos").mkdir(parents=True)
        (self.web / "index.html").write_text('<span class="ver" data-ver="kosmos">0.52</span>')
        self.download = self.web / "kosmos/download.html"
        self.download.write_text(
            '<span class="ver" data-ver="kosmos">0.52</span>\n'
            'https://github.com/daver64/Kosmos/releases/latest/download/kosmos-beta-0.52-Linux.tar.gz\n'
            'https://github.com/daver64/Kosmos/releases/latest/download/kosmos-beta-0.52-Windows.zip\n'
            '>kosmos-sdk.zip</a></td><td>12 MB\n>kosmos-sdk.tar.gz</a></td><td>12 MB\n'
        )
        self.notes_page = self.web / "kosmos/release-notes.html"
        self.notes_page.write_text(
            '<h1>Release notes</h1>\n<h1>Kosmos 0.52</h1>\n'
            '<p>kosmos-beta-0.52-Linux.tar.gz</p>\n</main>'
        )
        self.notes = "# Kosmos 0.53\n\n## Changes\n\n- Fix & improve.\n\n```sh\necho test\n```\n"
        self.sizes = {"kosmos-sdk.zip": 2048, "kosmos-sdk.tar.gz": 2097152}

    def prepare(self):
        return turnkey.prepare_site(self.web, "0.53", self.notes, self.sizes)

    def test_prepares_site_preserving_historical_notes_and_renders_markdown(self):
        pages = self.prepare()
        download = pages[self.download][1].decode()
        self.assertIn("kosmos-beta-0.53-Linux.tar.gz", download)
        self.assertIn(">kosmos-sdk.zip</a></td><td>2 KB", download)
        notes = pages[self.notes_page][1].decode()
        self.assertIn("<h1>Kosmos 0.53</h1>", notes)
        self.assertIn("Fix &amp; improve.", notes)
        self.assertIn("<pre><code", notes)
        self.assertIn("kosmos-beta-0.52-Linux.tar.gz", notes)
        self.assertLess(notes.index("Kosmos 0.53"), notes.index("Kosmos 0.52"))
        for path, (_, updated) in pages.items():
            path.write_bytes(updated)
        self.assertTrue(all(original == updated for original, updated in self.prepare().values()))

    def test_preflight_rejects_bad_templates_or_notes(self):
        with self.assertRaisesRegex(ValueError, "heading"):
            turnkey.prepare_site(self.web, "0.53", "# Wrong\n")
        self.download.write_text("missing")
        with self.assertRaisesRegex(ValueError, "latest-release link"):
            self.prepare()

    def test_remote_divergence_and_resume(self):
        pages = self.prepare()
        original = [turnkey.digest(value[0]) for value in pages.values()]
        updated = [turnkey.digest(value[1]) for value in pages.values()]
        with patch.object(turnkey, "remote_hashes", return_value=updated):
            with self.assertRaisesRegex(ValueError, "differs"):
                turnkey.check_remote("host", pages)
            turnkey.check_remote("host", pages, allow_updated=True)
        original[0] = "0" * 64
        with patch.object(turnkey, "remote_hashes", return_value=original):
            with self.assertRaisesRegex(ValueError, "differs"):
                turnkey.check_remote("host", pages, allow_updated=True)

    def test_deploy_supports_partial_remote_and_checks_local_changes(self):
        pages = self.prepare()
        remote = {p: original for p, (original, _) in pages.items()}
        remote[next(iter(remote))] = next(iter(pages.values()))[1]

        def hashes(host, paths):
            return [turnkey.digest(remote[path]) for path in paths]

        def run(args, **kwargs):
            remote[Path(str(args[-1]).split(":", 1)[1])] = Path(args[-2]).read_bytes()

        with patch.object(turnkey, "remote_hashes", side_effect=hashes), patch.object(turnkey, "run", side_effect=run):
            turnkey.deploy("host", pages)
        for path, (_, updated) in pages.items():
            self.assertEqual(path.read_bytes(), updated)
            self.assertEqual(remote[path], updated)

    def test_resume_checks_release_before_advancing_or_deploying(self):
        (self.root / "docs/release-notes").mkdir(parents=True)
        (self.root / "docs/release-notes/0.53.md").write_text(self.notes)
        (self.root / "VERSION").write_text("0.53\n")
        args = ["turnkey.py", "--resume", "0.53", "--web-root", str(self.web)]
        with patch.object(turnkey, "ROOT", self.root), patch("sys.argv", args), \
                patch.object(turnkey, "verify_release", side_effect=ValueError("not published")), \
                patch.object(turnkey, "deploy") as deploy:
            self.assertEqual(turnkey.main(), 1)
            deploy.assert_not_called()
        self.assertEqual((self.root / "VERSION").read_text(), "0.53\n")

    def test_complete_workflow_and_resume_after_partial_deployment(self):
        (self.root / "docs/release-notes").mkdir(parents=True)
        (self.root / "docs/release-notes/0.53.md").write_text(self.notes)
        (self.root / "VERSION").write_text("0.53\n")
        pages = self.prepare()
        remote = {path: original for path, (original, _) in pages.items()}
        events = []
        fail_once = [True]

        def hashes(host, paths):
            return [turnkey.digest(remote[path]) for path in paths]

        def run(args, **kwargs):
            if "publish-github-release.py" in str(args[1]):
                events.append("publish")
                (self.root / "VERSION").write_text("0.54\n")
            elif args[0] == "rsync":
                if len([event for event in events if event == "copy"]) == 1 and fail_once[0]:
                    fail_once[0] = False
                    raise OSError("network interrupted")
                remote[Path(str(args[-1]).split(":", 1)[1])] = Path(args[-2]).read_bytes()
                events.append("copy")

        def open_page(url, **kwargs):
            relative = url.removeprefix("https://example.test/")
            return io.BytesIO(remote[self.web / relative])

        common = ["turnkey.py", "--web-root", str(self.web), "--site-url", "https://example.test/"]
        with patch.object(turnkey, "ROOT", self.root), \
                patch.object(turnkey, "remote_hashes", side_effect=hashes), \
                patch.object(turnkey, "run", side_effect=run), \
                patch.object(turnkey, "urlopen", side_effect=open_page), \
                patch.object(turnkey, "verify_release", return_value=self.sizes) as verify:
            with patch("sys.argv", common):
                self.assertEqual(turnkey.main(), 1)
            self.assertEqual((self.root / "VERSION").read_text(), "0.54\n")
            self.assertEqual(events.count("publish"), 1)
            with patch("sys.argv", common + ["--resume", "0.53"]):
                self.assertEqual(turnkey.main(), 0)
            verify.assert_called_with("gh", "0.53", self.notes, full=True)
        self.assertEqual(events.count("publish"), 1)
        for path, (_, updated) in pages.items():
            self.assertEqual(remote[path], updated)
            self.assertEqual(path.read_bytes(), updated)

    def test_public_page_verification_detects_mismatch(self):
        pages = self.prepare()
        with patch.object(turnkey, "urlopen", return_value=io.BytesIO(b"wrong")):
            with self.assertRaisesRegex(ValueError, "Public page"):
                turnkey.verify_public("https://example.test/", self.web, pages)
        with self.assertRaisesRegex(ValueError, "Site URL"):
            turnkey.verify_public("not-a-url", self.web, pages)

    def test_full_release_verification_rejects_corrupted_assets(self):
        content = {
            "kosmos-beta-0.53-Linux.tar.gz": b"linux",
            "kosmos-beta-0.53-Windows.zip": b"windows",
            "kosmos-sdk.tar.gz": b"sdk-tar",
            "kosmos-sdk.zip": b"sdk-zip",
            "VERSION.txt": b"0.53\n",
            "RELEASE_NOTES.md": self.notes.encode(),
            "packs.json": b"{}",
        }
        content["SHA256SUMS.txt"] = "".join(
            f"{turnkey.digest(data)}  {name}\n"
            for name, data in content.items() if name.endswith((".zip", ".tar.gz"))
        ).encode()
        release = {"tag_name": "v0.53", "draft": False, "prerelease": False,
                   "assets": [{"name": name, "size": len(data)} for name, data in content.items()]}

        def run(args, **kwargs):
            if args[0] == "git":
                return ""
            if args[1] == "api":
                if args[2].endswith("/latest"):
                    return json.dumps(release)
                return json.dumps({"object": {"type": "commit", "sha": "a" * 40}})
            name = args[args.index("--pattern") + 1]
            data = content[name]
            if name == "kosmos-sdk.zip":
                data = b"bad-zip"
            (Path(args[args.index("--dir") + 1]) / name).write_bytes(data)
            return ""

        with patch.object(turnkey, "run", side_effect=run):
            with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                turnkey.verify_release("gh", "0.53", self.notes, full=True)


if __name__ == "__main__":
    unittest.main()
