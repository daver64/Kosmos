#!/usr/bin/env python3
"""Exercise user scenery lookup in the real renderer on a graphical display."""

import os
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
KOSMOS = Path(os.environ.get("KOSMOS", ROOT / "build/kosmos"))


def main():
    if not os.environ.get("DISPLAY"):
        raise SystemExit("user_scenery_test requires DISPLAY")
    with tempfile.TemporaryDirectory(prefix="kosmos-user-scenery-") as directory:
        temp = Path(directory)
        root = temp / "install"
        root.mkdir()
        for name in ("addons", "scenarios"):
            (root / name).symlink_to(ROOT / name, target_is_directory=True)
        (root / "data").mkdir()
        for source in (ROOT / "data").iterdir():
            if source.name not in ("textures-hires", "textures-generated", "reference"):
                (root / "data" / source.name).symlink_to(source, target_is_directory=source.is_dir())
        installed_high = root / "data/textures-hires"
        installed_high.mkdir()
        shutil.copyfile(ROOT / "data/textures/mars.jpg", installed_high / "mars.jpg")
        user = temp / "config/kosmos/scenery"
        high = user / "data/textures-hires"
        high.mkdir(parents=True)
        shutil.copyfile(ROOT / "data/textures/europa.jpg", high / "mars.jpg")
        shutil.copyfile(ROOT / "data/textures/europa.jpg", high / "mars_normal.jpg")
        tree = user / "import/orbiter/Mars/Archive/Elev.tree"
        tree.parent.mkdir(parents=True)
        tree.write_bytes(b"invalid test tree")
        env = os.environ.copy()
        env.update(XDG_CONFIG_HOME=str(temp / "config"), OMP_NUM_THREADS="1",
                   OPENBLAS_NUM_THREADS="1", SDL_AUDIODRIVER="dummy")

        def capture(name, body="Mars", orbital_tiles=False):
            shot = temp / f"{name}.png"
            tile_option = [] if orbital_tiles else ["--no-tiles"]
            result = subprocess.run(
                [str(KOSMOS), "--root", str(root), "--ship", "CVEL Deltaglider",
                 "--orbit", "--orbit-body", body, *tile_option, "--no-clouds",
                 "--gen-threads", "1", "--max-texture", "1024", "--hide-info",
                 "--shot", str(shot), "--shot-at", "1"],
                env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                timeout=120, check=True,
            )
            assert shot.is_file() and shot.stat().st_size > 0, result.stdout
            return result.stdout

        log = capture("override")
        assert str(high / "mars.jpg") in log, log
        assert str(high / "mars_normal.jpg") in log, log
        assert str(tree) in log and "not an Orbiter .tree archive" in log, log
        (high / "mars.jpg").unlink()
        (high / "mars_normal.jpg").unlink()
        tree.unlink()
        log = capture("fallback")
        assert str(installed_high / "mars.jpg") in log, log
        assert str(high / "mars.jpg") not in log, log
        assert str(high / "mars_normal.jpg") not in log, log
        assert str(tree) not in log, log
        (installed_high / "mars.jpg").unlink()
        log = capture("bundled")
        assert str(root / "data/textures/mars.jpg") in log, log
        assert f"surface texture {root / 'data/textures/rhea.jpg'}: 720x360 (file 2880x1440)" in log, log
        for body in ("dione", "tethys", "iapetus"):
            assert f"surface texture {root / 'data/textures' / (body + '.jpg')}: 720x360 (file 1440x720)" in log, log
        assert f"surface texture {root / 'data/textures/mimas.jpg'}: 720x360 (file 5760x2880)" in log, log
        assert f"surface texture {root / 'data/textures/enceladus.jpg'}: 900x450 (file 3600x1800)" in log, log
        assert str(high / "mars.jpg") not in log, log
        assert str(high / "mars_normal.jpg") not in log, log
        assert str(tree) not in log, log
        earth = user / "import/celestia/hires/earth"
        earth.mkdir(parents=True)
        installed_earth = root / "import/celestia/hires/earth"
        installed_earth.mkdir(parents=True)
        installed_ctx = installed_earth / "installed.ctx"
        installed_ctx.write_text('VirtualTexture\n{\n ImageDirectory "tiles"\n'
                                 ' BaseSplit 0\n TileSize 512\n TileType "jpg"\n}\n')
        installed_level = installed_earth / "tiles/level0"
        installed_level.mkdir(parents=True)
        for name in ("tx_0_0.jpg", "tx_1_0.jpg"):
            shutil.copyfile(ROOT / "data/textures/mars.jpg", installed_level / name)
        log = capture("empty-user-tiles")
        assert f"Earth terrain imagery candidate: {installed_ctx}" in log, log
        tiles = earth / "JestrMarble JPG/level0"
        tiles.mkdir(parents=True)
        ctx = earth / "JestrMarble JPG.ctx"
        ctx.write_text('VirtualTexture\n{\n ImageDirectory "JestrMarble JPG"\n'
                       ' BaseSplit 0\n TileSize 512\n TileType "jpg"\n}\n')
        for name in ("tx_0_0.jpg", "tx_1_0.jpg"):
            shutil.copyfile(ROOT / "data/textures/mars.jpg", tiles / name)
        log = capture("celestia")
        assert f"Earth terrain imagery candidate: {ctx}" in log, log
        log = capture("earth-orbital-tiles", body="Earth", orbital_tiles=True)
        assert f"Earth orbital imagery tiles: {ctx}" in log, log
        assert "tiled Earth unavailable" not in log, log
        log = capture("rhea-orbit", body="Rhea")
        assert f"surface texture {root / 'data/textures/rhea.jpg'}: 720x360 (file 2880x1440)" in log, log
    print("user scenery lookup tests passed")


if __name__ == "__main__":
    main()
