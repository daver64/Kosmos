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

# Saved user settings must not leak into the shots, so weather and the cloud style are always set.
W='--set /sim/settings/weather=1 --set /sim/settings/gfx-cloud-style=2 --clean'
# Rockets just clearing the tower; ships in orbit with the planet behind them, day to the right, night to the left, above the terminator;
# aircraft in level flight over the Alps, with the cloud deck 10 km up so it stays out of the view.
ORB='--sunlit --sun-angle -90 --orbit-alt 4000 --cam-yaw 0 --cam-pitch 1.3'
LAUNCH='--auto-stage --launch-at 2 --cam-yaw 0.8 --cam-pitch 0.08'
SHOTS="launch_proton|9|--scenario proton_k_baikonur $W $LAUNCH --cam-distance 200
interior_quarters|14|--hide-info --scenario mare_quarters_on_foot $W --interior-at 3 3.5 0 -5
launch_delta_iv_heavy|8.5|--scenario delta_iv_heavy_kennedy --date '2024-06-21 15:00:00' $W $LAUNCH --cam-distance 260
launch_atlas|8|--scenario atlas_iii_kennedy --date '2024-06-21 15:00:00' $W $LAUNCH --cam-distance 200
launch_soyuz|8|--scenario soyuz_fg_twickenham --date '2024-06-21 11:00:00' $W $LAUNCH --cam-distance 200
launch_kestrel|7|--ship Kestrel $W $LAUNCH --cam-distance 150
orbit_deltaglider|8|--ship 'CVEL Deltaglider' --orbit --alone --hide-info --date '2024-06-21 11:00:00' $W --sunlit --sun-angle -70 --orbit-alt 400 --cam-yaw -1.5 --cam-pitch 0.4 --cam-distance 40
orbit_dragonfly|8|--scenario dragonfly_orbit --hide-info --date '2024-06-21 11:00:00' $W --sunlit --sun-angle 20 --orbit-alt 600 --cam-yaw -0.5 --cam-pitch 0.65 --cam-distance 25
orbit_shuttle_a|8|--scenario shuttle_a_cargo_orbit --hide-info --date '2024-06-21 11:00:00' $W --sunlit --sun-angle 20 --orbit-alt 400 --cam-yaw 0.8 --cam-pitch 0.6 --cam-distance 55
orbit_soyuz|4|--scenario soyuz_tma_orbit $W $ORB --cam-distance 40
orbit_mars_stack|8|--scenario-file '$SRC/scripts/promo/gallery_mars_stack.json' --hide-info $W --sunlit --sun-angle -70 --cam-yaw -0.6 --cam-pitch 0.55 --cam-distance 50
flight_skylark_alps|16|--scenario-file '$SRC/scripts/promo/alps_skylark.json' $W --cam-yaw 0.6 --cam-pitch 0.2 --cam-distance 30
flight_vulcan_alps|16|--scenario-file '$SRC/scripts/promo/alps_vulcan.json' $W --cam-yaw 0.7 --cam-pitch 0.15 --cam-distance 45
flight_cub_alps|16|--scenario-file '$SRC/scripts/promo/alps_cub.json' $W --cam-yaw 0.7 --cam-pitch 0.15 --cam-distance 22
flight_deltaglider_alps|16|--scenario-file '$SRC/scripts/promo/alps_deltaglider.json' $W --cam-yaw 0.7 --cam-pitch 0.15 --cam-distance 38
cockpit3d_deltaglider|8|--hide-info --ship 'CVEL Deltaglider' --orbit --alone $W --cockpit3d --cam-pitch -20
cockpit2d_deltaglider|8|--hide-info --ship 'CVEL Deltaglider' --orbit --alone $W --cockpit
cockpit3d_skylark|16|--hide-info --scenario-file '$SRC/scripts/promo/alps_skylark.json' $W --cockpit3d
cockpit2d_skylark|16|--hide-info --scenario-file '$SRC/scripts/promo/alps_skylark.json' $W --cockpit
cockpit3d_shuttle_a|10|--hide-info --scenario shuttle_a_cargo_orbit $W --sunlit --sun-angle -90 --cockpit3d --cam-yaw 0 --cam-pitch -28"

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
