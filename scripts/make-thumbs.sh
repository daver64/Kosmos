#!/bin/sh
# Makes the launcher's preview images: one 640x360 PNG per scenario, in a thumbs/ folder beside its .json file
# (thumbs/<scenario id>.png). Explicit scenario thumbnails are left alone; fallback previews are
# skipped unless --force is given.
# Needs Python 3, a display (it starts Kosmos once per scenario) and ImageMagick's convert.
# Usage: make-thumbs.sh [--force] [SCENARIO_ID ...]      run from anywhere; uses build/kosmos
# Overrides (env): SHOT_AT (seconds of simulated start-up before the shot, default 12), KOSMOS (path to the executable)
set -e
SRC=$(cd "$(dirname "$0")/.." && pwd)
KOSMOS=${KOSMOS:-$SRC/build/kosmos}
SHOT_AT=${SHOT_AT:-12}
force=0
[ "$1" = "--force" ] && { force=1; shift; }
shot=$(mktemp "${TMPDIR:-/tmp}/kosmos-thumb.XXXXXXXXXX.png")
trap 'rm -f -- "$shot"' EXIT

cd "$(dirname "$KOSMOS")"
find "$SRC/scenarios" "$SRC"/addons/*/scenarios -name '*.json' 2>/dev/null | sort | while read -r file; do
    id=$(basename "$file" .json)
    if [ $# -gt 0 ]; then
        case " $* " in *" $id "*) ;; *) continue ;; esac
    fi
    explicit=$(python3 -c 'import json, pathlib, sys
p = pathlib.Path(sys.argv[1])
data = json.loads(p.read_text(encoding="utf-8"))
name = data.get("thumbnail") if isinstance(data, dict) else None
print("yes" if isinstance(name, str) and name and (p.parent / name).is_file() else "no")' "$file")
    if [ "$explicit" = yes ]; then
        echo "thumbnail: $id (explicit preview, skipped)"
        continue
    fi
    out=$(dirname "$file")/thumbs/$id.png
    [ $force -eq 0 ] && [ -f "$out" ] && continue
    echo "thumbnail: $id"
    rm -f -- "$shot"
    timeout 180 "$KOSMOS" --hide-info --scenario "$id" --shot "$shot" --shot-at "$SHOT_AT" > /dev/null 2>&1 || true
    if [ -f "$shot" ]; then
        mkdir -p "$(dirname "$out")"
        MAGICK_THREAD_LIMIT=4 convert "$shot" -resize 640x360 -define png:compression-level=9 "$out"
    else
        echo "  no image for $id"
    fi
done
