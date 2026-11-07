#!/bin/bash
# Capture with Kosmos' built-in recorder and screenshot facility, then edit with ffmpeg.
# Usage: bash scripts/promo/make-beta-promo.sh [all|clips|screenshots|edit|thumbnail|planets-review|holst-review|holst-modern-review] [output-directory]
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
        launch_photo) SCENE=(--no-clouds --scenario apollo11_saturn_v_t_minus_5 --auto-stage --launch-at 4 --cam-yaw 0.8 --cam-pitch 0.04 --cam-distance 470) ;;
        titan) SCENE=(--no-clouds --scenario titan4b_vandenberg --auto-stage --launch-at 4 --cam-yaw 0.8 --cam-pitch 0.08 --cam-distance 180 --set /sim/settings/weather=0) ;;
        soyuz) SCENE=(--no-clouds --scenario soyuz_fg_twickenham --auto-stage --launch-at 4 --cam-yaw 0.8 --cam-pitch 0.08 --cam-distance 140 --set /sim/settings/weather=0) ;;
        weapons) SCENE=(--no-clouds --scenario-file "$ROOT/scripts/promo/weapons.json" --sunlit --sun-angle 45 --set /controls/weapons/primary/fire=1 --cam-yaw 3.7 --cam-pitch 0.25 --cam-distance 90 --cam-spin 2.5) ;;
        reentry)
            SCENE=(--no-clouds --scenario-file "$ROOT/scripts/promo/buran_reentry.json" --cam-yaw 0.8 --cam-pitch 0.2 --cam-distance 100 --cam-spin 1)
            if [[ -n ${PROMO_TELEMETRY_PORT:-} ]]; then
                SCENE+=(--link --link-port "$PROMO_TELEMETRY_PORT")
            fi ;;
        flight) SCENE=(--no-clouds --scenario-file "$ROOT/scripts/promo/alps_deltaglider.json" --cam-yaw 0.7 --cam-pitch 0.15 --cam-distance 38 --cam-spin 1) ;;
        aircraft_autopilot) SCENE=(--no-clouds --scenario-file "$ROOT/scripts/promo/alps_deltaglider.json" --cockpit --aircraft-autopilot-window --set /autopilot/enabled=1 --set /autopilot/ac/hdg-hold=1 --set /autopilot/ac/heading=70 --set /autopilot/ac/alt-hold=1 --set /autopilot/ac/altitude=3600) ;;
        shuttle) SCENE=(--no-clouds --scenario shuttle_a_cargo_orbit --sunlit --sun-angle 20 --orbit-alt 400 --cam-yaw 0.8 --cam-pitch 0.6 --cam-distance 55 --cam-spin 1) ;;
        cockpit) SCENE=(--no-clouds --ship "CVEL Deltaglider" --orbit --alone --sunlit --sun-angle 20 --cockpit --mfd-left orbit --mfd-right surface) ;;
        orbitcomp) SCENE=(--no-clouds --scenario dragonfly_deltaglider_mars --mfd 1 --mfd-left orbitcomp --mfd-right orbit --set /orbitcomp/page=4 --set-at 5 /orbitcomp/page=2 --set-at 9 /orbitcomp/page=4 --cam-distance 55) ;;
        autoland) SCENE=(--scenario ares_spider_mars_orbit --spacecraft-autopilot-window --set /autopilot/land/armed=1 --cam-yaw 0.8 --cam-pitch 0.35 --cam-distance 40) ;;
        habitat) SCENE=(--scenario mare_quarters_on_foot --interior-at 2.6 2.7 0 0) ;;
        habitat_room) SCENE=(--scenario mare_quarters_on_foot --interior-at 3.0 4.2 20 0) ;;
        eva) CLEAN=(--hide-info); SCENE=(--scenario-file "$ROOT/scripts/promo/moon_highlands.json" --date "2024-01-22 10:00:00" --eva) ;;
        mars) SCENE=(--scenario ares_spider_gale_crater --cam-yaw 0.7 --cam-pitch 0.18 --cam-distance 80 --cam-spin 1) ;;
        planet_mars) SCENE=(--no-clouds --ship "CVEL Deltaglider" --orbit-body Mars --orbit-alt 400 --alone --sunlit --sun-angle 20 --cam-yaw -1.5 --cam-pitch 0.4 --cam-distance 40 --cam-spin 1) ;;
        planet_jupiter) SCENE=(--no-clouds --ship "CVEL Shuttle-A" --orbit-body Jupiter --orbit-alt 30000 --alone --sunlit --sun-angle 20 --cam-yaw 0.8 --cam-pitch 0.4 --cam-distance 55 --cam-spin 1) ;;
        planet_saturn) SCENE=(--no-clouds --ship "CVEL Dragonfly" --orbit-body Saturn --orbit-alt 30000 --alone --sunlit --sun-angle 20 --cam-yaw -0.5 --cam-pitch 0.4 --cam-distance 25 --cam-spin 1) ;;
        exoplanet) SCENE=(--scenario-file "$ROOT/scripts/promo/thalassa_surface.json" --date "2024-06-21 23:00:00" --cam-yaw 0.7 --cam-pitch 0.1 --cam-distance 48 --cam-spin 1 --set /sim/settings/gfx-clutter-density=35) ;;
        closing) SCENE=(--no-clouds --scenario dragonfly_orbit --sunlit --sun-angle 20 --orbit-alt 600 --cam-yaw -0.5 --cam-pitch 0.65 --cam-distance 25 --cam-spin 1) ;;
        docked) SCENE=(--no-clouds --scenario-file "$ROOT/scripts/promo/gallery_mars_stack.json" --sunlit --sun-angle -70 --cam-yaw -0.6 --cam-pitch 0.55 --cam-distance 50) ;;
        moon) SCENE=(--scenario-file "$ROOT/scripts/promo/moon_apennines.json" --date "2024-01-22 10:00:00" --cam-yaw 0.8 --cam-pitch 0.1 --cam-distance 48) ;;
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
    local name start duration filter caption caption_x index=0 total
    local -a shots=(
        "orbit|3|4" "launch|6|4" "titan|6|4"
        "flight|3|4" "reentry|3|4" "cockpit|3|4"
        "orbitcomp|3|4.5" "autoland|3|4" "shuttle|3|3.5"
        "habitat|3|4" "eva|5|4" "moon|3|3.5"
        "mars|3|3.5" "exoplanet|3|4" "weapons|0|4.5" "closing|3|6"
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
            filter+=",drawtext=fontfile='$FONT':text='Space and Atmospheric Flight Simulator':fontsize=39:fontcolor=white:x=(w-text_w)/2:y=h*0.755"
            filter+=",drawtext=fontfile='$FONT':text='Free beta available now':fontsize=35:fontcolor=white:x=(w-text_w)/2:y=h*0.825"
            filter+=",drawtext=fontfile='$FONT':text='github.com/daver64/Kosmos':fontsize=34:fontcolor=white:x=(w-text_w)/2:y=h*0.895"
        else
            caption_x=60
            case "$name" in
                launch) caption="Saturn V launch" ;;
                titan) caption="Titan 4B launch" ;;
                flight) caption="Atmospheric flight" ;;
                reentry) caption="Atmospheric re-entry" ;;
                cockpit) caption="2D cockpit and flight instruments" ;;
                orbitcomp) caption="OrbitComp navigation and planning" ;;
                autoland) caption="Experimental Mars autoland"; caption_x="w-text_w-60" ;;
                shuttle) caption="Orbital flight" ;;
                habitat) caption="Habitat life-support systems" ;;
                eva) caption="Lunar EVA" ;;
                moon) caption="Explore the Moon" ;;
                mars) caption="Explore Mars" ;;
                exoplanet) caption="Explore other star systems" ;;
                weapons) caption="Spacecraft weapons" ;;
                *) echo "Missing caption for scene: $name" >&2; return 1 ;;
            esac
            filter+=",drawtext=fontfile='$FONT':text='$caption':fontsize=38:fontcolor=white:box=1:boxcolor=black@0.60:boxborderw=14:x=$caption_x:y=60"
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

