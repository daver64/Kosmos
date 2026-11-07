#!/usr/bin/env python3
import io
import json
import subprocess
import sys
import tarfile
import tempfile
import zipfile
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "scripts/audit-package-files.py"


def run():
    with tempfile.TemporaryDirectory(prefix="kosmos-package-audit-test-") as directory:
        root = Path(directory)
        repo = root / "repo"
        repo.mkdir()
        subprocess.run(["git", "init", "-q", str(repo)], check=True)
        (repo / "data").mkdir()
        (repo / "data/tracked.txt").write_text("tracked")
        subprocess.run(["git", "-C", str(repo), "add", "data/tracked.txt"], check=True)
        files = {
            "app/data/tracked.txt": b"tracked",
            "app/addons/ship/ignored.png": b"local",
            "app/addons/tool/bin/tool": b"generated",
            "app/README.md": b"readme",
        }
        for kind in ("zip", "tar.gz"):
            archive = root / f"app.{kind}"
            if kind == "zip":
                with zipfile.ZipFile(archive, "w") as out:
                    for name, data in files.items():
                        out.writestr(name, data)
            else:
                with tarfile.open(archive, "w:gz") as out:
                    for name, data in files.items():
                        info = tarfile.TarInfo(name)
                        info.size = len(data)
                        out.addfile(info, io.BytesIO(data))
            result = subprocess.run(
                [sys.executable, str(SCRIPT), str(archive), "--repo", str(repo)],
                check=True, capture_output=True, text=True,
            )
            report = json.loads(result.stdout)
            assert report["archive_root"] == "app"
            assert report["asset_files"] == 3
            assert report["not_in_git_index"] == [
                "addons/ship/ignored.png", "addons/tool/bin/tool",
            ]
        bad = root / "bad.zip"
        with zipfile.ZipFile(bad, "w") as out:
            out.writestr("app/data/../../outside", "bad")
        result = subprocess.run(
            [sys.executable, str(SCRIPT), str(bad), "--repo", str(repo)],
            capture_output=True, text=True,
        )
        assert result.returncode != 0 and "unsafe archive member" in result.stderr
        with zipfile.ZipFile(bad, "w") as out:
            out.writestr("app/data/tracked.txt", "a")
            out.writestr("other/data/tracked.txt", "b")
        result = subprocess.run(
            [sys.executable, str(SCRIPT), str(bad), "--repo", str(repo)],
            capture_output=True, text=True,
        )
        assert result.returncode != 0 and "single top-level" in result.stderr
    print("package inventory tests passed")


if __name__ == "__main__":
    run()
