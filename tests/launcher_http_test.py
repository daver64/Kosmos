#!/usr/bin/env python3
"""Exercise the launcher's real wxWebRequest transport against a local HTTP server.

Requires a working X display on Linux and a built kosmos-launcher.
"""

import http.server
import hashlib
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import tarfile

ROOT = Path(__file__).resolve().parents[1]
DATA = b"abcdefghijklmnopqrstuvwxyz" * 100
NOTES = b"Fixture release notes\n"
HASH = "a" * 64
pack_buffer = io.BytesIO()
with tarfile.open(fileobj=pack_buffer, mode="w:gz") as archive:
    info = tarfile.TarInfo("data/textures-hires/fixture.txt")
    data = b"fixture textures"
    info.size = len(data)
    archive.addfile(info, io.BytesIO(data))
PACK = pack_buffer.getvalue()
PARTS = [PACK[:len(PACK) // 2], PACK[len(PACK) // 2:]]


def release_manifest():
    def asset(name, size=1, digest=HASH):
        return {"file": name, "size": size, "sha256": digest}

    return {
        "schema": 1,
        "version": "1.1",
        "assets": {
            "linux": asset("kosmos-beta-1.1-Linux.tar.gz"),
            "windows": asset("kosmos-beta-1.1-Windows.zip"),
            "sdk-linux": asset("kosmos-sdk.tar.gz"),
            "sdk-windows": asset("kosmos-sdk.zip"),
            "notes": asset("release-notes.md", len(NOTES), hashlib.sha256(NOTES).hexdigest()),
        },
        "packs": [{
            "id": "hires", "name": "High resolution", "file": "kosmos-hires.tar.gz",
            "marker": "data/textures-hires", "size": len(PACK), "sha256": hashlib.sha256(PACK).hexdigest(),
            "parts": [asset(f"kosmos-hires.tar.gz.part{i:03}", len(part), hashlib.sha256(part).hexdigest())
                      for i, part in enumerate(PARTS, 1)],
        }],
    }


class Fixture(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_GET(self):
        if self.path.startswith("/api-missing?"):
            self.reply(json.dumps([{"tag_name": "v1.1", "draft": False, "assets": []}]).encode())
            return
        if self.path == "/invalid/v1.1/manifest.json":
            self.reply(b'{"schema": 1, "version": "1.1"}')
            return
        if self.path.startswith("/missing/v1.1/kosmos-hires.tar.gz.part"):
            self.send_response(404)
            self.end_headers()
            return
        if self.path.startswith("/missing/"):
            self.path = "/assets/" + self.path[len("/missing/"):]
        corrupt = self.path.startswith("/corrupt/")
        if corrupt:
            self.path = "/assets/" + self.path[len("/corrupt/"):]
        if self.path.startswith("/api?"):
            page = self.path.rsplit("=", 1)[-1]
            versions = range(100) if page == "1" else [101] if page == "2" else []
            entries = [
                {"tag_name": "v1.1" if v == 101 else f"v0.{v}", "draft": False,
                 "prerelease": v == 101, "assets": [{"name": "manifest.json", "state": "uploaded"}]}
                for v in versions
            ]
            self.reply(json.dumps(entries).encode())
            return
        if self.path == "/assets/v1.1/manifest.json":
            self.reply(json.dumps(release_manifest()).encode())
            return
        if self.path == "/assets/v1.1/release-notes.md":
            self.reply(NOTES)
            return
        if self.path.startswith("/assets/v1.1/kosmos-hires.tar.gz.part"):
            index = int(self.path[-3:]) - 1
            body = PARTS[index]
            if corrupt and index == 1:
                body = bytes([body[0] ^ 1]) + body[1:]
            self.reply(body)
            return
        if self.path == "/redirect":
            self.send_response(302)
            self.send_header("Location", "/normal")
            self.end_headers()
            return
        if self.path == "/error":
            self.send_response(404)
            self.end_headers()
            return
        header = self.headers.get("Range")
        offset = int(header[6:-1]) if header and header.startswith("bytes=") and header.endswith("-") else 0
        if self.path == "/ignore":
            offset = 0
        body = DATA[offset:]
        self.send_response(206 if offset else 200)
        if offset:
            actual = offset + 1 if self.path == "/bad-range" else offset
            self.send_header("Content-Range", f"bytes {actual}-{len(DATA)-1}/{len(DATA)}")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if self.path == "/broken":
            self.wfile.write(body[:len(body) // 2])
            self.close_connection = True
        else:
            self.wfile.write(body)

    def reply(self, body):
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def main():
    launcher = ROOT / "build/kosmos-launcher"
    if not launcher.is_file():
        raise SystemExit("Build kosmos-launcher before running this test")
    server = http.server.HTTPServer(("127.0.0.1", 0), Fixture)
    thread = threading.Thread(target=server.serve_forever)
    thread.start()
    base = f"http://127.0.0.1:{server.server_port}"
    try:
        with tempfile.TemporaryDirectory(prefix="kosmos-http-test-") as temp:
            file = Path(temp) / "download"

            def download(path):
                result = subprocess.run([str(launcher), "--check-http", base + path, str(file)],
                                        text=True, capture_output=True, timeout=30, env=os.environ.copy())
                if result.returncode:
                    raise AssertionError(f"launcher exited {result.returncode}: {result.stdout} {result.stderr}")
                return result.stdout

            assert "HTTP OK" in download("/redirect")
            assert file.read_bytes() == DATA
            file.write_bytes(DATA[:99])
            assert "HTTP OK" in download("/normal")
            assert file.read_bytes() == DATA
            file.write_bytes(DATA[:99])
            assert "HTTP OK" in download("/ignore")
            assert file.read_bytes() == DATA
            file.write_bytes(DATA[:99])
            assert "HTTP ERROR" in download("/bad-range")
            assert file.read_bytes() == DATA[:99]
            file.unlink()
            assert "HTTP ERROR" in download("/broken")
            assert 0 < file.stat().st_size < len(DATA)
            assert "HTTP OK" in download("/normal")
            assert file.read_bytes() == DATA
            file.unlink()
            assert "HTTP ERROR" in download("/error")
            assert not file.exists() or file.stat().st_size == 0
            env = os.environ.copy()
            env["XDG_CONFIG_HOME"] = temp
            result = subprocess.run([str(launcher), "--check-release", base + "/api", base + "/assets"],
                                    text=True, capture_output=True, timeout=30, env=env)
            assert result.returncode == 0, (result.stdout, result.stderr)
            assert "GitHub version: 1.1" in result.stdout, (result.stdout, result.stderr)
            assert "packs: 1" in result.stdout and "ERROR" not in result.stdout
            assert (Path(temp) / "kosmos/downloads/v1.1/release-notes.md").read_bytes() == NOTES
            for api, assets, expected in (
                ("/api-missing", "/assets", "missing manifest"),
                ("/api", "/invalid", "Invalid release manifest"),
            ):
                result = subprocess.run([str(launcher), "--check-release", base + api, base + assets],
                                        text=True, capture_output=True, timeout=30, env=env)
                assert "GitHub release ERROR" in result.stdout and expected in result.stdout, (result.stdout, result.stderr)
            install = Path(temp) / "install"
            install.mkdir()
            result = subprocess.run([str(launcher), "--check-release-pack", base + "/api", base + "/assets", str(install)],
                                    text=True, capture_output=True, timeout=30, env=env)
            assert result.returncode == 0, (result.stdout, result.stderr)
            assert "Pack OK" in result.stdout, (result.stdout, result.stderr)
            assert (install / "data/textures-hires/fixture.txt").read_bytes() == b"fixture textures"
            assert not (Path(temp) / "kosmos/downloads/v1.1/kosmos-hires.tar.gz.part001").exists()
            bad_install = Path(temp) / "bad-install"
            bad_install.mkdir()
            env["XDG_CONFIG_HOME"] = str(Path(temp) / "corrupt-config")
            result = subprocess.run([str(launcher), "--check-release-pack", base + "/api", base + "/corrupt", str(bad_install)],
                                    text=True, capture_output=True, timeout=30, env=env)
            assert "Pack ERROR" in result.stdout and "checksum mismatch" in result.stdout, (result.stdout, result.stderr)
            assert not (bad_install / "data/textures-hires/fixture.txt").exists()
            env["XDG_CONFIG_HOME"] = str(Path(temp) / "missing-config")
            result = subprocess.run([str(launcher), "--check-release-pack", base + "/api", base + "/missing", str(bad_install)],
                                    text=True, capture_output=True, timeout=30, env=env)
            assert "Pack ERROR" in result.stdout, (result.stdout, result.stderr)
            assert not (bad_install / "data/textures-hires/fixture.txt").exists()
    finally:
        server.shutdown()
        thread.join()
        server.server_close()
    print("launcher_http_test: passed")


if __name__ == "__main__":
    main()
