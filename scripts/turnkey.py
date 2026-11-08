#!/usr/bin/env python3
"""Publish a Kosmos release and update its website, without committing or pushing."""

import argparse
import hashlib
import html
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile
from urllib.parse import quote, urlsplit
from urllib.request import urlopen

import markdown


ROOT = Path(__file__).resolve().parents[1]
VERSION = re.compile(r"[0-9]+\.[0-9]+")
LABEL = re.compile(r'(?<=class="ver" data-ver="kosmos">)[^<]*')
LATEST_ARCHIVE = re.compile(
    r"(https://github\.com/daver64/Kosmos/releases/latest/download/"
    r"kosmos-beta-)(?:[0-9]+\.[0-9]+-)?(Linux|Windows)(?=\.)"
)


def run(args, *, capture=False):
    return subprocess.run([str(x) for x in args], cwd=ROOT, check=True, text=True,
                          stdout=subprocess.PIPE if capture else None).stdout


def digest(data):
    return hashlib.sha256(data).hexdigest()


def next_version(version):
    if not VERSION.fullmatch(version):
        raise ValueError(f"Invalid version: {version!r}")
    major, minor = version.split(".")
    return f"{major}.{int(minor) + 1}"


def prepare_site(web_root, version, notes, sdk_sizes=None):
    """Return original and updated bytes for the release-related HTML files."""
    next_version(version)
    if not notes.splitlines() or notes.splitlines()[0].strip() != f"# Kosmos {version}":
        raise ValueError(f"Release notes must begin with the heading '# Kosmos {version}'")
    files = [web_root / "index.html", *sorted((web_root / "kosmos").glob("*.html"))]
    required = {web_root / "index.html", web_root / "kosmos/download.html",
                web_root / "kosmos/release-notes.html"}
    if not required.issubset(files) or not all(path.is_file() for path in required):
        raise ValueError(f"Missing website pages under {web_root}")
    result = {}
    labels = 0
    for path in files:
        original = path.read_bytes()
        text = original.decode("utf-8")
        text, count = LABEL.subn(version, text)
        labels += count
        if path.name != "release-notes.html":
            text = LATEST_ARCHIVE.sub(lambda m: m[1] + version + "-" + m[2], text)
        if path == web_root / "kosmos/download.html":
            for platform, extension in (("Linux", "tar.gz"), ("Windows", "zip")):
                url = ("https://github.com/daver64/Kosmos/releases/latest/download/"
                       f"kosmos-beta-{version}-{platform}.{extension}")
                if url not in text:
                    raise ValueError(f"Download page is missing the {platform} latest-release link")
            for name in ("kosmos-sdk.zip", "kosmos-sdk.tar.gz"):
                pattern = rf"(>{re.escape(name)}</a></td><td>)[0-9.]+ [KMG]B"
                if len(re.findall(pattern, text)) != 1:
                    raise ValueError(f"Cannot update download size for {name}")
                if sdk_sizes is not None:
                    size = sdk_sizes[name]
                    label = f"{(size + 1023) // 1024} KB" if size < 1048576 else f"{(size + 524288) // 1048576} MB"
                    text = re.sub(pattern, lambda m: m[1] + label, text)
        if path == web_root / "kosmos/release-notes.html":
            heading = f"<h1>Kosmos {html.escape(version)}</h1>"
            rendered = markdown.markdown(notes, extensions=["fenced_code"])
            if text.count(heading) > 1:
                raise ValueError(f"Duplicate website release notes for {version}")
            if heading in text:
                start = text.index(heading)
                end = text.find("\n<h1>Kosmos ", start + len(heading))
                if end == -1:
                    raise ValueError("Cannot locate the next historical release section")
                if text[start:end].strip() != rendered.strip():
                    raise ValueError(f"Existing website notes for {version} differ from release notes")
            else:
                marker = "<h1>Release notes</h1>"
                if text.count(marker) != 1:
                    raise ValueError("Cannot locate the website release notes insertion point")
                text = text.replace(marker, marker + "\n" + rendered, 1)
        updated = text.encode("utf-8")
        if original != updated or count or path in required:
            result[path] = (original, updated)
    if labels == 0:
        raise ValueError("No website version labels found")
    return result


