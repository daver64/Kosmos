#!/usr/bin/env python3
"""Check entrance label defaults and settings persistence in an isolated home."""

import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
binary = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build/kosmos"
with tempfile.TemporaryDirectory() as home:
    env = dict(os.environ, HOME=home, XDG_CONFIG_HOME=str(Path(home) / ".config"))
    settings = Path(home) / ".config/kosmos/settings.json"
    settings.parent.mkdir(parents=True)
    settings.write_text(json.dumps({"ship-labels": False}))
    result = subprocess.run(
        [str(binary), "--root", str(root), "--selftest", "1",
         "--set", "/selftest/entrance-label-settings=true"],
        cwd=root, env=env, capture_output=True, text=True, timeout=60,
    )
    if result.returncode or "Entrance label settings selftest ok" not in result.stdout:
        raise SystemExit(result.stdout + result.stderr)
    print("entrance_label_settings_test ok")
