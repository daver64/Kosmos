#!/bin/sh
# Makes the launcher's preview images: one 640x360 PNG per scenario, in a thumbs/ folder beside its .json file
# (thumbs/<scenario id>.png). Scenarios that already have one are skipped unless --force is given.
# Needs a display (it starts Kosmos once per scenario) and ImageMagick's convert.
# Usage: make-thumbs.sh [--force] [SCENARIO_ID ...]      run from anywhere; uses build/kosmos
# Overrides (env): SHOT_AT (seconds of simulated start-up before the shot, default 12), KOSMOS (path to the executable)
set -e
SRC=$(cd "$(dirname "$0")/.." && pwd)
KOSMOS=${KOSMOS:-$SRC/build/kosmos}
SHOT_AT=${SHOT_AT:-12}
force=0
[ "$1" = "--force" ] && { force=1; shift; }
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

cd "$(dirname "$KOSMOS")"
find "$SRC/data/scenarios" "$SRC"/addons/*/scenarios -name '*.json' 2>/dev/null | sort | while read -r file; do
    id=$(basename "$file" .json)
    if [ $# -gt 0 ]; then
        case " $* " in *" $id "*) ;; *) continue ;; esac
    fi
    out=$(dirname "$file")/thumbs/$id.png
    [ $force -eq 0 ] && [ -f "$out" ] && continue
    echo "thumbnail: $id"
    timeout 180 "$KOSMOS" --hide-info --scenario "$id" --shot "$tmp/$id.png" --shot-at "$SHOT_AT" > /dev/null 2>&1 || true
    if [ -f "$tmp/$id.png" ]; then
        mkdir -p "$(dirname "$out")"
        convert "$tmp/$id.png" -resize 640x360 "$out"
    else
        echo "  no image for $id"
    fi
done
