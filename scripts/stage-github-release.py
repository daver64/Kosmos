#!/usr/bin/env python3
"""Stage release assets locally; this script never uploads or changes VERSION."""

import argparse
import hashlib
import json
import re
import shutil
import sys
import tarfile
import zipfile
from pathlib import Path, PurePosixPath

GIB = 1024 ** 3
PART_SIZE = 1800 * 1024 ** 2
BUFFER = 1024 * 1024


def digest(path):
    h = hashlib.sha256()
    size = 0
    with path.open("rb") as src:
        while chunk := src.read(BUFFER):
            h.update(chunk)
            size += len(chunk)
    return {"file": path.name, "size": size, "sha256": h.hexdigest()}


def copy_asset(source, output, name):
    if source.name != name:
        raise ValueError(f"expected {name}, got {source.name}")
    if not source.is_file():
        raise ValueError(f"missing input: {source}")
    if source.stat().st_size <= 0 or source.stat().st_size >= 2 * GIB:
        raise ValueError(f"asset must be nonempty and under 2 GiB: {source}")
    dest = output / name
    shutil.copyfile(source, dest)
    return digest(dest)


def check_version(source, member, version):
    if not source.is_file():
        raise ValueError(f"missing input: {source}")
    if source.suffix == ".zip":
        with zipfile.ZipFile(source) as archive:
            entries = archive.infolist()
            check_excluded_assets(source, (item.filename for item in entries))
            matches = [item for item in entries if item.filename == member]
            if len(matches) != 1 or matches[0].file_size > 128:
                raise ValueError(f"missing or duplicate version in {source}")
            data = archive.read(matches[0])
    else:
        with tarfile.open(source, "r:gz") as archive:
            entries = list(archive)
            check_excluded_assets(source, (item.name for item in entries))
            matches = [item for item in entries if item.name == member]
            if len(matches) != 1 or not matches[0].isfile() or matches[0].size > 128:
                raise ValueError(f"missing or duplicate version in {source}")
            entry = archive.extractfile(matches[0])
            if entry is None:
                raise ValueError(f"cannot read version in {source}")
            data = entry.read()
    if data.decode("ascii").strip() != version:
        raise ValueError(f"version mismatch in {source}")


def check_excluded_assets(source, names):
    excluded = ("addons/cvel-ariane5", "scenarios/CVEL/CVEL-Ariane", "data/textures/planets",
                "addons/cvel-saturn-v", "addons/cvel-saturn-mlv3", "addons/cvel-saturn-v23",
                "scenarios/CVEL/CVEL-SaturnV", "scenarios/Ares_To_Mars.json",
                "data/textures/skybox", "data/reference",
                *(f"addons/cvel-deltaglider/Skins/Shuttle_{country}"
                  for country in ("BRI", "CAN", "FRA", "USA")))
    for name in names:
        parts = PurePosixPath(name).parts
        if len(parts) < 2:
            continue
        relative = "/".join(parts[1:])
        if (any(relative == item or relative.startswith(item + "/") for item in excluded)
                or re.fullmatch(r"data/textures/[^/]+_normal\.jpg", relative)):
            raise ValueError(f"archive contains excluded asset: {source}: {name}")


def split_pack(source, output, part_size):
    if not source.is_file() or source.stat().st_size <= 0:
        raise ValueError(f"missing or empty pack: {source}")
    if not 0 < part_size < 2 * GIB:
        raise ValueError("part size must be positive and under 2 GiB")
    total = source.stat().st_size
    count = (total + part_size - 1) // part_size
    if count > 990:
        raise ValueError("too many parts for a GitHub release")
    full_hash = hashlib.sha256()
    parts = []
    with source.open("rb") as src:
        for index in range(1, count + 1):
            path = output / f"kosmos-hires.tar.gz.part{index:03d}"
            h = hashlib.sha256()
            size = 0
            with path.open("xb") as dest:
                while size < part_size:
                    chunk = src.read(min(BUFFER, part_size - size))
                    if not chunk:
                        break
                    dest.write(chunk)
                    size += len(chunk)
                    h.update(chunk)
                    full_hash.update(chunk)
            if size == 0:
                raise ValueError("pack changed while splitting")
            parts.append({"file": path.name, "size": size, "sha256": h.hexdigest()})
        if src.read(1) or sum(p["size"] for p in parts) != total:
            raise ValueError("pack changed while splitting")
    return {
        "id": "hires",
        "name": "Procedural planet textures",
        "description": "Optional fictional procedural maps; not geographically accurate and no surface tiles.",
        "file": "kosmos-hires.tar.gz",
        "marker": "data/textures-generated",
        "size": total,
        "sha256": full_hash.hexdigest(),
        "parts": parts,
    }