edit_planets_review() {
    local music="$OUT/holst-1923.mp3" shot name caption music_at index=0
    local -a shots=(
        "planet_mars|Mars|120"
        "planet_jupiter|Jupiter|1150"
        "planet_saturn|Saturn|1580"
    )
    test -s "$music" || { echo "Missing historical recording: $music" >&2; return 1; }
    if [[ $(sha256sum "$music" | cut -d' ' -f1) != 541f7b1eaef43ab034467d4554b4eb8b336d1e77e577683545c163decd7b466d ]]; then
        echo "Unexpected recording; use the documented Holst 1923 MP3 transcode" >&2; return 1
    fi
    : >"$OUT/edit/planets-list.txt"
    for shot in "${shots[@]}"; do
        IFS='|' read -r name caption music_at <<<"$shot"
        index=$((index + 1))
        test -s "$OUT/clips/$name.mp4" || { echo "Missing clip: $name" >&2; return 1; }
        ffmpeg -nostdin -loglevel error -y -ss 2 -i "$OUT/clips/$name.mp4" -ss "$music_at" -i "$music" -t 8 \
            -filter_complex "[0:v]fps=30,format=yuv420p,drawtext=fontfile='$FONT':text='$caption':fontsize=48:fontcolor=white:box=1:boxcolor=black@0.60:boxborderw=14:x=60:y=60[v];[0:a]aresample=48000,volume=0.4[effects];[1:a]aresample=48000,volume=0.65,afade=t=in:st=0:d=0.3,afade=t=out:st=7.6:d=0.4[music];[effects][music]amix=inputs=2:duration=first:normalize=0,alimiter=limit=0.9[a]" \
            -map "[v]" -map "[a]" -c:v libx264 -preset medium -crf 18 -c:a aac -b:a 192k "$OUT/edit/planets-$index.mp4"
        printf "file '%s'\n" "$OUT/edit/planets-$index.mp4" >>"$OUT/edit/planets-list.txt"
    done
    ffmpeg -nostdin -loglevel error -y -f concat -safe 0 -i "$OUT/edit/planets-list.txt" \
        -c copy -movflags +faststart "$OUT/planet-music-review.mp4"
    echo "Wrote $OUT/planet-music-review.mp4 (review only; rights and audio need checking)"
}

