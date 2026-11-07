#!/bin/bash
# Capture with Kosmos' built-in recorder and screenshot facility, then edit with ffmpeg.
# Usage: bash scripts/promo/make-beta-promo.sh [all|clips|screenshots|edit|thumbnail] [output-directory]
#        bash scripts/promo/make-beta-promo.sh [clip|shot] output-directory scene [seconds]
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${2:-"$ROOT/build/beta-promo"}
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
KOSMOS=${KOSMOS:-"$ROOT/build/kosmos"}
MODE=${1:-all}
FONT=${FONT:-/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf}
FFMPEG_BIN=${FFMPEG_BIN:-$(command -v ffmpeg)}
if [[ ! -x $FFMPEG_BIN || $FFMPEG_BIN -ef "$ROOT/scripts/promo/ffmpeg" ]]; then
    echo "FFMPEG_BIN must point to the real ffmpeg executable" >&2; exit 1
fi
export FFMPEG_BIN
export PATH="$ROOT/scripts/promo:$PATH" OMP_NUM_THREADS=1 MAGICK_THREAD_LIMIT=1
export XDG_CONFIG_HOME="$OUT/config"
export DISPLAY=${DISPLAY:-:0}
export SDL_AUDIODRIVER=${SDL_AUDIODRIVER:-dummy}
mkdir -p "$OUT/clips" "$OUT/screenshots" "$OUT/logs" "$OUT/config" "$OUT/edit"

# Shared visual settings avoid depending on the player's saved settings.
COMMON=(--root "$ROOT" --fullscreen --gen-threads 2 --set /sim/settings/weather=1
        --set /sim/settings/gfx-cloud-style=1 --set /sim/settings/sound=1
        --set /sim/settings/volume=75 --set /sim/settings/gfx-msaa=8
        --set /sim/settings/gfx-terrain-detail=0 --set /sim/settings/gfx-clutter-density=70)

scene() {
    # Avoid the installed Earth cloud pack, which includes AI-edited imagery.
    CLEAN=(--clean)
    case "$1" in
        orbit) SCENE=(--no-clouds --ship "CVEL Deltaglider" --orbit --alone --sunlit --sun-angle 20 --orbit-alt 400 --cam-yaw -1.5 --cam-pitch 0.4 --cam-distance 40 --cam-spin 1) ;;
        launch) SCENE=(--no-clouds --scenario apollo11_saturn_v_t_minus_5 --auto-stage --launch-at 4 --cam-yaw 0.8 --cam-pitch 0.08 --cam-distance 300) ;;
        flight) SCENE=(--no-clouds --scenario-file "$ROOT/scripts/promo/alps_deltaglider.json" --cam-yaw 0.7 --cam-pitch 0.15 --cam-distance 38 --cam-spin 1) ;;
        shuttle) SCENE=(--no-clouds --scenario shuttle_a_cargo_orbit --sunlit --sun-angle 20 --orbit-alt 400 --cam-yaw 0.8 --cam-pitch 0.6 --cam-distance 55 --cam-spin 1) ;;
        cockpit) SCENE=(--no-clouds --ship "CVEL Deltaglider" --orbit --alone --sunlit --sun-angle 20 --cockpit --mfd-left orbit --mfd-right surface) ;;
        orbitcomp) SCENE=(--no-clouds --scenario dragonfly_deltaglider_mars --mfd 1 --mfd-left orbitcomp --mfd-right orbit --set /orbitcomp/page=4 --set-at 5 /orbitcomp/page=2 --set-at 9 /orbitcomp/page=4 --cam-distance 55) ;;
        autoland) SCENE=(--scenario ares_spider_mars_orbit --spacecraft-autopilot-window --set /autopilot/land/armed=1 --cam-yaw 0.8 --cam-pitch 0.35 --cam-distance 40) ;;
        habitat) SCENE=(--scenario mare_quarters_on_foot --interior-at 2.6 2.7 0 0) ;;
        habitat_room) SCENE=(--scenario mare_quarters_on_foot --interior-at 3.0 4.2 20 0) ;;
        eva) CLEAN=(--hide-info); SCENE=(--scenario-file "$ROOT/scripts/promo/moon_highlands.json" --date "2024-01-22 10:00:00" --eva) ;;
        mars) SCENE=(--scenario ares_spider_gale_crater --cam-yaw 0.7 --cam-pitch 0.18 --cam-distance 80 --cam-spin 1) ;;
        exoplanet) SCENE=(--scenario-file "$ROOT/scripts/promo/thalassa_surface.json" --date "2024-06-21 23:00:00" --cam-yaw 0.7 --cam-pitch 0.1 --cam-distance 48 --cam-spin 1 --set /sim/settings/gfx-clutter-density=35) ;;
        closing) SCENE=(--no-clouds --scenario dragonfly_orbit --sunlit --sun-angle 20 --orbit-alt 600 --cam-yaw -0.5 --cam-pitch 0.65 --cam-distance 25 --cam-spin 1) ;;
        docked) SCENE=(--no-clouds --scenario-file "$ROOT/scripts/promo/gallery_mars_stack.json" --sunlit --sun-angle -70 --cam-yaw -0.6 --cam-pitch 0.55 --cam-distance 50) ;;
        moon) SCENE=(--scenario-file "$ROOT/scripts/promo/moon_highlands.json" --date "2024-01-22 10:00:00" --cam-yaw 0.8 --cam-pitch 0.1 --cam-distance 48) ;;
        *) echo "Unknown scene: $1" >&2; return 1 ;;
    esac
}

