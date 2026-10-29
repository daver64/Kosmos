#!/bin/sh
# Makes the promo video: records a clip of each shot below with Kosmos' own --record (so the game's engine and weapon
# sound is on the clip), then joins them with captions and fades over a synthesised soundtrack: an orchestral cue for
# the flight and base shots that drops into techno for the sci-fi weapons shots (scripts/promo-music.py).
# Needs a display, ffmpeg with drawtext, python3 with numpy and scipy, and build/kosmos.
# Usage: make-promo.sh [OUT.mp4]      run from anywhere; default output is build/kosmos-promo.mp4
# Overrides (env): CLIP (seconds per clip, default 9), SIZE (output size, default 1280x720), KOSMOS (path to the executable),
#                  GAME_GAIN and MUSIC_GAIN (mix levels, default 1.6 and 0.55: the engine stays loud over the music)
# Edit the SHOTS list to change the reel. Each line is: caption|seconds|kosmos options. A Deltaglider that is not on the ground
# gets its gear up by itself. Seconds "auto" leaves the length to the options (--record N --record-after-stage S).
# The first shot whose caption starts with "TECHNO" is where the music changes; captions cannot contain colons.
set -e
SRC=$(cd "$(dirname "$0")/.." && pwd)
KOSMOS=${KOSMOS:-$SRC/build/kosmos}
CLIP=${CLIP:-9}
SIZE=${SIZE:-1280x720}
GAME_GAIN=${GAME_GAIN:-1.6}
MUSIC_GAIN=${MUSIC_GAIN:-0.55}
OUT=${1:-$(dirname "$KOSMOS")/kosmos-promo.mp4}
FONT=${FONT:-/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

SHOTS='Kosmos - a spaceflight simulator|default|--scenario 07_dragonfly_orbit --clean --cam-yaw 60 --cam-pitch 15 --cam-distance 55 --cam-spin 6 --cam-tilt 6
Real rockets - the Apollo 11 Saturn V|auto|--scenario 01_apollo11_saturn_v_t_minus_5 --clean --auto-stage --shot-throttle 1 --set /controls/throttle=1 --set /sim/warp-index=3 --cam-yaw 40 --cam-pitch 10 --cam-distance 500 --cam-spin 3 --record 400 --record-after-stage 10
Fly the Deltaglider|default|--ship "CVEL Deltaglider" --orbit --clean --sunlit --cam-yaw 70 --cam-pitch 10 --cam-distance 36 --cam-spin 7 --cam-tilt 8
Dock, transfer, arrive - Mars departure|default|--scenario 13_dragonfly_deltaglider_mars --clean --cam-yaw 40 --cam-pitch 12 --cam-distance 50 --cam-spin -6
Step out and walk into the base|default|--scenario 14_mare_quarters_on_foot --clean --interior-at 5.5 3.5 0 0
Lamps, windows and live life support|default|--scenario 14_mare_quarters_on_foot --clean --interior-at 3.2 4.0 90 -5
TECHNO Beams and sparkles|10|--ship "CVEL Deltaglider" --orbit --alone --clean --sunlit --set /controls/weapons/primary/fire=1 --set /controls/weapons/secondary/fire=1 --cam-yaw 150 --cam-pitch 8 --cam-distance 220 --cam-spin 5 --cam-tilt 10
Argo - beam weapon|15|--ship "Argo" --orbit --alone --clean --sunlit --set /controls/lighting/nav=0 --set /controls/lighting/lamps=0 --set /controls/weapons/primary/fire=1 --cam-yaw 95 --cam-pitch 6 --cam-distance 140 --cam-spin 4 --cam-tilt 8'

n=0
: >"$tmp/list.txt"
techno_at=
elapsed=0
echo "$SHOTS" >"$tmp/shots.txt"
while IFS='|' read -r caption secs options; do
    n=$((n + 1))
    mkdir -p "$tmp/$n"
    case "$caption" in TECHNO*) caption=${caption#TECHNO }; [ -n "$techno_at" ] || techno_at=$elapsed ;; esac
    text=$(printf '%s' "$caption" | sed 's/:/ -/g')
    echo "== shot $n: $caption"
    record=
    [ "$secs" = auto ] || record="--record ${secs#default}"
    [ "$secs" = default ] && record="--record $CLIP"
    [ "$secs" != default ] && [ "$secs" != auto ] && record="--record $secs"
    (cd "$tmp/$n" && DISPLAY=${DISPLAY:-:0} timeout 900 sh -c "exec \"$KOSMOS\" $record $options" </dev/null >/dev/null 2>&1) || true
    clip=$(ls "$tmp/$n"/*.mp4 2>/dev/null | head -1)
    [ -n "$clip" ] || { echo "no clip for shot $n" >&2; exit 1; }
    total=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$clip")
    # drop the first second (loading and settling); caption, fade in and out
    len=$(echo "$total - 1" | bc)
    fade_out=$(echo "$len - 0.7" | bc)
    cap_end=$(echo "$len - 1" | bc)
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
            -filter_complex "[0:v]scale=${SIZE%x*}:${SIZE#*x},fps=30,drawtext=fontfile=$FONT:text='$text':fontsize=h/18:fontcolor=white:borderw=3:bordercolor=black@0.7:x=(w-tw)/2:y=h-h/8:enable='between(t,0.8,$cap_end)',fade=t=in:d=0.7,fade=t=out:st=$fade_out:d=0.7[v];[1:a]aresample=48000,aformat=channel_layouts=stereo,afade=t=in:d=0.5,afade=t=out:st=$fade_out:d=0.7[a]" \
            -map "[v]" -map "[a]" -c:v libx264 -preset medium -crf 18 -pix_fmt yuv420p -c:a aac -b:a 192k "$tmp/c$n.mp4"
    else
        ffmpeg -nostdin -loglevel error -y -ss 1 -i "$clip" -f lavfi -i "anullsrc=r=48000:cl=stereo" -t "$len" \
            -filter_complex "[0:v]scale=${SIZE%x*}:${SIZE#*x},fps=30,drawtext=fontfile=$FONT:text='$text':fontsize=h/18:fontcolor=white:borderw=3:bordercolor=black@0.7:x=(w-tw)/2:y=h-h/8:enable='between(t,0.8,$cap_end)',fade=t=in:d=0.7,fade=t=out:st=$fade_out:d=0.7[v]" \
            -map "[v]" -map 1:a -c:v libx264 -preset medium -crf 18 -pix_fmt yuv420p -c:a aac -b:a 192k -shortest "$tmp/c$n.mp4"
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
