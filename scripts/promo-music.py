#!/usr/bin/env python3
"""Synthesises the promo soundtrack: an orchestral cue (strings, brass, timpani, cello) that swells into a riser and
then drops into harder, faster techno (140 bpm kick, claps, hats, acid bass, saw lead).

Usage: promo-music.py OUT.wav TOTAL_SECONDS TECHNO_START_SECONDS
Needs numpy and scipy. Deterministic: the noise is seeded."""
import sys

import numpy as np
from scipy import signal

RATE = 48000


def midi(n):
    return 440.0 * 2.0 ** ((np.asarray(n, dtype=float) - 69.0) / 12.0)


def saw(freq, t, detune=0.0):
    phase = np.cumsum(np.full_like(t, freq * (1.0 + detune)) / RATE)
    return 2.0 * (phase % 1.0) - 1.0


def lowpass(x, cutoff, order=2):
    sos = signal.butter(order, min(cutoff, RATE * 0.45), "low", fs=RATE, output="sos")
    return signal.sosfilt(sos, x)


def highpass(x, cutoff, order=2):
    sos = signal.butter(order, cutoff, "high", fs=RATE, output="sos")
    return signal.sosfilt(sos, x)


def envelope(n, attack, release, hold=1.0):
    t = np.arange(n) / RATE
    env = np.minimum(1.0, t / max(attack, 1e-4))
    tail = np.minimum(1.0, (n / RATE - t) / max(release, 1e-4))
    return env * tail * hold


def add(mix, start, samples, gain=1.0, pan=0.0):
    first = int(start * RATE)
    if first >= mix.shape[1] or first + len(samples) <= 0:
        return
    lo = max(0, -first)
    hi = min(len(samples), mix.shape[1] - first)
    chunk = samples[lo:hi] * gain
    left = np.sqrt(0.5 * (1.0 - pan)) * 1.4142
    right = np.sqrt(0.5 * (1.0 + pan)) * 1.4142
    mix[0, first + lo:first + hi] += chunk * left
    mix[1, first + lo:first + hi] += chunk * right


def strings(chord, seconds, level):
    n = int(seconds * RATE)
    t = np.arange(n) / RATE
    out = np.zeros(n)
    for note in chord:
        f = midi(note)
        for detune in (-0.004, 0.0, 0.004):
            out += saw(f, t, detune) * 0.12
        vibrato = 1.0 + 0.0025 * np.sin(2 * np.pi * 5.2 * t)
        out += np.sin(2 * np.pi * f * np.cumsum(vibrato) / RATE) * 0.1
    out = lowpass(out, 1400 + 3500 * level)
    return out * envelope(n, 1.4, 1.2)


def brass(note, seconds, level):
    n = int(seconds * RATE)
    t = np.arange(n) / RATE
    f = midi(note)
    out = saw(f, t, 0.002) + saw(f, t, -0.002) + 0.5 * saw(f * 2, t)
    swell = np.minimum(1.0, t / (seconds * 0.6)) ** 1.5
    out = lowpass(out, 500 + 3000 * float(np.mean(swell)) * level)
    return out * 0.18 * swell * envelope(n, 0.2, 0.6)


def timpani(note, level):
    n = int(1.6 * RATE)
    t = np.arange(n) / RATE
    f = midi(note) * (1.0 + 0.25 * np.exp(-t * 18.0))
    body = np.sin(2 * np.pi * np.cumsum(f) / RATE) * np.exp(-t * 2.8)
    rng = np.random.default_rng(int(note))
    thud = lowpass(rng.standard_normal(n), 400) * np.exp(-t * 30.0) * 3.0
    return (body + thud) * (0.5 + 0.5 * level)


def cello(note, seconds):
    n = int(seconds * RATE)
    t = np.arange(n) / RATE
    out = saw(midi(note), t, 0.001) + saw(midi(note), t, -0.001)
    return lowpass(out, 700) * 0.22 * envelope(n, 0.4, 0.5)


def orchestra(mix, end):
    # A minor, F, C, G: four bars of four seconds, rising in level and ending on a held chord.
    progression = [([57, 60, 64, 69], 45, 33), ([53, 57, 60, 65], 41, 29), ([48, 55, 60, 64], 36, 36), ([55, 59, 62, 67], 43, 31)]
    bar = 4.0
    t = 0.0
    bar_index = 0
    while t < end - 0.05:
        level = min(1.0, 0.25 + t / max(end, 1.0) * 0.9)
        chord, bass, _ = progression[bar_index % 4]
        length = min(bar + 0.4, end - t + 0.4)
        add(mix, t, strings(chord, length, level), 0.55 + 0.45 * level, -0.2)
        add(mix, t, strings([n + 12 for n in chord[1:]], length, level), 0.3 * level, 0.3)
        add(mix, t, cello(bass, length), 0.9)
        if t > 8.0:
            add(mix, t, brass(chord[0], length, level), 0.6 * level, 0.1)
        for beat in range(4):
            if t + beat < end - 0.2 and (bar_index > 0 or beat % 2 == 0):
                add(mix, t + beat, timpani(bass - 12 if beat % 2 == 0 else bass - 7, level), 0.5 + 0.5 * level)
        t += bar
        bar_index += 1


