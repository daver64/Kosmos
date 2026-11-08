#!/usr/bin/env python3
"""Build, verify and publish a GitHub release without re-uploading unchanged scenery."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tarfile
import tempfile
import zipfile


ROOT = Path(__file__).resolve().parents[1]
REPO = "daver64/Kosmos"
BASE = f"https://github.com/{REPO}/releases/"
VERSION = re.compile(r"[0-9]+\.[0-9]+")
FILENAME = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]*")
SHA = re.compile(r"[0-9a-f]{64}")


def run(args, *, cwd=ROOT, env=None, capture=False):
    return subprocess.run([str(x) for x in args], cwd=cwd, env=env, check=True,
                          text=True, stdout=subprocess.PIPE if capture else None).stdout


def digest(path):
    sha = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            sha.update(block)
    return sha.hexdigest()


def checksums(text):
    result = {}
    for line in text.splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  ([A-Za-z0-9][A-Za-z0-9._-]*)", line)
        if not match or match[2] in result:
            raise ValueError("Invalid or duplicate entry in SHA256SUMS.txt")
        result[match[2]] = match[1]
    return result


def validate_catalogue(catalogue):
    if not isinstance(catalogue, dict) or not isinstance(catalogue.get("packs"), list) or not catalogue["packs"]:
        raise ValueError("Scenery catalogue must contain a non-empty packs array")
    names, ids = set(), set()
    for pack in catalogue["packs"]:
        if not isinstance(pack, dict):
            raise ValueError("Invalid scenery entry")
        name, ident, sha, size = (pack.get(k) for k in ("file", "id", "sha256", "size"))
        if (not isinstance(name, str) or not FILENAME.fullmatch(name) or name in names or
                not isinstance(ident, str) or not ident or ident in ids or
                not isinstance(sha, str) or not SHA.fullmatch(sha) or
                type(size) is not int or size <= 0):
            raise ValueError(f"Invalid or duplicate scenery entry: {ident!r}")
        for field in ("marker", "remove"):
            value = pack.get(field, pack.get("marker"))
            if (not isinstance(value, str) or not value or value == "." or value.startswith("/") or
                    "\\" in value or ".." in value):
                raise ValueError(f"Invalid scenery {field}: {value!r}")
        for field in ("name", "description"):
            if field in pack and not isinstance(pack[field], str):
                raise ValueError(f"Invalid scenery {field}: {ident}")
        if pack.get("remove", pack["marker"]) != pack["marker"] and not pack["marker"].startswith(pack["remove"] + "/"):
            raise ValueError(f"Scenery removal path does not contain marker: {ident}")
        names.add(name)
        ids.add(ident)
    return catalogue


def pinned_tag(url, name, latest_tag):
    if url == BASE + "latest/download/" + name:
        return latest_tag
    prefix, suffix = BASE + "download/v", "/" + name
    if not isinstance(url, str) or not url.startswith(prefix) or not url.endswith(suffix):
        raise ValueError(f"Untrusted scenery URL: {url!r}")
    version = url[len(prefix):-len(suffix)]
    if not VERSION.fullmatch(version):
        raise ValueError(f"Invalid scenery release URL: {url!r}")
    return "v" + version


class GitHub:
    def __init__(self, executable):
        self.gh = executable

    def call(self, *args, capture=False):
        return run([self.gh, *args], capture=capture)

    def api(self, path):
        return json.loads(self.call("api", path, capture=True))

    def draft(self, tag):
        return json.loads(self.call("release", "view", tag, "--repo", REPO,
                                    "--json", "isDraft,targetCommitish,assets", capture=True))

    def download(self, tag, name, directory):
        self.call("release", "download", tag, "--repo", REPO, "--pattern", name, "--dir", directory)
        path = directory / name
        if not path.is_file():
            raise ValueError(f"Missing downloaded asset {tag}/{name}")
        return path


def previous_catalogue(github, latest, directory):
    tag = latest["tag_name"]
    if not re.fullmatch(r"v[0-9]+\.[0-9]+", tag):
        raise ValueError(f"Unsupported latest release tag: {tag}")
    catalogue = validate_catalogue(json.loads(github.download(tag, "packs.json", directory).read_text()))
    releases = {tag: latest}
    sums = {}
    for pack in catalogue["packs"]:
        source = pinned_tag(pack.get("url"), pack["file"], tag)
        if source not in releases:
            releases[source] = github.api(f"repos/{REPO}/releases/tags/{source}")
        release = releases[source]
        if release.get("draft") or release.get("prerelease"):
            raise ValueError(f"Scenery source {source} is not a published release")
        asset = next((a for a in release["assets"] if a["name"] == pack["file"]), None)
        if not asset or asset["size"] != pack["size"]:
            raise ValueError(f"Scenery asset missing or wrong size: {source}/{pack['file']}")
        if source not in sums:
            folder = directory / source
            folder.mkdir()
            sums[source] = checksums(github.download(source, "SHA256SUMS.txt", folder).read_text())
        if sums[source].get(pack["file"]) != pack["sha256"]:
            raise ValueError(f"Scenery checksum does not match {source}/SHA256SUMS.txt: {pack['file']}")
        pack["url"] = BASE + "download/" + source + "/" + pack["file"]
    return catalogue


def prepare_catalogue(previous, pack_dir, tag):
    if pack_dir is None:
        return previous, []
    current = validate_catalogue(json.loads((pack_dir / "packs.json").read_text()))
    old = {pack["file"]: pack for pack in previous["packs"]}
    uploads = []
    for pack in current["packs"]:
        path = pack_dir / pack["file"]
        if not path.is_file() or path.stat().st_size != pack["size"] or digest(path) != pack["sha256"]:
            raise ValueError(f"Local scenery archive does not match catalogue: {path}")
        existing = old.get(pack["file"])
        if existing and existing["sha256"] == pack["sha256"] and existing["size"] == pack["size"]:
            pack["url"] = existing["url"]
        else:
            pack["url"] = BASE + "download/" + tag + "/" + pack["file"]
            uploads.append(path)
    return current, uploads


def verify_assets(github, tag, files, release, directory):
    assets = {asset["name"]: asset for asset in release["assets"]}
    for path in files:
        asset = assets.get(path.name)
        if not asset or asset["size"] != path.stat().st_size:
            raise ValueError(f"Release asset missing or wrong size: {path.name}")
        remote = github.download(tag, path.name, directory)
        if digest(remote) != digest(path):
            raise ValueError(f"Release asset failed download verification: {path.name}")


def validate_archives(files, version):
    root = f"kosmos-beta-{version}-Linux/"
    with tarfile.open(files[0], "r:gz") as archive:
        for name in ("kosmos", "kosmos-launcher", "VERSION.txt"):
            if not archive.getmember(root + name).isfile():
                raise ValueError(f"Linux package is missing {name}")
        if archive.extractfile(root + "VERSION.txt").read().decode().strip() != version:
            raise ValueError("Linux package has the wrong version")
    root = f"kosmos-beta-{version}-Windows/"
    with zipfile.ZipFile(files[1]) as archive:
        for name in ("kosmos.exe", "kosmos-launcher.exe", "VERSION.txt"):
            if archive.getinfo(root + name).is_dir():
                raise ValueError(f"Windows package is missing {name}")
        if archive.read(root + "VERSION.txt").decode().strip() != version:
            raise ValueError("Windows package has the wrong version")
    with zipfile.ZipFile(files[3]) as sdk, tarfile.open(files[2], "r:gz") as sdk_tar:
        for name in ("planetbake/bin/planetbake-linux-x86_64", "planetbake/bin/planetbake-windows-x86_64.exe",
                     "docs/PROTOCOL.md", "docs/MANUAL.md"):
            path = "kosmos-sdk/" + name
            if sdk.getinfo(path).is_dir() or not sdk_tar.getmember(path).isfile():
                raise ValueError(f"SDK build omitted {name}")
        for name in ("PROTOCOL.md", "MANUAL.md"):
            path = "kosmos-sdk/docs/" + name
            expected = (ROOT / "sdk/docs" / name).read_bytes()
            if sdk.read(path) != expected or sdk_tar.extractfile(path).read() != expected:
                raise ValueError(f"SDK archive has outdated {name}")


def publish(args):
    version = (ROOT / "VERSION").read_text().strip()
    if not VERSION.fullmatch(version):
        raise ValueError("VERSION must be major.build")
    tag = "v" + version
    notes = ROOT / "docs" / "release-notes" / (version + ".md")
    if not notes.is_file() or not notes.read_text().strip():
        raise ValueError(f"Missing release notes: {notes}")
    if run(["git", "status", "--porcelain"], capture=True).strip():
        raise ValueError("Commit or remove all working-tree changes before releasing; the script never commits.")
    commit = run(["git", "rev-parse", "HEAD"], capture=True).strip()
    simlib = Path(os.environ.get("KOSMOS_SIMLIB_DIR") or os.environ.get("SIMLIB_SRC") or
                  (Path.home() / "Documents/Projects/rsimlib"))
    pin = (ROOT / "SIMLIB_COMMIT").read_text().strip()
    if not re.fullmatch(r"[0-9a-f]{40}", pin) or not simlib.is_dir():
        raise ValueError(f"A simlib checkout is required to verify SIMLIB_COMMIT: {simlib}")
    if (run(["git", "-C", simlib, "rev-parse", "HEAD"], capture=True).strip() != pin or
            run(["git", "-C", simlib, "status", "--porcelain", "--untracked-files=no"], capture=True).strip()):
        raise ValueError(f"simlib checkout is dirty or does not match SIMLIB_COMMIT: {simlib}")
    github = GitHub(args.gh)
    github.call("auth", "status")
    remote_commit = github.api(f"repos/{REPO}/commits/{commit}")
    if remote_commit.get("sha") != commit:
        raise ValueError("HEAD must be pushed to GitHub before releasing")
    latest = github.api(f"repos/{REPO}/releases/latest")
    latest_tag = latest["tag_name"]
    if not re.fullmatch(r"v[0-9]+\.[0-9]+", latest_tag):
        raise ValueError(f"Unsupported latest release tag: {latest_tag}")
    if tuple(map(int, version.split("."))) <= tuple(map(int, latest_tag[1:].split("."))):
        raise ValueError(f"{tag} is not newer than the latest release {latest_tag}")
    # Check all releases, including drafts, without treating API/auth errors as absence.
    releases = json.loads(github.call("api", "--paginate", "--slurp", f"repos/{REPO}/releases?per_page=100", capture=True))
    if any(release["tag_name"] == tag for page in releases for release in page):
        raise ValueError(f"Release {tag} already exists; inspect it rather than overwriting assets")
    if run(["git", "tag", "--list", tag], capture=True).strip():
        raise ValueError(f"Local tag {tag} already exists; inspect it before proceeding")
    refs = github.api(f"repos/{REPO}/git/matching-refs/tags/{tag}")
    if any(ref["ref"] == "refs/tags/" + tag for ref in refs):
        raise ValueError(f"Remote tag {tag} already exists; inspect it before proceeding")
    with tempfile.TemporaryDirectory(prefix="kosmos-release-") as temp:
        work = Path(temp)
        previous = previous_catalogue(github, latest, work)
        catalogue, scenery = prepare_catalogue(previous, args.packs_dir, tag)
        env = dict(os.environ, JOBS="4")
        env.pop("INCREMENTAL", None)
        env.pop("OUT", None)
        env.pop("SIMLIB_ALLOW_MISMATCH", None)
        run(["sh", ROOT / "scripts/release.sh", "-j4"], env=env)
        test_env = dict(env, SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy",
                        LIBGL_ALWAYS_SOFTWARE="1", LP_NUM_THREADS="4")
        run(["sh", ROOT / "scripts/run-tests.sh"], env=test_env)
        run(["sh", ROOT / "scripts/build-sdk.sh"], env=env)
        out = ROOT / "build" / "package"
        files = [out / f"kosmos-beta-{version}-Linux.tar.gz",
                 ROOT / "build-win" / "package" / f"kosmos-beta-{version}-Windows.zip",
                 out / "kosmos-sdk.tar.gz", out / "kosmos-sdk.zip"]
        if any(not path.is_file() or path.stat().st_size == 0 for path in files):
            raise ValueError("Missing or empty game or SDK archive")
        reserved = {path.name for path in files} | {"VERSION.txt", "RELEASE_NOTES.md", "packs.json", "SHA256SUMS.txt"}
        if any(pack["file"] in reserved for pack in catalogue["packs"]):
            raise ValueError("Scenery archive name conflicts with a release asset")
        if any(path.stat().st_size >= 2 * 1024**3 for path in files + scenery):
            raise ValueError("A release asset exceeds GitHub's 2 GiB limit")
        validate_archives(files, version)
        (work / "VERSION.txt").write_text(version + "\n")
        (work / "RELEASE_NOTES.md").write_text(notes.read_text())
        (work / "packs.json").write_text(json.dumps(catalogue, indent=2) + "\n")
        sums = {path.name: digest(path) for path in files}
        sums.update({path.name: digest(path) for path in scenery})
        (work / "SHA256SUMS.txt").write_text("".join(f"{sha}  {name}\n" for name, sha in sums.items()))
        files += scenery + [work / name for name in ("VERSION.txt", "RELEASE_NOTES.md", "packs.json", "SHA256SUMS.txt")]
        if run(["git", "status", "--porcelain"], capture=True).strip() or \
                run(["git", "rev-parse", "HEAD"], capture=True).strip() != commit:
            raise ValueError("Repository changed during the build; refusing to publish")
        print(f"Creating draft {tag}; {len(scenery)} scenery archive(s) to upload.", flush=True)
        github.call("release", "create", tag, "--repo", REPO, "--target", commit,
                    "--title", f"Kosmos {version}", "--notes-file", notes, "--draft")
        github.call("release", "upload", tag, *files, "--repo", REPO)
        draft = github.draft(tag)
        if not draft.get("isDraft") or draft.get("targetCommitish") != commit:
            raise ValueError("Draft release has the wrong target or is already published")
        verify_dir = work / "verify"
        verify_dir.mkdir()
        verify_assets(github, tag, files, draft, verify_dir)
        refs = github.api(f"repos/{REPO}/git/matching-refs/tags/{tag}")
        if any(ref["ref"] == "refs/tags/" + tag for ref in refs):
            raise ValueError(f"Remote tag {tag} appeared during the build; refusing to publish")
        github.call("release", "edit", tag, "--repo", REPO, "--draft=false", "--latest")
        published = github.api(f"repos/{REPO}/releases/latest")
        if published.get("tag_name") != tag or published.get("draft"):
            raise ValueError(f"{tag} was not confirmed as the latest published release")
        ref = github.api(f"repos/{REPO}/git/ref/tags/{tag}")
        if ref["object"]["type"] != "commit" or ref["object"]["sha"] != commit:
            raise ValueError(f"Published tag {tag} does not point to the built commit")
        if (ROOT / "VERSION").read_text().strip() != version:
            raise ValueError("VERSION changed during publication; update it manually")
        major, minor = version.split(".")
        (ROOT / "VERSION").write_text(f"{major}.{int(minor) + 1}\n")
        print(f"Published {tag}; VERSION advanced locally. Commit and push that change separately.")
        print("Update website version labels and release notes separately.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gh", default=os.environ.get("GH", "gh"), help="GitHub CLI executable")
    parser.add_argument("--packs-dir", type=Path, help="directory with updated packs.json and its archives")
    args = parser.parse_args()
    if args.packs_dir:
        args.packs_dir = args.packs_dir.resolve()
    try:
        publish(args)
    except (OSError, ValueError, KeyError, tarfile.TarError, zipfile.BadZipFile, subprocess.CalledProcessError) as error:
        print(f"Release failed: {error}", file=sys.stderr)
        print("If a draft was created, inspect it on GitHub before retrying.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