def remote_hashes(host, paths):
    command = "sha256sum -- " + " ".join(shlex.quote(str(path)) for path in paths)
    output = run(["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=10", host, command],
                 capture=True)
    lines = output.splitlines()
    if len(lines) != len(paths):
        raise ValueError("Unexpected remote checksum output")
    hashes = []
    for line, path in zip(lines, paths):
        match = re.fullmatch(r"([0-9a-f]{64})  (.+)", line)
        if not match or match[2] != str(path):
            raise ValueError(f"Unexpected remote checksum for {path}")
        hashes.append(match[1])
    return hashes


def check_remote(host, pages, *, allow_updated=False):
    paths = list(pages)
    for path, remote in zip(paths, remote_hashes(host, paths)):
        original, updated = pages[path]
        allowed = {digest(original)}
        if allow_updated:
            allowed.add(digest(updated))
        if remote not in allowed:
            raise ValueError(f"Remote {path} differs from the local page; reconcile it before deploying")


def verify_release(gh, version, notes, *, full=False):
    tag = "v" + version
    release = json.loads(run([gh, "api", "repos/daver64/Kosmos/releases/latest"], capture=True))
    if release.get("tag_name") != tag or release.get("draft") or release.get("prerelease"):
        raise ValueError(f"{tag} is not the latest published release")
    assets = {asset["name"]: asset for asset in release["assets"]}
    required = {f"kosmos-beta-{version}-Linux.tar.gz", f"kosmos-beta-{version}-Windows.zip",
                "kosmos-sdk.tar.gz", "kosmos-sdk.zip", "VERSION.txt", "RELEASE_NOTES.md",
                "packs.json", "SHA256SUMS.txt"}
    if not required.issubset(assets):
        raise ValueError(f"Published release is missing assets: {sorted(required - assets.keys())}")
    ref = json.loads(run([gh, "api", f"repos/daver64/Kosmos/git/ref/tags/{tag}"], capture=True))
    obj = ref["object"]
    if obj["type"] != "commit" or not re.fullmatch(r"[0-9a-f]{40}", obj["sha"]):
        raise ValueError(f"Published tag {tag} does not point directly to a commit")
    run(["git", "merge-base", "--is-ancestor", obj["sha"], "HEAD"])
    with tempfile.TemporaryDirectory(prefix="kosmos-turnkey-verify-") as temp:
        def download(name):
            run([gh, "release", "download", tag, "--repo", "daver64/Kosmos",
                 "--pattern", name, "--dir", temp])
            path = Path(temp) / name
            if not path.is_file() or path.stat().st_size != assets[name]["size"]:
                raise ValueError(f"Published asset has wrong size: {name}")
            return path
        for name in ("VERSION.txt", "RELEASE_NOTES.md"):
            download(name)
        if (Path(temp) / "VERSION.txt").read_text().strip() != version:
            raise ValueError("Published VERSION.txt does not match the release")
        if (Path(temp) / "RELEASE_NOTES.md").read_text() != notes:
            raise ValueError("Local release notes do not match the published release")
        if full:
            sums = {}
            for line in download("SHA256SUMS.txt").read_text().splitlines():
                match = re.fullmatch(r"([0-9a-f]{64})  ([A-Za-z0-9][A-Za-z0-9._-]*)", line)
                if not match or match[2] in sums:
                    raise ValueError("Invalid published SHA256SUMS.txt")
                sums[match[2]] = match[1]
            archives = {name for name in required if name.endswith((".zip", ".tar.gz"))}
            if not archives.issubset(sums) or not sums.keys() <= assets.keys():
                raise ValueError("Published checksums do not cover the required archives")
            for name, expected in sums.items():
                sha = hashlib.sha256()
                with download(name).open("rb") as stream:
                    for block in iter(lambda: stream.read(1024 * 1024), b""):
                        sha.update(block)
                if sha.hexdigest() != expected:
                    raise ValueError(f"Published checksum mismatch: {name}")
    return {name: assets[name]["size"] for name in ("kosmos-sdk.zip", "kosmos-sdk.tar.gz")}