edit_holst_review() {
    local music="$OUT/holst-1923.mp3" shot name caption start duration index=0 filter
    local total=0 jupiter_start=-1 saturn_start=-1 jupiter_duration saturn_duration
    local -a shots=(
        "orbit|KOSMOS|3|4"
        "launch|Saturn V launch|6|4"
        "soyuz|Soyuz launch|6|4"
        "titan|Titan 4B launch|6|4"
        "flight|Atmospheric flight|3|4"
        "reentry|Atmospheric re-entry|3|4"
        "cockpit|2D cockpit and flight instruments|3|4"
        "aircraft_autopilot|Aircraft autopilot|3|4"
        "orbitcomp|OrbitComp navigation and planning|3|4"
        "weapons|Spacecraft weapons|0|4"
        "planet_mars|Mars|2|5"
        "planet_jupiter|Jupiter|2|6"
        "planet_saturn|Saturn|2|6"
        "habitat|Habitat systems|3|4"
        "eva|Extravehicular activity|5|4"
        "moon|Lunar surface|3|4"
        "exoplanet|Explore other star systems|3|4"
        "closing|KOSMOS|3|6"
    )
    test -s "$music" || { echo "Missing historical recording: $music" >&2; return 1; }
    if [[ $(sha256sum "$music" | cut -d' ' -f1) != 541f7b1eaef43ab034467d4554b4eb8b336d1e77e577683545c163decd7b466d ]]; then
        echo "Unexpected recording; use the documented Holst 1923 MP3 transcode" >&2; return 1
    fi
    for shot in "${shots[@]}"; do
        IFS='|' read -r name caption start duration <<<"$shot"
        test -s "$OUT/clips/$name.mp4" || { echo "Missing clip: $name" >&2; return 1; }
        [[ $name == planet_jupiter ]] && jupiter_start=$total
        [[ $name == planet_saturn ]] && saturn_start=$total
        total=$((total + duration))
    done
    if ((jupiter_start < 1 || saturn_start <= jupiter_start || total <= saturn_start)); then
        echo "Invalid planetary music timeline" >&2; return 1
    fi
    jupiter_duration=$((saturn_start - jupiter_start))
    saturn_duration=$((total - saturn_start))
    : >"$OUT/edit/holst-list.txt"
    for shot in "${shots[@]}"; do
        IFS='|' read -r name caption start duration <<<"$shot"
        index=$((index + 1))
        filter="fps=30,format=yuv420p"
        if [[ $name == orbitcomp ]]; then
            filter+=",crop=1000:562:460:500,scale=1920:1080:flags=lanczos"
        fi
        if [[ $name == orbit ]]; then
            filter+=",drawtext=fontfile='$FONT':text='KOSMOS':fontsize=96:fontcolor=white:borderw=3:bordercolor=black@0.7:x=(w-text_w)/2:y=h*0.14"
        elif [[ $name == closing ]]; then
            filter+=",drawbox=x=0:y=ih*0.60:w=iw:h=ih*0.40:color=black@0.52:t=fill"
            filter+=",drawtext=fontfile='$FONT':text='KOSMOS':fontsize=80:fontcolor=white:x=(w-text_w)/2:y=h*0.64"
            filter+=",drawtext=fontfile='$FONT':text='Space and Atmospheric Flight Simulator':fontsize=39:fontcolor=white:x=(w-text_w)/2:y=h*0.755"
            filter+=",drawtext=fontfile='$FONT':text='github.com/daver64/Kosmos':fontsize=34:fontcolor=white:x=(w-text_w)/2:y=h*0.85"
        else
            filter+=",drawtext=fontfile='$FONT':text='$caption':fontsize=38:fontcolor=white:box=1:boxcolor=black@0.60:boxborderw=14:x=60:y=60"
        fi
        ffmpeg -nostdin -loglevel error -y -ss "$start" -i "$OUT/clips/$name.mp4" -t "$duration" \
            -vf "$filter" -af "aresample=48000" \
            -c:v libx264 -preset medium -crf 18 -c:a aac -b:a 192k "$OUT/edit/holst-$index.mp4"
        printf "file '%s'\n" "$OUT/edit/holst-$index.mp4" >>"$OUT/edit/holst-list.txt"
    done
    ffmpeg -nostdin -loglevel error -y -f concat -safe 0 -i "$OUT/edit/holst-list.txt" \
        -c copy "$OUT/edit/holst-picture.mp4"
    # Mars spans the opening and Mars shot; Jupiter and Saturn enter with their shots.
    ffmpeg -nostdin -loglevel error -y -i "$OUT/edit/holst-picture.mp4" \
        -ss 110 -t "$jupiter_start" -i "$music" -ss 1150 -t "$jupiter_duration" -i "$music" -ss 1580 -t "$saturn_duration" -i "$music" \
        -filter_complex "[0:a]volume=0.4[effects];[1:a]volume=0.65,afade=t=in:st=0:d=0.4,afade=t=out:st=$((jupiter_start - 1)).6:d=0.4[mars];[2:a]volume=0.65,afade=t=in:st=0:d=0.4,afade=t=out:st=$((jupiter_duration - 1)).6:d=0.4,adelay=$((jupiter_start * 1000)):all=1[jupiter];[3:a]volume=0.65,afade=t=in:st=0:d=0.4,afade=t=out:st=$((saturn_duration - 1)):d=1,adelay=$((saturn_start * 1000)):all=1[saturn];[effects][mars][jupiter][saturn]amix=inputs=4:duration=first:normalize=0,alimiter=limit=0.9[a]" \
        -map 0:v -map "[a]" -c:v copy -c:a aac -b:a 192k -movflags +faststart "$OUT/kosmos-holst-review.mp4"
    echo "Wrote $OUT/kosmos-holst-review.mp4 (review only; rights and audio need checking)"
}

