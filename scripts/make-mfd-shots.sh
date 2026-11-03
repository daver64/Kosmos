#!/bin/sh
# Takes the MFD screenshots used by the MFD manual with Kosmos' own --shot and crops each display out of the frame.
# Needs a display, ImageMagick (convert) and build/kosmos.
# Usage: make-mfd-shots.sh [OUTDIR] [RUN...]   default OUTDIR is build/mfd-shots; with run names, only those runs are taken
# Overrides (env): KOSMOS (path to the executable)
# Edit the RUNS list to change the set. Each line is: run|seconds before the shot|left mode|left image|right mode|right image|kosmos options.
# An image name of - skips that side. Both displays are cropped from one frame, so a run gives up to two images.
set -e
export MAGICK_THREAD_LIMIT=4 OMP_NUM_THREADS=4
SRC=$(cd "$(dirname "$0")/.." && pwd)
KOSMOS=${KOSMOS:-$SRC/build/kosmos}
OUT=${1:-$(dirname "$KOSMOS")/mfd-shots}
[ $# -gt 0 ] && shift
mkdir -p "$OUT"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# The framed MFD is about 340 px square at 1280x720: the left display sits at 256,321 and the right at 684,321.
LEFT='340x340+256+321'
RIGHT='340x340+684+321'

W='--hide-info --set /sim/settings/weather=1 --clean --mfd 1'
STN="--scenario orbital_stations $W"
AIR="--scenario deltaglider_chippenham $W"
CUB="--scenario piper_cub_chippenham $W"
RUNS="transfer|6|transfer|transfer|orbitcomp|orbitcomp-orb|$STN --set /sim/target=Moon
orbitcomp-man|6|orbit|-|orbitcomp|orbitcomp-man|$STN --set /sim/target=Moon --set /orbitcomp/page=2 --set /orbitcomp/prograde=250
orbitcomp-pln|6|orbit|-|orbitcomp|orbitcomp-pln|$STN --set /sim/target=Mars --set /orbitcomp/page=3
orbit-surface|6|orbit|orbit|surface|surface|$STN
map-attitude|6|map|map|attitude|attitude|$STN
ascent|40|ascent|ascent|-|-|--scenario delta_iv_heavy_kennedy $W --auto-stage --launch-at 2
align|6|align|align|orbit|-|$STN --set /sim/target=Moon
systems|6|systems|systems|orbit|-|$STN
air-comnav-vor|8|comnav|comnav|vor|vor|$AIR
air-hsi-vtol|8|hsi|hsi|vtol|vtol|$AIR
air-adf-surface|8|adf|adf|surface|surface-air|$AIR
air-adi-asi|8|adi|adi|asi|asi|$CUB
air-alt-vsi|8|altimeter|altimeter|vsi|vsi|$CUB
air-dg-turn|8|dg|dg|turn|turn|$CUB
air-engine|8|engine|engine|-|-|$CUB"

echo "$RUNS" >"$tmp/runs.txt"
while IFS='|' read -r run secs lmode lname rmode rname options; do
    if [ $# -gt 0 ]; then
        case " $* " in *" $run "*) ;; *) continue ;; esac
    fi
    echo "== $run"
    (cd "$tmp" && DISPLAY=${DISPLAY:-:0} timeout 600 sh -c "exec \"$KOSMOS\" $options --mfd-left $lmode --mfd-right $rmode --shot \"$tmp/$run.png\" --shot-at $secs" </dev/null >/dev/null 2>&1) || true
    [ -f "$tmp/$run.png" ] || { echo "no shot for $run" >&2; continue; }
    [ "$lname" = - ] || convert "$tmp/$run.png" -crop "$LEFT" +repage "$OUT/$lname.png"
    [ "$rname" = - ] || convert "$tmp/$run.png" -crop "$RIGHT" +repage "$OUT/$rname.png"
done <"$tmp/runs.txt"
