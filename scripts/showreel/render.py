#!/usr/bin/env python3
"""Render the Kosmos showreel from the editable timeline and real simulator frames."""

import argparse
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
FONT = ROOT / "assets/fonts/DejaVuSansMono.ttf"


def run(command, *, env=None, cwd=ROOT):
    result = subprocess.run([str(x) for x in command], cwd=cwd, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if result.returncode:
        raise RuntimeError(f"Command failed ({result.returncode}): {command[0]}\n{result.stdout[-12000:]}")
    return result.stdout


def ffmpeg(*args):
    return run(["ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error",
                "-threads", "4", "-filter_threads", "4", "-filter_complex_threads", "4", *args])


def escaped(path):
    return str(path).replace("\\", "\\\\").replace(":", "\\:").replace("'", "\\'")


def caption(text_path, width, height, seconds):
    font = escaped(FONT)
    text = escaped(text_path)
    size = max(15, height // 28)
    fade = min(0.9, seconds / 4)
    end = seconds - fade
    alpha = f"if(lt(t\\,{fade})\\,t/{fade}\\,if(gt(t\\,{end})\\,({seconds}-t)/{fade}\\,1))"
    return (f"drawtext=fontfile='{font}':textfile='{text}':fontcolor=white:"
            f"fontsize={size}:shadowcolor=black@0.8:shadowx=2:shadowy=2:"
            f"x=(w-text_w)/2:y=h*0.81:alpha='{alpha}'")


def capture(binary, shot, fps, width, height, frames, scratch, env, survey=False):
    output = scratch / "frames"
    args = [binary, "--root", ROOT, "--clean", "--mfd", "0", "--no-clutter", "--link-port", "0",
            "--gen-threads", "1", "--date", shot.get("date", "2024-06-21 15:00:00"),
            *shot["args"], "--capture-frames", output, fps, frames, width, height]
    warmup = shot.get("warmup", 1)
    if warmup:
        args += ["--capture-warmup", int(round(warmup * fps))]
    if "camera" in shot:
        camera = scratch / "camera.json"
        camera.write_text(json.dumps(shot["camera"]), encoding="utf-8")
        args += ["--capture-camera", camera]
    run(args, env=env)
    captured = sorted(output.glob("frame-*.png"))
    if len(captured) != frames:
        raise RuntimeError(f"{shot['id']}: expected {frames} frames, found {len(captured)}")
    return captured[0] if survey else output


def encode_shot(shot, frames, clip, fps, width, height, scratch):
    filters = []
    if shot.get("fade_in"):
        filters.append("fade=t=in:st=0:d=0.8")
    if "title" in shot:
        text = scratch / "title.txt"
        text.write_text(shot["title"], encoding="utf-8")
        filters.append(caption(text, width, height, shot["seconds"]))
    args = ["-framerate", str(fps), "-i", frames / "frame-%06d.png"]
    if filters:
        args += ["-vf", ",".join(filters)]
    ffmpeg(*args, "-frames:v", str(shot["seconds"] * fps), "-an",
           "-c:v", "libx264", "-threads", "4", "-preset", "medium",
           "-crf", "18", "-pix_fmt", "yuv420p", "-n", clip)


def encode_card(shot, clip, fps, width, height, scratch):
    lines = [
        ("KOSMOS", 0.29, 0.105),
        ("THE UNIVERSE IS YOURS TO EXPLORE", 0.48, 0.035),
        ("FREE SPACE FLIGHT SIMULATOR", 0.65, 0.028),
        ("WINDOWS & LINUX", 0.72, 0.028),
        ("salyut.ddns.net/kosmos", 0.83, 0.025),
    ]
    filters = []
    for i, (line, y, scale) in enumerate(lines):
        text = scratch / f"card-{i}.txt"
        text.write_text(line, encoding="utf-8")
        filters.append(f"drawtext=fontfile='{escaped(FONT)}':textfile='{escaped(text)}':"
                       f"fontcolor=white:fontsize={max(14, round(height * scale))}:"
                       f"x=(w-text_w)/2:y=h*{y}")
    filters.extend(["fade=t=in:st=0:d=1", f"fade=t=out:st={shot['seconds'] - 1}:d=1"])
    ffmpeg("-f", "lavfi", "-i", f"color=c=black:s={width}x{height}:r={fps}",
           "-vf", ",".join(filters), "-frames:v", str(shot["seconds"] * fps),
           "-an", "-c:v", "libx264", "-threads", "4", "-preset", "medium",
           "-crf", "18", "-pix_fmt", "yuv420p", "-n", clip)


def verify(path, fps, width, height, seconds):
    info = json.loads(run(["ffprobe", "-v", "error", "-count_frames", "-show_streams",
                           "-show_format", "-of", "json", path]))
    video = next((s for s in info["streams"] if s["codec_type"] == "video"), None)
    audio = next((s for s in info["streams"] if s["codec_type"] == "audio"), None)
    if not video or not audio:
        raise RuntimeError("Finished reel is missing video or audio")
    if (video["codec_name"] != "h264" or video["width"] != width or video["height"] != height or
            video["r_frame_rate"] != f"{fps}/1" or int(video["nb_read_frames"]) != seconds * fps):
        raise RuntimeError(f"Unexpected video stream: {video}")
    if audio["codec_name"] != "aac" or int(audio["sample_rate"]) != 48000 or audio["channels"] != 2:
        raise RuntimeError(f"Unexpected audio stream: {audio}")
    if abs(float(info["format"]["duration"]) - seconds) > 0.1:
        raise RuntimeError(f"Unexpected duration: {info['format']['duration']}")


def copy_credits(output):
    sources = {
        "CREDITS-models.md": ROOT / "data/CREDITS-models.md",
        "CREDITS-textures.md": ROOT / "data/CREDITS-textures.md",
        "CREDITS-sounds.md": ROOT / "data/CREDITS-sounds.md",
        "LICENSE-XRSound.txt": ROOT / "data/sounds/LICENSE-XRSound.txt",
        "LICENSE-Ares-Spider.txt": ROOT / "addons/cvel-ares/LICENSE-Ares-Spider.txt",
    }
    for name, source in sources.items():
        target = output / name
        if target.exists() and target.read_bytes() != source.read_bytes():
            raise FileExistsError(f"Different credits already exist: {target}")
        if not target.exists():
            shutil.copyfile(source, target)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--survey", action="store_true", help="capture one composition check per shot")
    mode.add_argument("--preview", action="store_true", help="render a 640x360, 10 fps preview")
    mode.add_argument("--final", action="store_true", help="render the 1920x1080, 30 fps master")
    parser.add_argument("--output", type=Path, default=ROOT / "build/showreel")
    parser.add_argument("--binary", type=Path, default=ROOT / "build/kosmos")
    parser.add_argument("--timeline", type=Path, default=HERE / "timeline.json")
    parser.add_argument("--only", help="comma-separated shot IDs to survey")
    args = parser.parse_args()
    output, binary = args.output.resolve(), args.binary.resolve()
    timeline = json.loads(args.timeline.read_text(encoding="utf-8"))
    shots = timeline["shots"]
    if sum(s["seconds"] for s in shots) != 90 or timeline["fps"] != 30:
        raise ValueError("The showreel timeline must total 90 seconds at 30 fps")
    if args.only:
        if not args.survey:
            raise ValueError("--only is available with --survey")
        wanted = set(args.only.split(","))
        if not wanted <= {s["id"] for s in shots if not s.get("card")}:
            raise ValueError("--only contains an unknown shot ID")
        shots = [s for s in shots if s["id"] in wanted]
    if not binary.is_file() or not FONT.is_file():
        raise FileNotFoundError("Kosmos executable or bundled font not found")
    if not args.survey:
        for program in ("ffmpeg", "ffprobe"):
            if not shutil.which(program):
                raise FileNotFoundError(f"{program} was not found on PATH")
        for module in ("numpy", "scipy"):
            if importlib.util.find_spec(module) is None:
                raise RuntimeError(f"Python module {module} is required")
        for source in (ROOT / "scripts/promo-music.py", ROOT / "data/sounds/liftoff.wav",
                       ROOT / "data/sounds/main_engines.wav"):
            if not source.is_file():
                raise FileNotFoundError(f"Missing soundtrack input: {source}")
    output.mkdir(parents=True, exist_ok=True)
    fps, width, height = (30, 1920, 1080) if args.final else (10, 640, 360)
    name = "final" if args.final else "preview"
    result = output / f"kosmos-beyond-the-horizon-{name}.mp4"
    clips = output / f"{name}-clips"
    survey = output / "survey"
    if args.survey:
        survey.mkdir(exist_ok=True)
        planned = [survey / f"{s['id']}.png" for s in shots if not s.get("card")]
    else:
        clips.mkdir(exist_ok=True)
        planned = [result, *(clips / f"{i:02d}-{s['id']}.mp4" for i, s in enumerate(shots)),
                   output / f"{name}-score.wav", output / f"{name}-effects.wav"]
    if any(path.exists() for path in planned):
        raise FileExistsError("Output already exists; choose a fresh --output directory")
    if not args.survey:
        copy_credits(output)
    env = dict(os.environ, SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy",
               LP_NUM_THREADS="4", OMP_NUM_THREADS="4", OPENBLAS_NUM_THREADS="4",
               MKL_NUM_THREADS="4")
    with tempfile.TemporaryDirectory(prefix="kosmos-reel-", dir=output) as temp:
        work = Path(temp)
        home = work / "home"
        home.mkdir()
        env.update(HOME=str(home), XDG_CONFIG_HOME=str(home / "config"), XDG_CACHE_HOME=str(home / "cache"))
        for i, shot in enumerate(shots):
            if shot.get("card") and args.survey:
                continue
            print(f"{i + 1}/{len(shots)} {shot['id']}", flush=True)
            with tempfile.TemporaryDirectory(prefix="shot-", dir=work) as shot_temp:
                scratch = Path(shot_temp)
                if shot.get("card"):
                    encode_card(shot, clips / f"{i:02d}-{shot['id']}.mp4", fps, width, height, scratch)
                elif args.survey:
                    frame = capture(binary, shot, fps, width, height, 1, scratch, env, survey=True)
                    shutil.copyfile(frame, survey / f"{shot['id']}.png")
                else:
                    frames = capture(binary, shot, fps, width, height, shot["seconds"] * fps, scratch, env)
                    encode_shot(shot, frames, clips / f"{i:02d}-{shot['id']}.mp4", fps, width, height, scratch)
        if args.survey:
            print(f"Survey images: {survey}")
            return
        sequence = work / "clips.txt"
        sequence.write_text("".join(f"file '{str(p).replace(chr(39), chr(39) + chr(92) + chr(39) + chr(39))}'\n"
                                    for p in planned[1:1 + len(shots)]), encoding="utf-8")
        joined = work / "video.mp4"
        ffmpeg("-f", "concat", "-safe", "0", "-i", sequence, "-c", "copy", "-n", joined)
        soundtrack = work / "soundtrack.wav"
        run([sys.executable, HERE / "score.py", soundtrack,
             "--score", output / f"{name}-score.wav",
             "--effects", output / f"{name}-effects.wav"], env=env)
        license_text = (ROOT / "data/sounds/LICENSE-XRSound.txt").read_text(
            encoding="utf-8", errors="replace").split("====")[0].strip()
        ffmpeg("-i", joined, "-i", soundtrack, "-map", "0:v:0", "-map", "1:a:0",
               "-c:v", "copy", "-c:a", "aac", "-b:a", "192k", "-ar", "48000",
               "-metadata", "title=Kosmos - Beyond the Horizon",
               "-metadata", f"comment=Simulator asset credits are included alongside the video. "
                            f"Sound effects from XRSound 3.0:\n{license_text}",
               "-t", "90", "-movflags", "+faststart", "-n", result)
        verify(result, fps, width, height, 90)
        print(f"Verified {result}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, KeyError) as error:
        print(f"showreel: {error}", file=sys.stderr)
        sys.exit(1)
