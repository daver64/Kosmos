#!/bin/sh
# Makes the promo video: records a clip of each shot below with Kosmos' own --record (so the game's engine and weapon
# sound is on the clip), then joins them with captions and fades over a synthesised soundtrack: an orchestral cue for
# the flight and base shots that drops into techno for the sci-fi weapons shots (scripts/promo-music.py).
# Needs a display, ffmpeg with drawtext, python3 with numpy and scipy, and build/kosmos.
# Usage: make-promo.sh [OUT.mp4]      run from anywhere; default output is build/kosmos-promo.mp4
# Overrides (env): CLIP (seconds per clip, default 9), SIZE (output size, default 1280x720), KOSMOS (path to the executable),
#                  GAME_GAIN and MUSIC_GAIN (mix levels, default 1.6 and 0.55: the engine stays loud over the music)
# Edit the SHOTS list to change the reel. Each line is: caption|seconds|left-hand feature label|kosmos options. A Deltaglider that is not on the ground
# gets its gear up by itself. Seconds "auto" leaves the length to the options (--record N --record-after-stage S);
# "auto:HEAD,TAIL" keeps only the first HEAD and last TAIL seconds of that recording. A shot with --launch-at S gets a
# launch countdown (T-minus, then T-plus) drawn over it.
# The first shot whose caption starts with "TECHNO" is where the music changes; captions cannot contain colons.
set -e
# ffmpeg and x264 otherwise use every core; tooling never uses more than 4 threads.
export OMP_NUM_THREADS=4
ffmpeg() { command ffmpeg -threads 4 -filter_threads 4 -filter_complex_threads 4 "$@"; }
SRC=$(cd "$(dirname "$0")/.." && pwd)
export SRC
KOSMOS=${KOSMOS:-$SRC/build/kosmos}
CLIP=${CLIP:-9}
SIZE=${SIZE:-1280x720}
GAME_GAIN=${GAME_GAIN:-1.6}
BOOM_GAIN=${BOOM_GAIN:-1.2}
MUSIC_GAIN=${MUSIC_GAIN:-0.55}
OUT=${1:-$(dirname "$KOSMOS")/kosmos-promo.mp4}
FONT=${FONT:-/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

SHOTS='Kosmos - a spaceflight simulator|default|N-body orbits and sunlight|--scenario dragonfly_orbit --clean --sunlit --sun-angle 80 --orbit-alt 3000 --set /controls/lighting/nav=1 --cam-yaw 4.5 --cam-pitch 0.9 --cam-distance 55 --cam-spin 6 --cam-tilt 6
Real rockets - the Apollo 11 Saturn V|auto:48,42|Thrust and the rocket equation|--scenario apollo11_saturn_v_t_minus_5 --clean --auto-stage --launch-at 10 --cam-distance 375 --cam-spin 5 --record 200 --record-after-stage 20
TECHNO Fly the Deltaglider|14|Attitude control and RCS|--ship "CVEL Deltaglider" --orbit --orbit-alt 3000 --alone --clean --sunlit --sun-angle 80 --set /controls/lighting/nav=1 --set-at 2 /controls/pitch=1 --set-at 3 /controls/pitch=0 --set-at 4 /controls/yaw=1 --set-at 5 /controls/yaw=0 --set-at 6.5 /controls/roll=1 --set-at 7.5 /controls/roll=0 --set-at 9 /controls/rcs-x=1 --set-at 10.5 /controls/rcs-x=0 --set-at 11 /controls/pitch=-1 --set-at 12 /controls/pitch=0 --cam-yaw 4.5 --cam-pitch 0.9 --cam-distance 36 --cam-spin 7 --cam-tilt 8
Shuttle-A with cargo pods|default|Docking and cargo|--scenario shuttle_a_cargo_orbit --clean --sunlit --sun-angle 80 --orbit-alt 3000 --set /controls/lighting/nav=1 --cam-yaw 4.5 --cam-pitch 0.9 --cam-distance 50 --cam-spin 5 --cam-tilt 6
Shuttle-A - hover pods swing aft, then main engines|42|Pods and main engines|--scenario shuttle_a_cargo_helsinki --alone --clean --mfd 0 --set /controls/lighting/nav=1 --cam-yaw 0.2 --cam-pitch 0.15 --cam-distance 130 --cam-spin 3 --set-at 1 /controls/sas=attitude --set-at 1 /controls/target-heading=90 --set-at 1 /controls/target-pitch=0 --set-at 1 /controls/hover=1 --set-at 1 /controls/pod-left=0.5 --set-at 1 /controls/pod-right=0.5 --set-at 1 /controls/pod-tilt-left=0.5 --set-at 1 /controls/pod-tilt-right=0.5 --set-at 15 /controls/pod-tilt-left=0.525 --set-at 15 /controls/pod-tilt-right=0.525 --set-at 16 /controls/pod-tilt-left=0.550 --set-at 16 /controls/pod-tilt-right=0.550 --set-at 17 /controls/pod-tilt-left=0.575 --set-at 17 /controls/pod-tilt-right=0.575 --set-at 18 /controls/pod-tilt-left=0.600 --set-at 18 /controls/pod-tilt-right=0.600 --set-at 19 /controls/pod-tilt-left=0.625 --set-at 19 /controls/pod-tilt-right=0.625 --set-at 20 /controls/pod-tilt-left=0.650 --set-at 20 /controls/pod-tilt-right=0.650 --set-at 21 /controls/pod-tilt-left=0.675 --set-at 21 /controls/pod-tilt-right=0.675 --set-at 22 /controls/pod-tilt-left=0.700 --set-at 22 /controls/pod-tilt-right=0.700 --set-at 23 /controls/pod-tilt-left=0.725 --set-at 23 /controls/pod-tilt-right=0.725 --set-at 24 /controls/pod-tilt-left=0.750 --set-at 24 /controls/pod-tilt-right=0.750 --set-at 25 /controls/pod-tilt-left=0.775 --set-at 25 /controls/pod-tilt-right=0.775 --set-at 26 /controls/pod-tilt-left=0.800 --set-at 26 /controls/pod-tilt-right=0.800 --set-at 27 /controls/pod-tilt-left=0.825 --set-at 27 /controls/pod-tilt-right=0.825 --set-at 28 /controls/pod-tilt-left=0.850 --set-at 28 /controls/pod-tilt-right=0.850 --set-at 29 /controls/pod-tilt-left=0.875 --set-at 29 /controls/pod-tilt-right=0.875 --set-at 30 /controls/pod-tilt-left=0.900 --set-at 30 /controls/pod-tilt-right=0.900 --set-at 31 /controls/pod-tilt-left=0.925 --set-at 31 /controls/pod-tilt-right=0.925 --set-at 32 /controls/pod-tilt-left=0.950 --set-at 32 /controls/pod-tilt-right=0.950 --set-at 33 /controls/pod-tilt-left=0.975 --set-at 33 /controls/pod-tilt-right=0.975 --set-at 34 /controls/pod-tilt-left=1.000 --set-at 34 /controls/pod-tilt-right=1.000 --set-at 24.5 /controls/throttle=0.1 --set-at 25.0 /controls/throttle=0.2 --set-at 25.5 /controls/throttle=0.3 --set-at 26.0 /controls/throttle=0.7 --set-at 26.5 /controls/throttle=0.5 --set-at 27.0 /controls/throttle=0.6 --set-at 27.5 /controls/throttle=0.7 --set-at 28.0 /controls/throttle=0.8 --set-at 28.5 /controls/throttle=0.9 --set-at 29.0 /controls/throttle=1.0
Dock, transfer, arrive - Mars orbit|default|Interplanetary transfers|--scenario-file "$SRC/scripts/promo/mars.json" --clean --sunlit --sun-angle 45 --cam-yaw 4.5 --cam-pitch 0.9 --cam-distance 45 --cam-spin -5
Step out and walk into the base|default|Walkable interiors|--scenario mare_quarters_on_foot --clean --interior-at 5.5 3.5 0 0
Lamps, windows and live life support|default|Life support simulation|--scenario mare_quarters_on_foot --clean --interior-at 3.2 4.0 90 -5
Skylark - straight and level, then a banked turn|18|Aerodynamics and autopilot|--scenario-file "$SRC/scripts/promo/skylark.json" --clean --set /controls/lighting/nav=1 --set /controls/lighting/lamps=0 --set-at 8 /autopilot/ac/heading=150 --cam-yaw 60 --cam-pitch 6 --cam-distance 40 --cam-spin 3
Vulcan - low-level ripple bombing run|24|Ordnance and blast physics|--scenario-file "$SRC/scripts/promo/vulcan.json" --clean --set /controls/lighting/nav=0 --set /controls/lighting/lamps=0 --drop 1 8 0.7 --cam-yaw 1.9 --cam-pitch 0.15 --cam-distance 60 --cam-dolly 70 --cam-spin 1
Beam weapons hit a target|14|Weapons and shields|--scenario-file "$SRC/scripts/promo/weapons.json" --clean --sunlit --sun-angle 45 --set /controls/weapons/primary/fire=1 --cam-yaw 3.7 --cam-pitch 0.25 --cam-distance 90 --cam-spin 2.5
Argo - beam weapon|15|Weapons and shields|--scenario-file "$SRC/scripts/promo/argo.json" --clean --sunlit --sun-angle 45 --set /controls/lighting/nav=0 --set /controls/lighting/lamps=0 --set /controls/weapons/primary/fire=1 --cam-yaw 3.7 --cam-pitch 0.25 --cam-distance 90 --cam-spin 2.5'

n=0
: >"$tmp/list.txt"
techno_at=
elapsed=0
echo "$SHOTS" >"$tmp/shots.txt"
while IFS='|' read -r caption secs tag options; do
    n=$((n + 1))
    mkdir -p "$tmp/$n"
    case "$caption" in TECHNO*) caption=${caption#TECHNO }; [ -n "$techno_at" ] || techno_at=$elapsed ;; esac
    text=$(printf '%s' "$caption" | sed 's/:/ -/g')
    tagtext=$(printf '%s' "$tag" | sed 's/:/ -/g')
    head=0
    echo "== shot $n: $caption"
    record=
        [ "$secs" = default ] && record="--record $CLIP"
    case "$secs" in default|auto*) ;; *) record="--record $secs" ;; esac
    (cd "$tmp/$n" && DISPLAY=${DISPLAY:-:0} timeout 900 sh -c "exec \"$KOSMOS\" $record --set /sim/settings/sound=1 --set /sim/settings/volume=100 $options" </dev/null >/dev/null 2>&1) || true
    clip=$(ls "$tmp/$n"/*.mp4 2>/dev/null | head -1)
    [ -n "$clip" ] || { echo "no clip for shot $n" >&2; exit 1; }
    case "$secs" in auto:*)
        keep=${secs#auto:}; head=${keep%,*}; tail=${keep#*,}
        whole=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$clip")
        tstart=$(echo "$whole - $tail" | bc)
        ffmpeg -nostdin -loglevel error -y -i "$clip" -filter_complex "[0:v]split[v1][v2];[0:a]asplit[a1][a2];[v1]trim=0:$(echo "$head + 1" | bc),setpts=PTS-STARTPTS[h];[a1]atrim=0:$(echo "$head + 1" | bc),asetpts=PTS-STARTPTS[ha];[v2]trim=$tstart,setpts=PTS-STARTPTS[t];[a2]atrim=$tstart,asetpts=PTS-STARTPTS[ta];[h][ha][t][ta]concat=n=2:v=1:a=1[v][a]" -map "[v]" -map "[a]" -c:v libx264 -preset fast -crf 16 -c:a aac "$tmp/$n/joined.mp4"
        clip="$tmp/$n/joined.mp4" ;;
    esac
    total=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$clip")
    # drop the first second (loading and settling); caption, fade in and out
    len=$(echo "$total - 1" | bc)
    fade_out=$(echo "$len - 0.7" | bc)
    cap_end=$(echo "$len - 1" | bc)
    launch=$(printf '%s' "$options" | sed -n 's/.*--launch-at \([0-9.]*\).*/\1/p')
    overlay="null"
    if [ -n "$launch" ]; then
        # recording time t+1 is app time; T-minus counts down to the launch, then T-plus counts up until the cut
        overlay="$overlay,drawtext=fontfile=$FONT:text='T-%{eif\\:max(0\\,ceil($launch-t-1))\\:d}':fontsize=h/8:fontcolor=white:borderw=4:bordercolor=black@0.7:x=w-tw-w/25:y=h/12:enable='lt(t+1,$launch)'"
        overlay="$overlay,drawtext=fontfile=$FONT:text='T+%{eif\\:floor(t+1-$launch)\\:d}':fontsize=h/8:fontcolor=white:borderw=4:bordercolor=black@0.7:x=w-tw-w/25:y=h/12:enable='between(t+1,$launch,$head+1)'"
        # the cut after staging jumps ahead in app time, so the clock carries on from where the kept tail begins
        [ -n "$tstart" ] && overlay="$overlay,drawtext=fontfile=$FONT:text='T+%{eif\\:floor($tstart+t-$head-$launch)\\:d}':fontsize=h/8:fontcolor=white:borderw=4:bordercolor=black@0.7:x=w-tw-w/25:y=h/12:enable='gt(t+1,$head+1)'"
    fi
    # keep the game's own sound, or silence if the clip has none
    if ffprobe -v error -select_streams a -show_entries stream=codec_type -of csv=p=0 "$clip" | grep -q audio; then
        audio="-map 1:a:0"
        input2="$clip"
    else
        audio="-map 1:a:0"
        input2="anullsrc=r=48000:cl=stereo"
    fi
    if [ "$input2" = "$clip" ]; then
        ffmpeg -nostdin -loglevel error -y -ss 1 -i "$clip" -ss 1 -i "$clip" -t "$len" \
            -filter_complex "[0:v]scale=${SIZE%x*}:${SIZE#*x},fps=30,drawtext=fontfile=$FONT:text='$text':fontsize=h/18:fontcolor=white:borderw=3:bordercolor=black@0.7:x=(w-tw)/2:y=h-h/8:enable='between(t,0.8,$cap_end)',$overlay,fade=t=in:d=0.7,fade=t=out:st=$fade_out:d=0.7[v];[1:a]aresample=48000,aformat=channel_layouts=stereo,afade=t=in:d=0.5,afade=t=out:st=$fade_out:d=0.7[a]" \
            -map "[v]" -map "[a]" -c:v libx264 -preset medium -crf 18 -pix_fmt yuv420p -c:a aac -b:a 192k "$tmp/c$n.mp4"
    else
        ffmpeg -nostdin -loglevel error -y -ss 1 -i "$clip" -f lavfi -i "anullsrc=r=48000:cl=stereo" -t "$len" \
            -filter_complex "[0:v]scale=${SIZE%x*}:${SIZE#*x},fps=30,drawtext=fontfile=$FONT:text='$text':fontsize=h/18:fontcolor=white:borderw=3:bordercolor=black@0.7:x=(w-tw)/2:y=h-h/8:enable='between(t,0.8,$cap_end)',$overlay,fade=t=in:d=0.7,fade=t=out:st=$fade_out:d=0.7[v]" \
            -map "[v]" -map 1:a -c:v libx264 -preset medium -crf 18 -pix_fmt yuv420p -c:a aac -b:a 192k -shortest "$tmp/c$n.mp4"
    fi
    if [ -n "$launch" ]; then
        # a big ignition boom on top of the game's own thump (recording time t+1 is app time)
        [ -f "$tmp/boom.wav" ] || python3 "$SRC/scripts/promo-boom.py" "$tmp/boom.wav"
        ms=$(echo "($launch - 1) * 1000 / 1" | bc)
        ffmpeg -nostdin -loglevel error -y -i "$tmp/c$n.mp4" -i "$tmp/boom.wav" -filter_complex "[1:a]volume=$BOOM_GAIN,adelay=$ms|$ms[b];[0:a][b]amix=inputs=2:normalize=0,alimiter=limit=0.95[a]" -map 0:v -map "[a]" -c:v copy -c:a aac -b:a 192k "$tmp/c$n.boom.mp4" && mv "$tmp/c$n.boom.mp4" "$tmp/c$n.mp4"
    fi
    elapsed=$(echo "$elapsed + $len" | bc)
    echo "file '$tmp/c$n.mp4'" >>"$tmp/list.txt"
done <"$tmp/shots.txt"

ffmpeg -nostdin -loglevel error -y -f concat -safe 0 -i "$tmp/list.txt" -c copy "$tmp/video.mp4"
total=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$tmp/video.mp4")
[ -n "$techno_at" ] || techno_at=$total

python3 "$SRC/scripts/promo-music.py" "$tmp/music.wav" "$total" "$techno_at"

# The game's sound (engines, weapons) is mixed loud on top of the music; the music ducks under it a little.
ffmpeg -nostdin -loglevel error -y -i "$tmp/video.mp4" -i "$tmp/music.wav" \
    -filter_complex "[0:a]volume=$GAME_GAIN,asplit[g][g2];[1:a]volume=$MUSIC_GAIN[m];[m][g]sidechaincompress=threshold=0.05:ratio=3:attack=20:release=400[md];[md][g2]amix=inputs=2:normalize=0,alimiter=limit=0.95[a]" \
    -map 0:v -map "[a]" -c:v copy -c:a aac -b:a 192k -movflags +faststart "$OUT"
echo "Wrote $OUT ($total s, techno from $techno_at s)"
