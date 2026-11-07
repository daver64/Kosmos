#!/usr/bin/env python3
"""List packaged source-tree files absent from the current Git index.

This is an inventory aid, not a licence review or proof of file identity.
Generated addon binaries and other intentional outputs may also be reported.
"""
import argparse
import json
import subprocess
import sys
import tarfile
import zipfile
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[1]
ASSET_ROOTS = {"addons", "assets", "data", "docs", "scenarios"}


def archive_files(path):
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as archive:
            yield from (item.filename for item in archive.infolist() if not item.is_dir())
    else:
        with tarfile.open(path, "r|gz") as archive:
            yield from (item.name for item in archive if not item.isdir())


def inventory(path, repo):
    result = subprocess.run(
        ["git", "-C", str(repo), "ls-files", "-z", "--", *sorted(ASSET_ROOTS)],
        check=True, stdout=subprocess.PIPE,
    )
    tracked = set(result.stdout.decode("utf-8", errors="surrogateescape").split("\0"))
    roots = set()
    extra = set()
    count = 0
    for name in archive_files(path):
        parts = PurePosixPath(name).parts
        if (not parts or name.startswith("/") or "\\" in name
                or any(part in (".", "..") for part in name.split("/"))):
            raise ValueError(f"unsafe archive member: {name!r}")
        roots.add(parts[0])
        if len(roots) != 1:
            raise ValueError("archive must have a single top-level directory")
        if len(parts) < 3 or parts[1] not in ASSET_ROOTS:
            continue
        count += 1
        relative = "/".join(parts[1:])
        if relative not in tracked:
            extra.add(relative)
    if not roots:
        raise ValueError("archive contains no files")
    return {
        "archive": str(path),
        "archive_root": next(iter(roots)),
        "asset_files": count,
        "not_in_git_index": sorted(extra),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path, help="application ZIP or tar.gz")
    parser.add_argument("--repo", type=Path, default=ROOT, help="repository to compare with")
    args = parser.parse_args()
    try:
        report = inventory(args.archive, args.repo)
    except (OSError, ValueError, tarfile.TarError, zipfile.BadZipFile,
            subprocess.CalledProcessError) as error:
        print(f"audit-package-files: {error}", file=sys.stderr)
        return 1
    print(json.dumps(report, indent=2, ensure_ascii=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
