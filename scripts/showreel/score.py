#!/usr/bin/env python3
"""Generate the original 90-second score and a separate effects stem."""

import argparse
import importlib.util
import math
from pathlib import Path

import numpy as np
from scipy import signal
from scipy.io import wavfile

ROOT = Path(__file__).resolve().parents[2]
RATE = 48000
DURATION = 90
BEAT = 60.0 / 88.0
spec = importlib.util.spec_from_file_location("promo_music", ROOT / "scripts/promo-music.py")
music = importlib.util.module_from_spec(spec)
spec.loader.exec_module(music)


def piano(note, seconds):
    t = np.arange(int(seconds * RATE), dtype=np.float64) / RATE
    frequency = music.midi(note)
    tone = (np.sin(2 * np.pi * frequency * t) +
            0.26 * np.sin(2 * np.pi * 2 * frequency * t) +
            0.1 * np.sin(2 * np.pi * 3 * frequency * t))
    return tone * np.minimum(1, t / 0.012) * np.exp(-t * 1.8)


def volume_at(time):
    return np.interp(time, [0, 10, 12, 28, 43, 57, 64, 71, 76, 84, 90],
                     [0.20, 0.30, 0.65, 0.52, 0.62, 0.80, 1.0, 0.48, 0.58, 0.35, 0.0])


def score():
    mix = np.zeros((2, DURATION * RATE), dtype=np.float32)
    chords = [[50, 53, 57, 62], [46, 50, 53, 58], [48, 53, 57, 60], [48, 52, 55, 60]]
    bar = 4 * BEAT
    for i in range(math.ceil(DURATION / bar)):
        start = i * bar
        level = volume_at(start)
        chord = chords[(i // 2) % len(chords)]
        music.add(mix, start, music.strings(chord, min(bar + 0.7, DURATION - start + 0.1), level),
                  0.12 + level * 0.15, -0.15)
        if start >= 12 and start < 82:
            music.add(mix, start, music.cello(chord[0] - 12, min(bar + 0.4, DURATION - start)),
                      0.20 * level, 0.15)
        if 28 <= start < 75:
            music.add(mix, start, music.brass(chord[1] + 12, min(bar, DURATION - start), level),
                      0.12 * level, 0.22)
        if 12 <= start < 75:
            for beat in (0, 2):
                music.add(mix, start + beat * BEAT, music.timpani(chord[0] - 12, level),
                          0.12 * level)
    motif = [74, 69, 65, 69, 72, 69, 65, 62]
    for start in (2, 29, 45, 59, 76):
        for i, note in enumerate(motif):
            at = start + i * BEAT
            gain = 0.10 if start == 2 else 0.15 * volume_at(at)
            music.add(mix, at, piano(note, 2.4), gain, -0.22 + 0.06 * (i % 5))
            if 59 <= at < 70:
                music.add(mix, at + BEAT / 2, piano(note + 12, 1.2), gain * 0.35, 0.35)
    end = np.minimum(1.0, np.maximum(0.0, (DURATION - np.arange(mix.shape[1]) / RATE) / 4))
    mix *= end
    return mix


def sample(path):
    rate, data = wavfile.read(path)
    data = data.astype(np.float32) / np.iinfo(data.dtype).max
    if data.ndim == 1:
        data = np.column_stack((data, data))
    if rate != RATE:
        divisor = math.gcd(rate, RATE)
        data = signal.resample_poly(data, RATE // divisor, rate // divisor, axis=0)
    return data.T


def add_stereo(target, start, samples, gain):
    offset = int(start * RATE)
    count = min(samples.shape[1], target.shape[1] - offset)
    if count > 0:
        target[:, offset:offset + count] += samples[:, :count] * gain


def effects():
    mix = np.zeros((2, DURATION * RATE), dtype=np.float32)
    rng = np.random.default_rng(20261114)
    wind = signal.sosfilt(signal.butter(2, 260, fs=RATE, output="sos"), rng.standard_normal(12 * RATE))
    t = np.arange(len(wind)) / RATE
    wind *= np.minimum(1, t / 3) * np.minimum(1, (12 - t) / 2)
    music.add(mix, 0, wind, 0.018)
    add_stereo(mix, 12.5, sample(ROOT / "data/sounds/liftoff.wav"), 0.26)
    engine = sample(ROOT / "data/sounds/main_engines.wav")
    for start in np.arange(12.5, 27.5, engine.shape[1] / RATE):
        add_stereo(mix, float(start), engine[:, :min(engine.shape[1], int((28 - start) * RATE))], 0.16)
    duration = 3.2
    t = np.arange(int(duration * RATE)) / RATE
    noise = signal.sosfilt(signal.butter(2, 800, "high", fs=RATE, output="sos"), rng.standard_normal(len(t)))
    envelope = np.minimum(1, t / 0.8) * np.maximum(0, 1 - t / duration) ** 1.3
    sweep = np.sin(2 * np.pi * np.cumsum(100 + 1800 * (t / duration) ** 2) / RATE)
    music.add(mix, 63.7, (noise * 0.08 + sweep * 0.09) * envelope, 0.7)
    return mix


def write(path, samples):
    if path.exists():
        raise FileExistsError(f"Audio output already exists: {path}")
    wavfile.write(path, RATE, (np.clip(samples.T, -0.98, 0.98) * 32767).astype(np.int16))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("soundtrack", type=Path)
    parser.add_argument("--score", type=Path, required=True)
    parser.add_argument("--effects", type=Path, required=True)
    args = parser.parse_args()
    music_stem = score()
    effects_stem = effects()
    combined = music_stem + effects_stem
    peak = max(float(np.max(np.abs(combined))), float(np.max(np.abs(music_stem))),
               float(np.max(np.abs(effects_stem))))
    if peak > 0:
        gain = 0.9 / peak
        music_stem *= gain
        effects_stem *= gain
        combined *= gain
    write(args.score, music_stem)
    write(args.effects, effects_stem)
    write(args.soundtrack, combined)


if __name__ == "__main__":
    main()
