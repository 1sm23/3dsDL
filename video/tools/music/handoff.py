"""Two tracks for the film, blended so the change goes unnoticed: the first
under the opening, the second slipping in underneath and taking over.

    .venv/bin/python handoff.py <first.mp3> <second.mp3> <second-beats.json> <out.wav> [enter] [land] [gone] [bar]

enter: film time the second track starts, filtered and quiet (default 8.0,
       the title drop, whose impact covers it).
land:  film time where the second track's downbeat `bar` falls, by which it
       is fully open (default 12.0, the cut to the app).
gone:  film time the first track has faded out (default 13.0).
bar:   which downbeat of the second track lands (default 2, so it starts near
       its beginning). Any bar works: the track plays in from the bars before
       it. Pick one whose run-in shares a chord with what the first track
       plays mid-blend, so the key change happens through a common chord.

Both tracks are stretched to the film's 120 BPM (beats of exactly 0.5 s) and
their strongest accents lined up, so through the overlap the two grooves
play as one. Like a DJ blend, the first track loses its low end while the
second comes in through a closing low-pass that opens as it rises.
"""
import json
import subprocess
import sys
import tempfile

import librosa
import numpy as np
import soundfile as sf
from scipy.signal import butter, sosfilt, sosfilt_zi

SR, LEN, BEAT = 48000, 78.0, 0.5
first, second, beats_json, out = sys.argv[1:5]
ENTER = float(sys.argv[5]) if len(sys.argv) > 5 else 8.0
LAND = float(sys.argv[6]) if len(sys.argv) > 6 else 12.0
GONE = float(sys.argv[7]) if len(sys.argv) > 7 else 13.0
BAR = int(sys.argv[8]) if len(sys.argv) > 8 else 2


def pulse(path, lo=0.44, hi=0.52):
    """Beat period and the phase of the strongest accents, by comb search
    between lo and hi seconds (a wide window locks onto multiples)."""
    y, sr = librosa.load(path, sr=22050)
    hop = 128
    env = librosa.onset.onset_strength(y=y, sr=sr, hop_length=hop)
    fr = sr / hop
    best = (0, 0, 0)
    for p in np.arange(lo, hi, 0.0005):
        n = int(len(env) / fr / p) - 1
        k = np.arange(n) * p
        for ph in np.arange(0, p, 0.005):
            s = env[((ph + k) * fr).astype(int)].mean()
            if s > best[0]:
                best = (s, p, ph)
    return best[1], best[2]


def load(path, tempo):
    with tempfile.TemporaryDirectory() as folder:
        tmp = f"{folder}/stretched.wav"
        subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", path, "-af", f"atempo={tempo:.6f}",
                        "-ar", str(SR), "-ac", "2", tmp], check=True)
        return sf.read(tmp, dtype="float32")[0]


def sweep(x, kind, f0, f1, t0, t1, at):
    """A filter whose cutoff glides (log) from f0 to f1 between t0 and t1
    (film time; the clip starts at `at`), run in short blocks."""
    out, blk = np.empty_like(x), 256
    zi = None
    for i in range(0, len(x), blk):
        t = at + i / SR
        u = min(1.0, max(0.0, (t - t0) / (t1 - t0)))
        fc = f0 * (f1 / f0) ** u
        sos = butter(2, min(fc, SR / 2 * 0.95), kind, fs=SR, output="sos")
        if zi is None:
            zi = np.stack([sosfilt_zi(sos)] * 2, axis=-1) * x[0]
        out[i:i + blk], zi = sosfilt(sos, x[i:i + blk], axis=0, zi=zi)
    return out


def ramp(n, at, t0, t1, up):
    t = at + np.arange(n) / SR
    u = np.clip((t - t0) / (t1 - t0), 0, 1)
    g = np.sin(u * np.pi / 2) ** 2  # smooth S-curve, no corners
    return (g if up else 1 - g)[:, None]


bm = json.load(open(beats_json))
ibi = float(np.median(np.diff(bm["beats"])))
pa, pha = pulse(first)
pb, phb = pulse(second, ibi * 0.94, ibi * 1.06)  # around its beat-tracked tempo
ra, rb = pa / BEAT, pb / BEAT
a, b = load(first, ra), load(second, rb)
pha, phb = pha / ra, phb / rb  # accent phase in the stretched audio

# The second track placed so its downbeat BAR falls on LAND: b_at is the
# film time of the track's own time zero (negative when it enters mid-song).
down = np.array(bm["downbeats"]) / rb
b_at = LAND - down[BAR]
# The first track shifted (by under a beat) so its accents fall on the second's.
shift = ((b_at + phb) - pha) % BEAT - BEAT  # film time where the first track's audio starts, <= 0
film = np.zeros((int(LEN * SR), 2), dtype="float32")

ia = int(-shift * SR)
na = int(GONE * SR)
A = a[ia:ia + na].copy()
A = sweep(A, "highpass", 20, 700, ENTER + 1.0, GONE - 0.5, 0.0)
A *= ramp(len(A), 0.0, ENTER + 1.0, GONE, up=False)
film[:len(A)] += A

b_from = max(b_at, ENTER - 0.5)  # film time the second track's audio begins
B = b[int((b_from - b_at) * SR):][:len(film) - int(b_from * SR)].copy()
if b_from + len(B) / SR < LEN - 0.5:
    sys.exit(f"second track runs out at {b_from + len(B) / SR:.1f} s: pick an earlier bar")
B = sweep(B, "lowpass", 350, 20000, ENTER, LAND + 0.5, b_from)
B *= ramp(len(B), b_from, ENTER, LAND, up=True)
i = int(b_from * SR)
film[i:i + len(B)] += B

n = int(3.0 * SR)  # a slow fade over the last three seconds, as the lid shuts
film[-n:] *= (np.cos(np.linspace(0, np.pi / 2, n)) ** 2)[:, None]
sf.write(out, film, SR, subtype="PCM_24")
print(f"first {60 / pa:.2f} BPM x{ra:.4f}, second {60 / pb:.2f} BPM x{rb:.4f}; second's bar {BAR} "
      f"(at {down[BAR] * rb:.2f} s in the original) on {LAND:.2f} s; first shifted {shift:.3f} s to match accents")
