#!/usr/bin/env python3
"""Verify an existing launcher profile and the ground-relief toggle."""

import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

binary = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory() as home:
    config = Path(home) / "config"
    profile = config / "kosmos" / "launcher.json"
    profile.parent.mkdir(parents=True)
    (profile.parent / "ground-relief-test-marker").touch()
    profile.write_text(json.dumps({"extra_args": "--no-orbital-relief-ground"}))
    env = dict(os.environ, HOME=home, XDG_CONFIG_HOME=str(config), APPDATA=str(config))
    result = subprocess.run([str(binary), "--ground-relief-profile-test", str(config)], env=env,
                            capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr
    saved = json.loads(profile.read_text())
    assert saved["orbital_relief_ground"] is False
    assert saved["extra_args"] == "--no-orbital-relief-ground"
print("launcher_ground_relief_test ok")
