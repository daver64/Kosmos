#!/usr/bin/env python3
import hashlib
import io
import json
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts/stage-github-release.py"


def run():
    with tempfile.TemporaryDirectory(prefix="kosmos-release-test-") as directory:
        root = Path(directory)
        inputs = {
            "kosmos-beta-1.42-Linux.tar.gz": b"linux",
            "kosmos-beta-1.42-Windows.zip": b"windows",
            "kosmos-sdk.tar.gz": b"linux sdk",
            "kosmos-sdk.zip": b"windows sdk",
            "hires.tar.gz": bytes(range(37)),
            "1.42.md": b"notes",
        }
        for name, content in inputs.items():
            (root / name).write_bytes(content)
        with tarfile.open(root / "hires.tar.gz", "w:gz") as archive:
            item = tarfile.TarInfo("data/textures-hires")
            item.type = tarfile.DIRTYPE
            archive.addfile(item)
            for name, data in (("data/textures-hires/earth.dds", bytes(range(37))),
                               ("data/CREDITS-textures.md", b"fixture credits")):
                item = tarfile.TarInfo(name)
                item.size = len(data)
                archive.addfile(item, io.BytesIO(data))
        for name, member in (
            ("kosmos-beta-1.42-Linux.tar.gz", "kosmos-beta-1.42-Linux/VERSION.txt"),
            ("kosmos-sdk.tar.gz", "kosmos-sdk/VERSION"),
        ):
            data = b"1.42\n"
            with tarfile.open(root / name, "w:gz") as archive:
                item = tarfile.TarInfo(member)
                item.size = len(data)
                archive.addfile(item, io.BytesIO(data))
        for name, member in (
            ("kosmos-beta-1.42-Windows.zip", "kosmos-beta-1.42-Windows/VERSION.txt"),
            ("kosmos-sdk.zip", "kosmos-sdk/VERSION"),
        ):
            with zipfile.ZipFile(root / name, "w") as archive:
                archive.writestr(member, "1.42\n")
        asset_names = [name for name in inputs if name != "1.42.md"]
        audit = {"assets": {
            name: {"sha256": hashlib.sha256((root / name).read_bytes()).hexdigest(),
                   "approved": True, "evidence": "synthetic fixture"}
            for name in asset_names
        }}
        (root / "audit.json").write_text(json.dumps(audit))
        out = root / "stage"
        args = [sys.executable, str(SCRIPT), "--version", "1.42",
                "--linux", str(root / "kosmos-beta-1.42-Linux.tar.gz"),
                "--windows", str(root / "kosmos-beta-1.42-Windows.zip"),
                "--sdk-linux", str(root / "kosmos-sdk.tar.gz"),
                "--sdk-windows", str(root / "kosmos-sdk.zip"),
                "--pack", str(root / "hires.tar.gz"),
                "--notes", str(root / "1.42.md"),
                "--rights-audit", str(root / "audit.json"),
                "--part-size", "16", "--out", str(out)]
        subprocess.run(args, check=True, capture_output=True)
        manifest = json.loads((out / "manifest.json").read_text())
        pack = manifest["packs"][0]
        assert len(pack["parts"]) > 1
        assert all(p["size"] == 16 for p in pack["parts"][:-1])
        assert 0 < pack["parts"][-1]["size"] <= 16
        joined = b"".join((out / p["file"]).read_bytes() for p in pack["parts"])
        assert joined == (root / "hires.tar.gz").read_bytes()
        assert hashlib.sha256(joined).hexdigest() == pack["sha256"]
        for part in pack["parts"]:
            assert hashlib.sha256((out / part["file"]).read_bytes()).hexdigest() == part["sha256"]
        for line in (out / "SHA256SUMS.txt").read_text().splitlines():
            expected, name = line.split("  ", 1)
            assert hashlib.sha256((out / name).read_bytes()).hexdigest() == expected
        wrapper_root = root / "wrapper"
        for directory in ("scripts", "build/package", "build-win/package", "docs/release-notes"):
            (wrapper_root / directory).mkdir(parents=True)
        for name in ("release.sh", "stage-github-release.py"):
            shutil.copyfile(ROOT / "scripts" / name, wrapper_root / "scripts" / name)
        (wrapper_root / "VERSION").write_text("1.42\n")
        for name in ("kosmos-beta-1.42-Linux.tar.gz", "kosmos-sdk.tar.gz", "kosmos-sdk.zip"):
            shutil.copyfile(root / name, wrapper_root / "build/package" / name)
        shutil.copyfile(root / "kosmos-beta-1.42-Windows.zip",
                        wrapper_root / "build-win/package/kosmos-beta-1.42-Windows.zip")
        shutil.copyfile(root / "1.42.md", wrapper_root / "docs/release-notes/1.42.md")
        wrapper_out = root / "wrapper-stage"
        subprocess.run(["sh", str(wrapper_root / "scripts/release.sh"), "--stage-only", str(wrapper_out),
                        "--pack", str(root / "hires.tar.gz"), "--rights-audit", str(root / "audit.json")],
                       check=True, capture_output=True)
        assert json.loads((wrapper_out / "manifest.json").read_text())["version"] == "1.42"
        assert (wrapper_root / "VERSION").read_text() == "1.42\n"
        for name in ("build-linux.sh", "build-windows.sh"):
            stub = wrapper_root / "scripts" / name
            stub.write_text('#!/bin/sh\nprintf "%s\\n" "$@" > "$0.args"\n')
            stub.chmod(0o755)
        subprocess.run(["sh", str(wrapper_root / "scripts/release.sh"), "-j", "1"],
                       check=True, capture_output=True)
        for name in ("build-linux.sh", "build-windows.sh"):
            assert (wrapper_root / "scripts" / (name + ".args")).read_text() == "kosmos_package\n"
        assert (wrapper_root / "VERSION").read_text() == "1.42\n"
        blocked = root / "blocked.tar.gz"
        with tarfile.open(blocked, "w:gz") as archive:
            item = tarfile.TarInfo("data/textures-hires/ida.dds")
            item.size = 3
            archive.addfile(item, io.BytesIO(b"ida"))
        blocked_audit = json.loads((root / "audit.json").read_text())
        blocked_audit["assets"][blocked.name] = {
            "sha256": hashlib.sha256(blocked.read_bytes()).hexdigest(),
            "approved": True, "evidence": "synthetic fixture"
        }
        (root / "blocked-audit.json").write_text(json.dumps(blocked_audit))
        bad_pack = args.copy()
        bad_pack[bad_pack.index("--pack") + 1] = str(blocked)
        bad_pack[bad_pack.index("--rights-audit") + 1] = str(root / "blocked-audit.json")
        bad_pack[bad_pack.index("--out") + 1] = str(root / "blocked-out")
        failure = subprocess.run(bad_pack, capture_output=True, text=True)
        assert failure.returncode != 0 and "not approved for redistribution" in failure.stderr
        assert not (root / "blocked-out").exists()
        assert subprocess.run(args, capture_output=True).returncode != 0
        bad = args.copy()
        bad[bad.index("--out") + 1] = str(root / "bad")
        bad[bad.index("--part-size") + 1] = str(2 * 1024 ** 3)
        assert subprocess.run(bad, capture_output=True).returncode != 0
        bad = args.copy()
        bad[bad.index("--out") + 1] = str(root / "bad-version")
        with zipfile.ZipFile(root / "kosmos-sdk.zip", "w") as archive:
            archive.writestr("kosmos-sdk/VERSION", "1.41\n")
        assert subprocess.run(bad, capture_output=True).returncode != 0
        assert not (root / "bad-version").exists()
        with zipfile.ZipFile(root / "kosmos-sdk.zip", "w") as archive:
            archive.writestr("kosmos-sdk/VERSION", "1.42\n")
            archive.writestr("changed", "different content")
        bad[bad.index("--out") + 1] = str(root / "bad-rights")
        assert subprocess.run(bad, capture_output=True).returncode != 0
        assert not (root / "bad-rights" / "manifest.json").exists()
        fixture = root / "fixture"
        (fixture / "scripts").mkdir(parents=True)
        shutil.copyfile(ROOT / "scripts/pack-hires.sh", fixture / "scripts/pack-hires.sh")
        textures = fixture / "data/textures-hires"
        (textures / "bodies").mkdir(parents=True)
        (textures / "procedural").mkdir()
        (fixture / "data/CREDITS-textures.md").write_text("credits")
        for asteroid in ("gaspra", "ida", "kleopatra", "toutatis"):
            (textures / f"{asteroid}.dds").write_text("not for redistribution")
            (textures / "bodies" / f"{asteroid}.obj").write_text("not for redistribution")
        (textures / "earth.dds").write_text("earth")
        (textures / "procedural/world.dds").write_text("procedural")
        pack_out = root / "fixture-hires.tar.gz"
        subprocess.run(["sh", str(fixture / "scripts/pack-hires.sh"), "--no-procedural", str(pack_out)],
                       check=True, capture_output=True)
        with tarfile.open(pack_out) as archive:
            names = archive.getnames()
            assert "data/textures-hires/earth.dds" in names
            assert "data/CREDITS-textures.md" in names
            assert not any(name.endswith((".obj", "gaspra.dds", "ida.dds", "kleopatra.dds", "toutatis.dds")) for name in names)
            assert not any("procedural" in name for name in names)
    print("stage_github_release_test: passed")


if __name__ == "__main__":
    run()