edit_holst_modern_review() {
    local music="$OUT/holst-omega13a-2025.ogg" picture="$OUT/edit/holst-picture.mp4"
    test -s "$music" || { echo "Missing modern recording: $music" >&2; return 1; }
    if [[ $(sha256sum "$music" | cut -d' ' -f1) != c889ddb9914b0aa08ad5502e587c309c59aca36048f163801b919d4a5fa87a14 ]]; then
        echo "Unexpected recording; use the documented Omega13a 2025 original" >&2; return 1
    fi
    test -s "$picture" || { echo "Missing picture edit; run holst-review first" >&2; return 1; }
    local duration
    duration=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$picture")
    if ! awk -v t="$duration" 'BEGIN {exit !(t >= 79 && t < 80)}'; then
        echo "Picture edit has unexpected duration: $duration; run holst-review first" >&2; return 1
    fi
    ffmpeg -nostdin -loglevel error -y -i "$picture" \
        -ss 128 -t 45 -i "$music" -ss 1201 -t 6 -i "$music" -ss 1660 -t 28 -i "$music" \
        -filter_complex "[0:a]volume=0.4[effects];[1:a]bass=g=3:f=120,volume=0.45,afade=t=in:st=0:d=0.4,afade=t=out:st=44.6:d=0.4[mars];[2:a]bass=g=3:f=120,volume=0.45,afade=t=in:st=0:d=0.4,afade=t=out:st=5.6:d=0.4,adelay=45000:all=1[jupiter];[3:a]bass=g=3:f=120,volume=0.45,afade=t=in:st=0:d=0.4,afade=t=out:st=27:d=1,adelay=51000:all=1[saturn];[effects][mars][jupiter][saturn]amix=inputs=4:duration=first:normalize=0,alimiter=limit=0.8:level=0[a]" \
        -map 0:v -map "[a]" -c:v copy -c:a aac -b:a 192k -movflags +faststart "$OUT/kosmos-holst-modern-review.mp4"
    echo "Wrote $OUT/kosmos-holst-modern-review.mp4 (review only; footage rights need checking)"
}

