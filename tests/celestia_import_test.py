#!/usr/bin/env python3
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "tools/celestia_import.py"


def run():
    with tempfile.TemporaryDirectory(prefix="kosmos-celestia-test-") as directory:
        root = Path(directory)
        (root / "tools").mkdir()
        (root / "import/celestia/medres").mkdir(parents=True)
        (root / "data/textures").mkdir(parents=True)
        script = root / "tools/celestia_import.py"
        shutil.copyfile(SCRIPT, script)
        source = root / "import/celestia/medres/europa.jpg"
        source.write_bytes(b"fixture")
        (root / "import/celestia/medres/amalthea.jpg").write_bytes(b"another fixture")
        dest = root / "data/textures/europa.jpg"
        listed = subprocess.run([sys.executable, str(script), "--list"],
                                check=True, capture_output=True, text=True)
        assert "europa: medres/europa.jpg -> europa.jpg" in listed.stdout
        assert not dest.exists()
        overrides = root / "data/textures-hires"
        overrides.mkdir()
        dest.write_bytes(b"bundled")
        subprocess.run([sys.executable, str(script), "--out", str(overrides)],
                       check=True, capture_output=True)
        assert dest.read_bytes() == b"bundled"
        assert (overrides / "europa.jpg").read_bytes() == b"fixture"
        blocked = subprocess.run([sys.executable, str(script)], capture_output=True, text=True)
        assert blocked.returncode != 0 and "destination exists" in blocked.stderr
        assert dest.read_bytes() == b"bundled"
        assert not (root / "data/textures/amalthea.jpg").exists()
        subprocess.run([sys.executable, str(script), "--force"], check=True, capture_output=True)
        assert dest.read_bytes() == b"fixture"
        missing = subprocess.run([sys.executable, str(script), "--src", str(root / "missing")],
                                 capture_output=True, text=True)
        assert missing.returncode != 0 and "source directory not found" in missing.stderr
    print("celestia import tests passed")


if __name__ == "__main__":
    run()
