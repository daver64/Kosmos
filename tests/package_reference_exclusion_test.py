#!/usr/bin/env python3
import re
import os
import subprocess
import tarfile
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def run():
    cmake = (ROOT / "CMakeLists.txt").read_text()
    filters = []
    for path in ("data/reference", "data/textures/skybox"):
        expected = f"/{path}(/|$)"
        assert f'"{expected}"' in cmake
        filters.append(expected)
    skin_filter = "/addons/cvel-deltaglider/Skins/Shuttle_(BRI|CAN|FRA|USA)(/|$)"
    assert f'"{skin_filter}"' in cmake
    filters.append(skin_filter)
    normal_filter = r"/data/textures/[^/]+_normal\.jpg$"
    assert '"'+normal_filter.replace("\\", "\\\\")+'"' in cmake
    filters.append(normal_filter)
    with tempfile.TemporaryDirectory(prefix="kosmos-package-reference-") as directory:
        root = Path(directory)
        data = root / "data"
        for name in ("reference/hidden.jpg", "textures/skybox/hidden.jpg",
                     "textures/visible.jpg", "reference-neighbor/visible.jpg",
                     "textures/ganymede_normal.jpg", "textures/interior/wall_normal.png"):
            file = data / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_bytes(b"fixture")
        addons = root / "addons"
        for skin in ("Shuttle_BRI", "Shuttle_CAN", "Shuttle_FRA", "Shuttle_USA", "MySkin"):
            file = addons / "cvel-deltaglider/Skins" / skin / "texture.dds"
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_bytes(b"fixture")
        exe = root / "app"
        exe.write_bytes(b"fixture")
        package_script = Path(os.environ.get(
            "SIMLIB_PACKAGE_SCRIPT", ROOT.parent / "rsimlib/cmake/SimlibPackageRun.cmake"))
        assert package_script.is_file(), f"simlib packaging script not found: {package_script}"
        for fmt in ("zip", "tar.gz"):
            archive = root / f"package.{fmt}"
            subprocess.run(
                ["cmake", f"-DSTAGE={root / 'stage'}", f"-DZIP={archive}",
                 f"-DEXE={exe}", f"-DASSETS={data};{addons}", f"-DEXCLUDE={';'.join(filters)}",
                 f"-DFORMAT={fmt}", "-P", str(package_script)],
                check=True, capture_output=True, text=True,
            )
            if fmt == "zip":
                with zipfile.ZipFile(archive) as package:
                    names = package.namelist()
            else:
                with tarfile.open(archive, "r:gz") as package:
                    names = package.getnames()
            assert "stage/data/textures/visible.jpg" in names
            assert "stage/data/textures/interior/wall_normal.png" in names
            assert "stage/data/textures/ganymede_normal.jpg" not in names
            assert "stage/data/reference-neighbor/visible.jpg" in names
            assert "stage/addons/cvel-deltaglider/Skins/MySkin/texture.dds" in names
            assert not any(re.search(r"/data/(?:reference|textures/skybox)(?:/|$)", name)
                           for name in names)
            assert not any(re.search(r"/Skins/Shuttle_(?:BRI|CAN|FRA|USA)(?:/|$)", name)
                           for name in names)
    print("package reference exclusion tests passed")


if __name__ == "__main__":
    run()
