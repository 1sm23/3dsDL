"""Beat and downbeat map of a track with Beat This! (CPJKU, MIT).
    .venv-beat/bin/python beatmap.py <audio> <out.json>
Writes {audio, bpm, beats:[s], downbeats:[s], bar:[s per bar]}."""
import json
import sys

import numpy as np
from beat_this.inference import File2Beats

audio, out = sys.argv[1], sys.argv[2]
f2b = File2Beats(checkpoint_path="final0", device="mps", dbn=False)
beats, downbeats = f2b(audio)
beats, downbeats = [float(b) for b in beats], [float(d) for d in downbeats]
ibi = np.diff(beats)
bpm = 60 / float(np.median(ibi))
bars = np.diff(downbeats)
print(f"{audio.split('/')[-1]}: {len(beats)} beats, {len(downbeats)} downbeats, {bpm:.2f} BPM, bar median {np.median(bars):.3f}s, first downbeat {downbeats[0]:.3f}s")
json.dump({"audio": audio, "bpm": bpm, "beats": beats, "downbeats": downbeats}, open(out, "w"), indent=1)