def riser(mix, start, seconds):
    n = int(seconds * RATE)
    t = np.arange(n) / RATE
    rng = np.random.default_rng(7)
    noise = highpass(rng.standard_normal(n), 800)
    sweep = (t / seconds) ** 2.5
    tone = np.sin(2 * np.pi * np.cumsum(220.0 * (1.0 + 7.0 * sweep)) / RATE)
    add(mix, start, (noise * 0.35 + tone * 0.2) * sweep, 0.9)
    for k in range(int(seconds * 4)):
        hit = start + seconds - 0.25 * k
        if hit > start:
            add(mix, hit - 0.25, timpani(45, 1.0), 0.5)


def kick():
    n = int(0.35 * RATE)
    t = np.arange(n) / RATE
    f = 45.0 + 150.0 * np.exp(-t * 38.0)
    body = np.sin(2 * np.pi * np.cumsum(f) / RATE) * np.exp(-t * 9.0)
    click = np.exp(-t * 400.0) * 0.4
    return np.tanh(2.2 * (body + click))


def clap(seed):
    n = int(0.22 * RATE)
    t = np.arange(n) / RATE
    rng = np.random.default_rng(seed)
    noise = highpass(lowpass(rng.standard_normal(n), 6500), 900)
    env = np.exp(-t * 22.0) * (1.0 + 0.8 * (np.sin(t * 2 * np.pi * 90.0) > 0))
    return noise * env * 0.7


def hat(seed, open_hat):
    n = int((0.25 if open_hat else 0.05) * RATE)
    t = np.arange(n) / RATE
    rng = np.random.default_rng(seed)
    return highpass(rng.standard_normal(n), 7000) * np.exp(-t * (14.0 if open_hat else 90.0)) * 0.35


def acid(note, seconds, cutoff):
    n = int(seconds * RATE)
    t = np.arange(n) / RATE
    out = saw(midi(note), t)
    out = lowpass(out, cutoff, 4) * 1.6
    return np.tanh(out) * 0.4 * np.exp(-t * 6.0 / max(seconds, 0.05) * 0.5) * envelope(n, 0.003, 0.03)


def lead(note, seconds):
    n = int(seconds * RATE)
    t = np.arange(n) / RATE
    out = saw(midi(note), t, 0.006) + saw(midi(note), t, -0.006) + saw(midi(note) * 2, t, 0.003) * 0.5
    return lowpass(out, 4200) * 0.16 * envelope(n, 0.005, 0.12)


def techno(mix, start, end):
    bpm = 140.0
    beat = 60.0 / bpm
    step = beat / 4.0
    bass_line = [33, 33, 45, 33, 33, 36, 33, 45, 33, 33, 45, 33, 31, 33, 36, 40]
    arp = [57, 60, 64, 69, 64, 60, 57, 60, 55, 59, 62, 67, 62, 59, 55, 59]
    kick_sample = kick()
    bars = int((end - start) / (beat * 4.0)) + 1
    for bar in range(bars):
        t0 = start + bar * beat * 4.0
        energy = min(1.0, 0.7 + bar * 0.08)
        for b in range(4):
            tb = t0 + b * beat
            if tb >= end:
                continue
            add(mix, tb, kick_sample, 1.0)
            add(mix, tb + beat * 0.5, hat(bar * 4 + b, True), 0.8)
            if b in (1, 3):
                add(mix, tb, clap(b + bar * 7), 0.8)
        for s in range(16):
            ts = t0 + s * step
            if ts >= end:
                continue
            add(mix, ts, hat(100 + s + bar * 16, False), 0.45 + 0.25 * (s % 4 == 2))
            sweep = 300.0 + 2600.0 * (0.5 + 0.5 * np.sin((bar * 16 + s) / 28.0 * np.pi))
            if s % 4 != 0:
                add(mix, ts, acid(bass_line[s], step * 0.95, sweep), 0.9 * energy)
            if bar >= 1 and s % 2 == 0:
                add(mix, ts, lead(arp[s], step * 1.6), 0.7 * energy, -0.3 if s % 4 == 0 else 0.3)


def main():
    out_path = sys.argv[1]
    total = float(sys.argv[2])
    techno_start = float(sys.argv[3])
    mix = np.zeros((2, int(total * RATE)))
    orchestra(mix, techno_start)
    riser(mix, max(0.0, techno_start - 3.0), 3.0)
    techno(mix, techno_start, total)
    fade_in = np.minimum(1.0, np.arange(mix.shape[1]) / (RATE * 2.0))
    fade_out = np.minimum(1.0, (mix.shape[1] - np.arange(mix.shape[1])) / (RATE * 2.5))
    mix *= fade_in * fade_out
    mix /= max(1e-6, np.max(np.abs(mix))) / 0.85
    pcm = (mix.T * 32767.0).astype("<i2")
    import wave

    with wave.open(out_path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(pcm.tobytes())


if __name__ == "__main__":
    main()
