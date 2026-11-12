#!/usr/bin/env python3
"""Check preview generation respects explicitly selected scenario thumbnails."""

import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as directory:
    temp = Path(directory)
    scripts = temp / "scripts"
    scenarios = temp / "scenarios"
    thumbs = scenarios / "thumbs"
    bindir = temp / "bin"
    for path in (scripts, thumbs, bindir):
        path.mkdir(parents=True, exist_ok=True)
    shutil.copy(root / "scripts/make-thumbs.sh", scripts)
    (scenarios / "explicit.json").write_text(json.dumps({"ships": [], "thumbnail": "thumbs/custom.png"}))
    (thumbs / "custom.png").write_text("keep")
    (scenarios / "fallback.json").write_text(json.dumps({"ships": []}))
    kosmos = bindir / "kosmos"
    kosmos.write_text('#!/bin/sh\nwhile [ "$#" -gt 0 ]; do\n'
                      '  if [ "$1" = "--shot" ]; then printf shot > "$2"; exit; fi\n'
                      '  shift\ndone\nexit 1\n')
    convert = bindir / "convert"
    convert.write_text('#!/bin/sh\n[ "$MAGICK_THREAD_LIMIT" = 4 ] || exit 1\n'
                       'input=$1\nfor last; do :; done\ncp "$input" "$last"\n')
    kosmos.chmod(0o755)
    convert.chmod(0o755)
    env = dict(os.environ, KOSMOS=str(kosmos), PATH=str(bindir) + os.pathsep + os.environ["PATH"])
    command = ["sh", str(scripts / "make-thumbs.sh")]
    subprocess.run(command, env=env, check=True, capture_output=True, text=True)
    assert (thumbs / "custom.png").read_text() == "keep"
    assert not (thumbs / "explicit.png").exists()
    assert (thumbs / "fallback.png").read_text() == "shot"
    (thumbs / "fallback.png").write_text("old")
    subprocess.run(command + ["--force"], env=env, check=True, capture_output=True, text=True)
    assert (thumbs / "custom.png").read_text() == "keep"
    assert not (thumbs / "explicit.png").exists()
    assert (thumbs / "fallback.png").read_text() == "shot"

print("make_thumbs_test ok")