case "$MODE" in
    clip) capture_clip "${3:?scene required}" "${4:-15}" ;;
    shot) capture_shot "${3:?scene required}" "${4:-10}" ;;
    all|clips)
        capture_clip orbit 17
        capture_clip launch 17
        capture_clip titan 14
        capture_clip flight 17
        capture_clip reentry 18
        capture_clip shuttle 17
        capture_clip cockpit 17
        capture_clip orbitcomp 16
        capture_clip autoland 16
        capture_clip habitat 16
        capture_clip eva 16
        capture_clip moon 16
        capture_clip mars 15
        capture_clip exoplanet 15
        capture_clip weapons 14
        capture_clip closing 17
        ;;&
    all|screenshots)
        capture_shot launch_photo 12
        capture_shot orbit 12
        capture_shot cockpit 12
        capture_shot orbitcomp 8
        capture_shot habitat 12
        capture_shot mars 10
        capture_shot moon 12
        capture_shot exoplanet 10
        mkdir -p "$OUT/selected"
        cp "$OUT/screenshots/launch_photo.png" "$OUT/selected/launch.png"
        for name in orbit cockpit orbitcomp habitat mars moon exoplanet; do
            cp "$OUT/screenshots/$name.png" "$OUT/selected/$name.png"
        done
        ;;&
    all|edit) edit_video; make_thumbnail ;;
    planets-review) edit_planets_review ;;
    holst-review) edit_holst_review ;;
    holst-modern-review) edit_holst_modern_review ;;
    thumbnail) make_thumbnail ;;
    clips|screenshots) ;;
    *) echo "Usage: $0 [all|clips|screenshots|edit|thumbnail|planets-review|holst-review|holst-modern-review] [output-directory], or [clip|shot] output-directory scene [seconds]" >&2; exit 2 ;;
esac
