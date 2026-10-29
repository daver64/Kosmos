#!/bin/sh
# Takes the gallery screenshots with Kosmos' own --shot and writes them as 1280x720 JPEGs.
# Needs a display, ImageMagick (convert) and build/kosmos.
# Usage: make-gallery.sh [OUTDIR] [NAME...]   default OUTDIR is build/gallery; with names, only those shots are taken
# Overrides (env): KOSMOS (path to the executable), QUALITY (JPEG quality, default 88)
# Edit the SHOTS list to change the set. Each line is: name|seconds before the shot|kosmos options.
set -e
# ImageMagick otherwise uses every core; tooling never uses more than 4 threads.
export MAGICK_THREAD_LIMIT=4 OMP_NUM_THREADS=4
SRC=$(cd "$(dirname "$0")/.." && pwd)
export SRC
KOSMOS=${KOSMOS:-$SRC/build/kosmos}
QUALITY=${QUALITY:-88}
OUT=${1:-$(dirname "$KOSMOS")/gallery}
[ $# -gt 0 ] && shift
mkdir -p "$OUT"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

SHOTS='navlights_dragonfly|4|--scenario 07_dragonfly_orbit --clean --sunlit --sun-angle 80 --orbit-alt 3000 --set /controls/lighting/nav=1 --cam-yaw 4.5 --cam-pitch 0.9 --cam-distance 55
navlights_deltaglider|4|--ship "CVEL Deltaglider" --orbit --orbit-alt 3000 --alone --clean --sunlit --sun-angle 80 --set /controls/lighting/nav=1 --cam-yaw 4.5 --cam-pitch 0.9 --cam-distance 36
navlights_shuttle_a|4|--scenario 11_shuttle_a_cargo_orbit --clean --sunlit --sun-angle 80 --orbit-alt 3000 --set /controls/lighting/nav=1 --cam-yaw 4.5 --cam-pitch 0.9 --cam-distance 50
satv_launch|22|--scenario 01_apollo11_saturn_v_t_minus_5 --clean --launch-at 2 --cam-distance 375
skylark_nav|6|--scenario-file "$SRC/scripts/promo/skylark.json" --clean --set /controls/lighting/nav=1 --set /controls/lighting/lamps=0 --cam-yaw 60 --cam-pitch 6 --cam-distance 40
mars_dock|6|--scenario-file "$SRC/scripts/promo/mars.json" --clean --sunlit --sun-angle 45 --cam-yaw 4.5 --cam-pitch 0.9 --cam-distance 45'

echo "$SHOTS" >"$tmp/shots.txt"
while IFS='|' read -r name secs options; do
    if [ $# -gt 0 ]; then
        case " $* " in *" $name "*) ;; *) continue ;; esac
    fi
    echo "== $name"
    (cd "$tmp" && DISPLAY=${DISPLAY:-:0} timeout 600 sh -c "exec \"$KOSMOS\" $options --shot \"$tmp/$name.png\" --shot-at $secs" </dev/null >/dev/null 2>&1) || true
    [ -f "$tmp/$name.png" ] || { echo "no shot for $name" >&2; continue; }
    convert "$tmp/$name.png" -resize 1280x720! -quality "$QUALITY" "$OUT/$name.jpg"
done <"$tmp/shots.txt"
