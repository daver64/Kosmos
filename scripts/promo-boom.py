#!/usr/bin/env python3
"""Write a big engine-ignition boom (a pitch-dropping sub thump, a noise whoomp and a crack) as a stereo WAV: promo-boom.py OUT.wav"""
import math
import random
import struct
import sys
import wave

rate = 48000
seconds = 5.0
random.seed(7)
low = brown = phase = phase2 = 0.0
frames = bytearray()
for i in range(int(rate * seconds)):
    t = i / rate
    f = 22.0 + 48.0 * math.exp(-t * 2.2)
    phase += 2 * math.pi * f / rate
    phase2 += 2 * math.pi * f * 2.01 / rate
    body = min(1.0, t * 60.0) * math.exp(-t * 0.9)
    thump = (math.sin(phase) + 0.5 * math.sin(phase2) * math.exp(-t * 1.5)) * body
    n = random.uniform(-1, 1)
    low += 0.04 * (n - low)
    brown += 0.01 * (low - brown)
    whoomp = low * 3.0 * min(1.0, t * 20.0) * math.exp(-t * 1.1)
    crack = n * math.exp(-t * 25.0) * 0.5
    v = max(-1.0, min(1.0, math.tanh((thump * 1.4 + whoomp + crack) * 1.6)))
    s = struct.pack("<h", int(v * 32000))
    frames += s + s
with wave.open(sys.argv[1], "wb") as w:
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(rate)
    w.writeframes(bytes(frames))