def check_pack(source):
    seen = set()
    textures = {
        f"data/textures-generated/{name}.dds"
        for name in ("earth", "moon", "mars", "mercury", "venus", "jupiter", "saturn", "uranus", "neptune")
    }
    required = {"data/textures-generated", "data/textures-generated/CREDITS.md"} | textures
    allowed = required | {"data"}
    with tarfile.open(source, "r|gz") as archive:
        for member in archive:
            path = PurePosixPath(member.name)
            name = path.as_posix()
            if path.is_absolute() or ".." in path.parts or name in seen:
                raise ValueError(f"unsafe or duplicate pack entry: {member.name}")
            if name not in allowed:
                raise ValueError(f"pack contains an asset not approved for redistribution: {name}")
            if (name in ("data", "data/textures-generated") and not member.isdir()) or (
                name not in ("data", "data/textures-generated") and not member.isfile()
            ):
                raise ValueError(f"unsupported pack entry type: {name}")
            seen.add(name)
    if not required.issubset(seen):
        raise ValueError("pack is missing procedural maps or texture credits")


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        epilog=("Rights audit format: {\"assets\": {\"input filename\": "
                "{\"sha256\": \"64 lowercase hex characters\", \"approved\": true, "
                "\"evidence\": \"where the asset-by-asset review is recorded\"}}}. "
                "The declaration binds a human review to the exact input; it does not prove the rights."))
    parser.add_argument("--version", required=True)
    parser.add_argument("--linux", required=True, type=Path)
    parser.add_argument("--windows", required=True, type=Path)
    parser.add_argument("--pack", required=True, type=Path)
    parser.add_argument("--notes", required=True, type=Path)
    parser.add_argument("--sdk-linux", required=True, type=Path)
    parser.add_argument("--sdk-windows", required=True, type=Path)
    parser.add_argument("--rights-audit", required=True, type=Path,
                        help="JSON file recording a human rights review for each exact input hash")
    parser.add_argument("--out", required=True, type=Path,
                        help="new directory for staged assets")
    parser.add_argument("--part-size", type=int, default=PART_SIZE,
                        help="maximum bytes per pack part (default: 1800 MiB)")
    args = parser.parse_args()
    if not re.fullmatch(r"(?:0|[1-9][0-9]{0,8})\.(?:0|[1-9][0-9]{0,8})", args.version):
        parser.error("version must be major.build, with at most nine digits per component and no leading zeros")
    try:
        if args.notes.name != args.version + ".md" or not args.notes.is_file():
            raise ValueError("release notes must be named VERSION.md and exist")
        if not 0 < args.notes.stat().st_size <= 10 * 1024 ** 2:
            raise ValueError("release notes must be nonempty and at most 10 MiB")
        if not 0 < args.part_size < 2 * GIB:
            raise ValueError("part size must be positive and under 2 GiB")
        for source, member in (
            (args.linux, f"kosmos-beta-{args.version}-Linux/VERSION.txt"),
            (args.windows, f"kosmos-beta-{args.version}-Windows/VERSION.txt"),
            (args.sdk_linux, "kosmos-sdk/VERSION"),
            (args.sdk_windows, "kosmos-sdk/VERSION"),
        ):
            check_version(source, member, args.version)
        audit = json.loads(args.rights_audit.read_text(encoding="utf-8"))
        if not isinstance(audit, dict) or not isinstance(audit.get("assets"), dict):
            raise ValueError("rights audit must contain an assets object")
        input_names = [p.name for p in (args.linux, args.windows, args.sdk_linux, args.sdk_windows, args.pack)]
        if len(set(input_names)) != len(input_names):
            raise ValueError("input asset filenames must be distinct")
        for name in input_names:
            item = audit["assets"].get(name)
            if not isinstance(item, dict) or item.get("approved") is not True or not isinstance(item.get("evidence"), str) or not item["evidence"].strip() or not re.fullmatch(r"[0-9a-f]{64}", str(item.get("sha256", ""))):
                raise ValueError(f"rights audit lacks an approved hash and evidence for {name}")
        check_pack(args.pack)
        # Refuse to overwrite any existing staging directory, even after a failed run.
        args.out.mkdir(parents=True, exist_ok=False)
        assets = {}
        for key, source, name in (
            ("linux", args.linux, f"kosmos-beta-{args.version}-Linux.tar.gz"),
            ("windows", args.windows, f"kosmos-beta-{args.version}-Windows.zip"),
            ("sdk-linux", args.sdk_linux, "kosmos-sdk.tar.gz"),
            ("sdk-windows", args.sdk_windows, "kosmos-sdk.zip"),
        ):
            assets[key] = copy_asset(source, args.out, name)
        notes = args.out / "release-notes.md"
        shutil.copyfile(args.notes, notes)
        assets["notes"] = digest(notes)
        pack = split_pack(args.pack, args.out, args.part_size)
        for name, actual in (
            (args.linux.name, assets["linux"]["sha256"]),
            (args.windows.name, assets["windows"]["sha256"]),
            (args.sdk_linux.name, assets["sdk-linux"]["sha256"]),
            (args.sdk_windows.name, assets["sdk-windows"]["sha256"]),
            (args.pack.name, pack["sha256"]),
        ):
            if audit["assets"][name]["sha256"] != actual:
                raise ValueError(f"rights audit hash does not match {name}")
        manifest = {"schema": 1, "version": args.version, "assets": assets, "packs": [pack]}
        (args.out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        checksums = list(assets.values()) + pack["parts"] + [digest(args.out / "manifest.json")]
        (args.out / "SHA256SUMS.txt").write_text(
            "".join(f'{a["sha256"]}  {a["file"]}\n' for a in checksums), encoding="utf-8")
        print(f"Staged {args.version} in {args.out}; no release was published.")
        print("Audit declarations are not automatic proof of redistribution rights; verify evidence and credits before uploading.", file=sys.stderr)
    except (OSError, ValueError, UnicodeError, tarfile.TarError, zipfile.BadZipFile) as error:
        print(f"stage-github-release: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