def deploy(host, pages):
    check_remote(host, pages, allow_updated=True)
    for path, (original, updated) in pages.items():
        if path.read_bytes() not in (original, updated):
            raise ValueError(f"Local page changed during publication: {path}")
    with tempfile.TemporaryDirectory(prefix="kosmos-turnkey-site-") as temp:
        for number, (path, (_, updated)) in enumerate(pages.items()):
            staged = Path(temp) / str(number) / path.name
            staged.parent.mkdir()
            staged.write_bytes(updated)
            run(["rsync", "-c", "-t", "--itemize-changes", staged,
                 f"{host}:{shlex.quote(str(path))}"])
    expected = [digest(updated) for _, updated in pages.values()]
    if remote_hashes(host, list(pages)) != expected:
        raise ValueError("Remote website verification failed")
    for path, (original, updated) in pages.items():
        if path.read_bytes() not in (original, updated):
            raise ValueError(f"Local page changed during deployment: {path}")
        if path.read_bytes() != updated:
            path.write_bytes(updated)


def verify_public(site_url, web_root, pages, *, allow_original=False):
    parts = urlsplit(site_url)
    if parts.scheme not in ("http", "https") or not parts.netloc or parts.query or parts.fragment:
        raise ValueError("Site URL must be an HTTP(S) URL without a query or fragment")
    for path, (original, updated) in pages.items():
        relative = path.relative_to(web_root).as_posix()
        url = site_url.rstrip("/") + "/" + quote(relative)
        with urlopen(url, timeout=30) as response:
            allowed = {digest(updated)}
            if allow_original:
                allowed.add(digest(original))
            if digest(response.read()) not in allowed:
                raise ValueError(f"Public page does not match the deployed page: {url}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--resume", metavar="VERSION", help="finish website deployment for a published release")
    parser.add_argument("--packs-dir", type=Path, help="updated scenery catalogue and archives")
    parser.add_argument("--gh", default=os.environ.get("GH", "gh"), help="GitHub CLI executable")
    parser.add_argument("--host", default=os.environ.get("HOST", "dave@gemini"), help="website SSH host")
    parser.add_argument("--web-root", type=Path, default=Path(os.environ.get("WEB_ROOT", "/var/www/html")),
                        help="local and remote website root")
    parser.add_argument("--site-url", default=os.environ.get("SITE_URL", "https://salyut.ddns.net/"),
                        help="public website root URL for verification")
    args = parser.parse_args()
    try:
        if not args.web_root.is_absolute():
            raise ValueError("Website root must be an absolute path")
        if args.resume and args.packs_dir:
            raise ValueError("--packs-dir cannot be used with --resume")
        current = (ROOT / "VERSION").read_text().strip()
        version = args.resume or current
        following = next_version(version)
        if args.resume and current not in (version, following):
            raise ValueError(f"VERSION is {current}; expected {version} or {following}")
        notes = (ROOT / "docs/release-notes" / f"{version}.md").read_text()
        sizes = verify_release(args.gh, version, notes, full=True) if args.resume else None
        pages = prepare_site(args.web_root, version, notes, sizes)
        check_remote(args.host, pages, allow_updated=bool(args.resume))
        if args.resume:
            verify_public(args.site_url, args.web_root, pages, allow_original=True)
        else:
            verify_public(args.site_url, args.web_root, {path: (old, old) for path, (old, _) in pages.items()})
        if not args.resume:
            command = [sys.executable, ROOT / "scripts/publish-github-release.py", "--gh", args.gh]
            if args.packs_dir:
                command += ["--packs-dir", args.packs_dir]
            run(command)
            if prepare_site(args.web_root, version, notes) != pages:
                raise ValueError("Local website changed during the build; reconcile it before resuming")
            sizes = verify_release(args.gh, version, notes)
        if (ROOT / "VERSION").read_text().strip() == version:
            (ROOT / "VERSION").write_text(following + "\n")
        elif (ROOT / "VERSION").read_text().strip() != following:
            raise ValueError("VERSION changed during publication; inspect it before continuing")
        pages = prepare_site(args.web_root, version, notes, sizes)
        deploy(args.host, pages)
        verify_public(args.site_url, args.web_root, pages)
        print(f"Published v{version} and deployed its website. VERSION is {following}; commit and push it separately.")
    except (OSError, ValueError, KeyError, json.JSONDecodeError, subprocess.CalledProcessError) as error:
        print(f"Turnkey release failed: {error}", file=sys.stderr)
        print("Inspect GitHub for a draft or published release before retrying. "
              "For a published release, use --resume VERSION; never replace its assets.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
