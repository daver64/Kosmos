#!/usr/bin/env python3
"""Generate an original, deterministic instrumental cue for the beta reel."""
import math
import sys
import wave

import numpy as np

RATE = 48000
BPM = 124.0
BEAT = 60.0 / BPM


def main(path, duration):
    n = round(duration * RATE)
    mix = np.zeros((n, 2), dtype=np.float32)
    rng = np.random.default_rng(7462)

    def add(start, sound, gain=1.0, pan=0.0):
        begin = max(0, round(start * RATE))
        end = min(n, begin + len(sound))
        if end > begin:
            mix[begin:end, 0] += sound[:end - begin] * gain * math.sqrt(1 - pan)
            mix[begin:end, 1] += sound[:end - begin] * gain * math.sqrt(1 + pan)

    def tone(note, length, decay):
        t = np.arange(round(length * RATE), dtype=np.float32) / RATE
        hz = 440.0 * 2.0 ** ((note - 69) / 12.0)
        env = (1 - np.exp(-t * 35)) * np.exp(-t * decay)
        return env * (np.sin(2 * np.pi * hz * t) + 0.3 * np.sin(2 * np.pi * hz * 2.01 * t))

    t = np.arange(round(0.30 * RATE), dtype=np.float32) / RATE
    kick = np.sin(2 * np.pi * (46 * t + 2.6 * (1 - np.exp(-t * 25)))) * np.exp(-t * 14)
    t = np.arange(round(0.12 * RATE), dtype=np.float32) / RATE
    snare = rng.standard_normal(len(t)).astype(np.float32) * np.exp(-t * 30)
    t = np.arange(round(0.045 * RATE), dtype=np.float32) / RATE
    hat = rng.standard_normal(len(t)).astype(np.float32) * np.exp(-t * 95)
    chords = ((45, 52, 57, 60), (41, 48, 53, 57), (48, 55, 60, 64), (43, 50, 55, 59))
    for beat in range(math.ceil(duration / BEAT)):
        start = beat * BEAT
        chord = chords[(beat // 4) % len(chords)]
        if beat % 4 == 0:
            for note in chord:
                add(start, tone(note, BEAT * 4.2, 0.7), 0.026)
        add(start, kick, 0.19)
        if beat % 4 in (1, 3):
            add(start, snare, 0.055)
        for half in range(2):
            at = start + half * BEAT / 2
            add(at, hat, 0.025, -0.3 if half else 0.3)
            note = chord[(beat * 2 + half) % len(chord)] + 12
            add(at, tone(note, BEAT * 0.65, 5.0), 0.055, -0.35 if half else 0.35)
    fade = min(round(RATE * 1.5), n // 2)
    mix[:fade] *= np.linspace(0, 1, fade, dtype=np.float32)[:, None]
    mix[-fade:] *= np.linspace(1, 0, fade, dtype=np.float32)[:, None]
    peak = np.max(np.abs(mix))
    if peak > 0.85:
        mix *= 0.85 / peak
    pcm = (np.clip(mix, -1, 1) * 32767).astype("<i2")
    with wave.open(path, "wb") as out:
        out.setnchannels(2)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(pcm.tobytes())


if __name__ == "__main__":
    main(sys.argv[1], float(sys.argv[2]))