capture_clip() {
    local name=$1 duration=$2 work="$OUT/edit/capture-$1" clip
    mkdir -p "$work"
    if compgen -G "$work/*.mp4" >/dev/null; then
        echo "Capture directory is not empty: $work" >&2; return 1
    fi
    scene "$name"
    echo "Recording $name"
    (cd "$work" && timeout 600 "$KOSMOS" "${COMMON[@]}" "${CLEAN[@]}" "${SCENE[@]}" --record "$duration") >"$OUT/logs/$name.log" 2>&1 || {
        echo "Recording $name failed; see $OUT/logs/$name.log" >&2; return 1;
    }
    mapfile -t files < <(find "$work" -maxdepth 1 -name '*.mp4' -type f)
    if ((${#files[@]} != 1)); then echo "Expected one clip in $work, got ${#files[@]}" >&2; return 1; fi
    clip=${files[0]}
    mv "$clip" "$OUT/clips/$name.mp4"
    ffprobe -v error -show_entries stream=codec_name,width,height,r_frame_rate -show_entries format=duration -of compact "$OUT/clips/$name.mp4"
}

capture_shot() {
    local name=$1 at=$2
    scene "$name"
    echo "Photographing $name"
    (cd "$OUT/edit" && timeout 600 "$KOSMOS" "${COMMON[@]}" "${CLEAN[@]}" "${SCENE[@]}" --shot "$OUT/screenshots/$name.png" --shot-at "$at") >"$OUT/logs/$name-shot.log" 2>&1 || {
        echo "Screenshot $name failed; see $OUT/logs/$name-shot.log" >&2; return 1;
    }
    test -s "$OUT/screenshots/$name.png"
}

edit_video() {
    local name start duration filter index=0 total
    local -a shots=(
        "orbit|3|4" "launch|6|5" "flight|3|4" "cockpit|3|4"
        "orbitcomp|3|4.5" "autoland|3|4.5" "shuttle|3|4"
        "habitat|3|4.5" "eva|5|4.5" "moon|3|4"
        "mars|3|4.5" "exoplanet|3|4.5" "orbitcomp|8|4"
        "flight|8|4" "launch|10|4.5" "closing|3|6"
    )
    : >"$OUT/edit/list.txt"
    for shot in "${shots[@]}"; do
        IFS='|' read -r name start duration <<<"$shot"
        index=$((index + 1))
        filter="fps=30,format=yuv420p"
        if [[ $name == orbit ]]; then
            filter+=",drawtext=fontfile='$FONT':text='KOSMOS':fontsize=96:fontcolor=white:borderw=3:bordercolor=black@0.7:x=(w-text_w)/2:y=h*0.14:enable='lt(t,4.5)'"
        elif [[ $name == closing ]]; then
            filter+=",drawbox=x=0:y=ih*0.60:w=iw:h=ih*0.40:color=black@0.52:t=fill"
            filter+=",drawtext=fontfile='$FONT':text='KOSMOS':fontsize=80:fontcolor=white:x=(w-text_w)/2:y=h*0.64"
            filter+=",drawtext=fontfile='$FONT':text='Independent Spaceflight Simulator':fontsize=39:fontcolor=white:x=(w-text_w)/2:y=h*0.755"
            filter+=",drawtext=fontfile='$FONT':text='Free beta available now':fontsize=35:fontcolor=white:x=(w-text_w)/2:y=h*0.825"
            filter+=",drawtext=fontfile='$FONT':text='salyut.ddns.net/kosmos':fontsize=34:fontcolor=white:x=(w-text_w)/2:y=h*0.895"
        fi
        test -s "$OUT/clips/$name.mp4"
        if ! ffprobe -v error -select_streams a:0 -show_entries stream=codec_type -of csv=p=0 "$OUT/clips/$name.mp4" | grep -q audio; then
            echo "Missing captured audio in $OUT/clips/$name.mp4" >&2; return 1
        fi
        ffmpeg -nostdin -loglevel error -y -ss "$start" -i "$OUT/clips/$name.mp4" -t "$duration" \
            -vf "$filter" -af "aresample=48000,volume=0.75" \
            -c:v libx264 -preset medium -crf 18 -pix_fmt yuv420p -c:a aac -b:a 192k "$OUT/edit/segment-$index.mp4"
        printf "file '%s'\n" "$OUT/edit/segment-$index.mp4" >>"$OUT/edit/list.txt"
    done
    ffmpeg -nostdin -loglevel error -y -f concat -safe 0 -i "$OUT/edit/list.txt" \
        -c copy "$OUT/edit/picture-and-effects.mp4"
    total=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$OUT/edit/picture-and-effects.mp4")
    OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1 python3 "$ROOT/scripts/promo/beta-music.py" "$OUT/edit/music.wav" "$total"
    ffmpeg -nostdin -loglevel error -y -i "$OUT/edit/picture-and-effects.mp4" -i "$OUT/edit/music.wav" \
        -filter_complex "[1:a]volume=0.75[music];[0:a][music]amix=inputs=2:duration=first:normalize=0,alimiter=limit=0.9[a]" \
        -map 0:v:0 -map "[a]" -c:v copy -c:a aac -b:a 192k -movflags +faststart "$OUT/kosmos-beta-promo.mp4"
    echo "Wrote $OUT/kosmos-beta-promo.mp4"
}

make_thumbnail() {
    test -s "$OUT/screenshots/orbit.png"
    ffmpeg -nostdin -loglevel error -y -i "$OUT/screenshots/orbit.png" \
        -vf "drawtext=fontfile='$FONT':text='KOSMOS':fontsize=116:fontcolor=white:borderw=4:bordercolor=black@0.8:x=(w-text_w)/2:y=h*0.10" \
        -frames:v 1 "$OUT/thumbnail.png"
}

case "$MODE" in
    clip) capture_clip "${3:?scene required}" "${4:-15}" ;;
    shot) capture_shot "${3:?scene required}" "${4:-10}" ;;
    all|clips)
        capture_clip orbit 17
        capture_clip launch 17
        capture_clip flight 17
        capture_clip shuttle 17
        capture_clip cockpit 17
        capture_clip orbitcomp 16
        capture_clip autoland 16
        capture_clip habitat 16
        capture_clip eva 16
        capture_clip moon 16
        capture_clip mars 15
        capture_clip exoplanet 15
        capture_clip closing 17
        ;;&
    all|screenshots)
        capture_shot launch 12
        capture_shot orbit 12
        capture_shot cockpit 12
        capture_shot orbitcomp 8
        capture_shot habitat 12
        capture_shot mars 10
        capture_shot moon 12
        capture_shot exoplanet 10
        mkdir -p "$OUT/selected"
        for name in launch orbit cockpit orbitcomp habitat mars moon exoplanet; do
            cp "$OUT/screenshots/$name.png" "$OUT/selected/$name.png"
        done
        ;;&
    all|edit) edit_video; make_thumbnail ;;
    thumbnail) make_thumbnail ;;
    clips|screenshots) ;;
    *) echo "Usage: $0 [all|clips|screenshots|edit|thumbnail] [output-directory], or [clip|shot] output-directory scene [seconds]" >&2; exit 2 ;;
esac
