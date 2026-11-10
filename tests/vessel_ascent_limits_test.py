#!/usr/bin/env python3
"""Keep the MFD's target bounds aligned with the ascent controller."""

import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
header = (root / "src/vessel_ascent_limits.h").read_text()
mfd = (root / "addons/stockmfd/vessel_ascent.lua").read_text()
controller = (root / "src/vessel_ascent.cpp").read_text()

for bound in ("min", "max"):
    cpp = re.search(rf"\bvessel_ascent_{bound}_target_km\s*=\s*(\d+(?:\.\d+)?)\s*;", header)
    lua = re.search(rf"^local {bound}_target_km\s*=\s*(\d+(?:\.\d+)?)\s*$", mfd, re.MULTILINE)
    assert cpp and lua, f"Missing {bound} ascent target limit"
    assert float(cpp.group(1)) == float(lua.group(1)), f"MFD and controller {bound} limits differ"
    assert re.search(rf"\btarget\s*{'<' if bound == 'min' else '>'}\s*vessel_ascent_{bound}_target_km\b", controller), \
        f"Controller does not use {bound} limit"
    assert re.search(rf"\bmath\.{'max' if bound == 'min' else 'min'}\({bound}_target_km\s*,", mfd), \
        f"MFD does not use {bound} limit"

print("vessel ascent target limits agree")
