#!/usr/bin/env python3
"""Synthesises the banner jingle the HOME Menu plays when 3dsDL is selected.

An original little tune, timed to the banner animation: a quick rising
arpeggio while the arrow falls, a soft bop when it lands in the card at
0.7 s, and three sparkle chimes as the stars burst. About 1.6 seconds of
44.1 kHz 16-bit stereo, the format of the silent clip 3dsDL's banner used
before (banner sound must be stereo and at most 3 seconds).

Usage: python3 jingle.py jingle.wav
"""

import math
import struct
import sys
import wave

RATE = 44100
LENGTH = 1.6


def tone(buffer, start, duration, freq, volume, kind="triangle", end_freq=None, decay=6.0):
    """Adds one note: a pitch (optionally gliding) with a fast attack and an
    exponential decay."""
    first = int(start * RATE)
    count = int(duration * RATE)
    phase = 0.0
    for i in range(count):
        t = i / RATE
        f = freq if end_freq is None else freq * (end_freq / freq) ** (i / count)
        phase += 2 * math.pi * f / RATE
        if kind == "triangle":
            wave_value = 2 / math.pi * math.asin(math.sin(phase))
        elif kind == "bell":
            # A bell: the fundamental plus an inharmonic partial, kept
            # below the Nyquist limit of the low sample rate.
            wave_value = math.sin(phase) + (0.3 * math.sin(phase * 2.76) if f * 2.76 < RATE * 0.45 else 0.0)
        else:
            wave_value = math.sin(phase)
        attack = min(1.0, t / 0.004)
        release = min(1.0, (count - i) / (0.01 * RATE))
        envelope = attack * release * math.exp(-decay * t)
        if first + i < len(buffer):
            buffer[first + i] += volume * envelope * wave_value


def main(path):
    samples = [0.0] * int(LENGTH * RATE)
    # Rising arpeggio while the arrow falls (G5, C6, E6, G6).
    for k, freq in enumerate((784.0, 1046.5, 1318.5)):
        tone(samples, 0.06 + k * 0.085, 0.11, freq, 0.32, decay=10)
    tone(samples, 0.315, 0.3, 1568.0, 0.3, decay=7)
    # The arrow lands: a soft bop that drops in pitch.
    tone(samples, 0.7, 0.16, 560.0, 0.5, kind="sine", end_freq=250.0, decay=14)
    # Sparkles: three chimes, each a little higher and quieter.
    for k, freq in enumerate((2093.0, 2637.0, 3136.0)):
        tone(samples, 0.95 + k * 0.1, 0.5, freq, 0.16 - k * 0.03, kind="bell", decay=7)

    peak = max(abs(s) for s in samples) or 1.0
    gain = 0.5 / peak
    with wave.open(path, "wb") as out:
        out.setnchannels(2)
        out.setsampwidth(2)
        out.setframerate(RATE)
        frames = bytearray()
        for s in samples:
            value = int(max(-1.0, min(1.0, s * gain)) * 32767)
            frames += struct.pack("<hh", value, value)
        out.writeframes(bytes(frames))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
