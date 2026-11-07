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
            item = tarfile.TarInfo("data/textures-generated")
            item.type = tarfile.DIRTYPE
            archive.addfile(item)
            files = [(f"data/textures-generated/{name}.dds", bytes(range(37)))
                     for name in ("earth", "moon", "mars", "mercury", "venus",
                                  "jupiter", "saturn", "uranus", "neptune")]
            files.append(("data/textures-generated/CREDITS.md", b"fixture credits"))
            for name, data in files:
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
        alternate_out = root / "alternate-stage"
        subprocess.run(["sh", str(wrapper_root / "scripts/release.sh"), "--stage-only", str(alternate_out),
                        "--pack", str(root / "hires.tar.gz"), "--rights-audit", str(root / "audit.json"),
                        "--sdk-linux", str(root / "kosmos-sdk.tar.gz"),
                        "--sdk-windows", str(root / "kosmos-sdk.zip")], check=True, capture_output=True)
        assert json.loads((alternate_out / "manifest.json").read_text())["version"] == "1.42"
        for name in ("build-linux.sh", "build-windows.sh"):
            stub = wrapper_root / "scripts" / name
            stub.write_text('#!/bin/sh\nprintf "%s\\n" "$@" > "$0.args"\n')
            stub.chmod(0o755)
        subprocess.run(["sh", str(wrapper_root / "scripts/release.sh"), "-j", "1"],
                       check=True, capture_output=True)
        for name in ("build-linux.sh", "build-windows.sh"):
            assert (wrapper_root / "scripts" / (name + ".args")).read_text() == "kosmos_package\n"
        assert (wrapper_root / "VERSION").read_text() == "1.42\n"
        for option, name, prefix in (
            ("--linux", "kosmos-beta-1.42-Linux.tar.gz", "kosmos-beta-1.42-Linux"),
            ("--windows", "kosmos-beta-1.42-Windows.zip", "kosmos-beta-1.42-Windows"),
        ):
            for index, asset in enumerate(("addons/cvel-ariane5/models/core.obj",
                                           "scenarios/CVEL/CVEL-Ariane/ariane5_eca_kourou.json",
                                           "addons/cvel-saturn-v/models/sat5stg1.obj",
                                           "addons/cvel-saturn-mlv3/ship.json",
                                           "addons/cvel-saturn-v23/ship.json",
                                           "scenarios/CVEL/CVEL-SaturnV/apollo11_saturn_v_t_minus_5.json",
                                           "scenarios/Ares_To_Mars.json",
                                           "data/textures/planets/earth.jpg",
                                           "data/textures/skybox/milkyway_procedural_4k.jpg",
                                           "data/textures/ganymede_normal.jpg",
                                           "data/reference/orbiter_screenshots/3091a.jpg",
                                           *(f"data/textures/{body}.jpg" for body in
                                             ("amalthea", "ariel", "charon", "earth_lights", "hyperion",
                                              "mercury", "miranda", "moon", "neptune", "oberon", "phoebe",
                                              "pluto", "proteus", "saturn", "titan", "titania", "triton",
                                              "umbriel", "uranus", "venus")),
                                           "data/textures/hydra.dds", "data/textures/nix.dds",
                                           "data/parts/apollo-soyuz/apollo-soyuz.glb",
                                           "data/parts/orbiter/klc39b.obj",
                                           "data/parts/orbiter/textures/036MB.png",
                                           "data/parts/orbiter/textures/Station2.png",
                                           "addons/cvel-titan/models/titan21.obj",
                                           "addons/cvel-delta-iv/models/deltaivstg1.obj",
                                           "addons/cvel-atlas/models/atlasiii_stg1.obj",
                                           "addons/cvel-energia-m/models/energiamcore.obj",
                                           "addons/cvel-energia-m/thumbs/addon.png",
                                           "scenarios/CVEL/CVEL-Energia/thumbs/energia_m_baikonur.png",
                                           "addons/cvel-proton/models/protonstg1.obj",
                                           "addons/cvel-proton/thumbs/addon.png",
                                           "scenarios/CVEL/CVEL-Proton/thumbs/proton_k_baikonur.png",
                                           "addons/cvel-buran/models/buran.obj",
                                           "addons/cvel-buran/thumbs/addon.png",
                                           "scenarios/CVEL/CVEL-Buran/thumbs/buran_energia_baikonur.png",
                                           "addons/cvel-centaur/models/cvel-centaurg.obj",
                                           "addons/cvel-centaur/thumbs/addon.png",
                                           "addons/cvel-transtage/models/transtage.obj",
                                           "addons/cvel-transtage/thumbs/addon.png",
                                           "scenarios/CVEL/CVEL-Atlas/thumbs/centaur_g_orbit.png",
                                           "scenarios/CVEL/CVEL-Atlas/thumbs/transtage_orbit.png",
                                           "addons/cvel-ares/models/aresspider1config3.obj",
                                           "addons/cvel-ares/thumbs/addon.png",
                                           "scenarios/CVEL/CVEL-Ares/thumbs/ares_spider_gale_crater.png",
                                           "scenarios/CVEL/CVEL-Bases/thumbs/ares_spider_gale_crater.png",
                                           "addons/salyut/models/salyut.obj", "addons/salyut/thumbs/addon.png",
                                           "addons/probe/models/probe.obj", "addons/probe/thumbs/addon.png",
                                           "addons/orbiter-stations/models/iss.obj",
                                           "addons/orbiter-stations/models/iss.mtl",
                                           "addons/orbiter-stations/models/mir.obj",
                                           "addons/orbiter-stations/models/mir.mtl",
                                           "addons/orbiter-stations/models/wheel.obj",
                                           "addons/orbiter-stations/models/wheel.mtl",
                                           "addons/orbiter-stations/models/textures/Station2.png",
                                           "addons/orbiter-stations/models/textures/036MB.png",
                                           "addons/orbiter-stations/models/textures/iss_dc.png",
                                           "addons/orbiter-stations/models/textures/d_ring.png",
                                           "scenarios/CVEL/CVEL-Bases/thumbs/orbital_stations.png",
                                           "docs/manual/img/lunar_station.jpg",
                                           "addons/cvel-dragonfly/models/dragonfly.obj",
                                           "addons/cvel-dragonfly/panel/panel1.png",
                                           "addons/cvel-dragonfly/thumbs/addon.png",
                                           "scenarios/CVEL/CVEL-Dragonfly/thumbs/dragonfly_orbit.png",
                                           "addons/cvel-atlas/thumbs/addon.png",
                                           "scenarios/CVEL/CVEL-Atlas/thumbs/atlas_iii_kennedy.png",
                                           "addons/cvel-deltaglider/models/textures/original/DGIP_01.png",
                                           "addons/cvel-deltaglider/models/deltaglider.obj.bak",
                                           "addons/cvel-deltaglider/models/deltaglider.obj",
                                           "addons/cvel-deltaglider/panel/panel1.png",
                                           "addons/cvel-deltaglider/thumbs/addon.png",
                                           "scenarios/CVEL/CVEL-Deltaglider/thumbs/deltaglider_chippenham.png",
                                           "scenarios/CVEL/CVEL-Deltaglider/thumbs/nuclear_deltaglider_gale.png",
                                           "docs/manual/img/weapons.jpg",
                                           "addons/cvel-shuttle-a/models/shuttlea.obj",
                                           "addons/cvel-shuttle-a/panel/panel1.png",
                                           "addons/cvel-shuttle-a/thumbs/addon.png",
                                           "addons/cvel-shuttle-aq/thumbs/addon.png",
                                           "scenarios/CVEL/CVEL-ShuttleA/thumbs/shuttle_a_cargo_orbit.png",
                                           "scenarios/CVEL/CVEL-Bases/thumbs/shuttle_a_tranquility_miner.png",
                                           "scenarios/CVEL/CVEL-Exsolar/thumbs/shuttle_aq_thalassa_depot.png",
                                           "docs/manual/img/shuttle_a_pods.jpg",
                                           "addons/cvel-delta-iv/thumbs/addon.png",
                                           "scenarios/CVEL/CVEL-Titan/thumbs/titan1_kennedy.png",
                                           "scenarios/CVEL/CVEL-Atlas/thumbs/delta_iv_heavy_kennedy.png",
                                           *(f"addons/cvel-deltaglider/Skins/Shuttle_{country}/idpanel1.dds"
                                             for country in ("BRI", "CAN", "FRA", "USA")))):
                fixture_dir = root / f"excluded-{option[2:]}-{index}"
                fixture_dir.mkdir()
                fixture = fixture_dir / name
                if option == "--linux":
                    with tarfile.open(fixture, "w:gz") as archive:
                        for member, content in ((f"{prefix}/VERSION.txt", b"1.42\n"),
                                                (f"{prefix}/{asset}", b"excluded")):
                            info = tarfile.TarInfo(member)
                            info.size = len(content)
                            archive.addfile(info, io.BytesIO(content))
                else:
                    with zipfile.ZipFile(fixture, "w") as archive:
                        archive.writestr(f"{prefix}/VERSION.txt", "1.42\n")
                        archive.writestr(f"{prefix}/{asset}", "excluded")
                bad = args.copy()
                bad[bad.index(option) + 1] = str(fixture)
                bad[bad.index("--out") + 1] = str(fixture_dir / "stage")
                failure = subprocess.run(bad, capture_output=True, text=True)
                assert failure.returncode != 0 and "excluded asset" in failure.stderr
                assert not (fixture_dir / "stage").exists()
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
        import importlib.util
        spec = importlib.util.spec_from_file_location("stage_release", SCRIPT)
        stage = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(stage)
        try:
            stage.check_pack(pack_out)
        except ValueError as error:
            assert "not approved for redistribution" in str(error)
        else:
            raise AssertionError("legacy mixed pack was accepted")
        generated = root / "generated.tar.gz"
        subprocess.run([sys.executable, str(ROOT / "scripts/pack-procedural-hires.py"),
                        "--scale", "32", str(generated)], check=True, capture_output=True)
        stage.check_pack(generated)
        with tarfile.open(generated) as archive:
            names = archive.getnames()
            assert "data/CREDITS-textures.md" not in names
            assert "data/textures-generated/CREDITS.md" in names
            assert len([name for name in names if name.endswith(".dds")]) == 9
        old = generated.read_bytes()
        assert subprocess.run([sys.executable, str(ROOT / "scripts/pack-procedural-hires.py"),
                               "--scale", "32", str(generated)], capture_output=True).returncode != 0
        assert generated.read_bytes() == old
    print("stage_github_release_test: passed")


if __name__ == "__main__":
    run()
