#!/usr/bin/env python3
"""Exercise the launcher's real HTTP cancellation and resume against a local server."""

import http.server
import hashlib
import io
import os
import subprocess
import sys
import tarfile
import tempfile
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "build/launcher_http_test"
DATA = bytes(range(256)) * 8192
buffer = io.BytesIO()
with tarfile.open(fileobj=buffer, mode="w") as archive:
    entry = tarfile.TarInfo("data/payload.bin")
    entry.size = len(DATA)
    archive.addfile(entry, io.BytesIO(DATA))
ARCHIVE = buffer.getvalue()
requests = []


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        header = self.headers.get("Range", "")
        requests.append((self.path, header))
        if self.path == "/redirect":
            self.send_response(302)
            self.send_header("Location", "/range")
            self.end_headers()
            return
        if self.path == "/SHA256SUMS.txt":
            digest = hashlib.sha256(ARCHIVE).hexdigest()
            body = f"{digest}  pack1.tar\n{digest}  pack2.tar\n".encode()
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        data = ARCHIVE if self.path.startswith("/archive") else DATA
        start = int(header[6:-1]) if header.startswith("bytes=") else 0
        if self.path == "/full" and start == len(data):
            self.send_response(416)
            self.send_header("Content-Range", f"bytes */{len(data)}")
            self.end_headers()
            return
        if start and self.path != "/ignore":
            self.send_response(206)
            returned = start + 1 if self.path == "/invalid" else start
            self.send_header("Content-Range", f"bytes {returned}-{len(data)-1}/{len(data)}")
        else:
            start = 0
            self.send_response(200)
        self.send_header("Content-Length", str(len(data) - start))
        self.end_headers()
        try:
            for offset in range(start, len(data), 8192):
                self.wfile.write(data[offset:offset + 8192])
                self.wfile.flush()
                time.sleep(0.004 if self.path.endswith("-slow") else 0.001)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def log_message(self, *_):
        pass


def main():
    if os.environ.get("GDK_BACKEND") != "broadway" and os.environ.get("KOSMOS_TEST_ISOLATED_DISPLAY") != "1":
        raise SystemExit("Use an isolated display (GDK_BACKEND=broadway or KOSMOS_TEST_ISOLATED_DISPLAY=1).")
    server = http.server.HTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever)
    thread.start()
    try:
        with tempfile.TemporaryDirectory() as directory:
            part = Path(directory) / "download.part"
            env = dict(os.environ, HOME=directory, XDG_CONFIG_HOME=str(Path(directory) / "config"))
            wine = os.environ.get("KOSMOS_TEST_WINE") == "1"

            def native_path(path):
                if not wine:
                    return str(path)
                return subprocess.run(["winepath", "-w", str(path)], env=env, check=True,
                                      capture_output=True, text=True, timeout=60).stdout.strip()

            if wine:
                env["XDG_CONFIG_HOME"] = native_path(Path(directory) / "config")
            command = ["wine", str(BINARY)] if wine else [str(BINARY)]

            def run(endpoint, mode):
                result = subprocess.run(command + [f"http://127.0.0.1:{server.server_port}/{endpoint}",
                                         native_path(part), mode], capture_output=True, text=True, timeout=60, env=env)
                assert result.returncode == 0, result.stdout + result.stderr

            run("range", "pause")
            partial = part.read_bytes()
            assert 65536 <= len(partial) < len(DATA) and DATA.startswith(partial)
            run("range", "resume")
            assert part.read_bytes() == DATA
            assert requests[-1][1] == f"bytes={len(partial)}-"
            run("full", "resume")
            assert part.read_bytes() == DATA
            part.write_bytes(DATA[:65536])
            run("ignore", "resume")
            assert part.read_bytes() == DATA
            part.write_bytes(DATA[:65536])
            run("invalid", "invalid")
            assert part.read_bytes() == DATA[:65536]
            part.unlink()
            run("redirect", "resume")
            assert part.read_bytes() == DATA
            queue_modes = os.environ.get("KOSMOS_TEST_QUEUE_MODES", "queue,queue-archive,queue-timeout,queue-timeout-pause").split(",")
            for mode in queue_modes:
                if mode not in ("queue", "queue-archive", "queue-timeout", "queue-timeout-pause"):
                    raise ValueError(f"Unknown queue test mode: {mode}")
                data = ARCHIVE if mode == "queue-archive" else DATA
                queue_root = Path(directory) / mode
                queue_root.mkdir()
                try:
                    result = subprocess.run(command + [f"http://127.0.0.1:{server.server_port}",
                                             native_path(queue_root), mode, hashlib.sha256(data).hexdigest()],
                                            capture_output=True, text=True, timeout=60, env=env)
                except subprocess.TimeoutExpired as error:
                    raise AssertionError(f"{mode} timed out: {error.stderr!r}") from error
                assert result.returncode == 0, result.stdout + result.stderr
    finally:
        server.shutdown()
        thread.join()
        server.server_close()
    print("launcher_http_test ok")


if __name__ == "__main__":
    main()
