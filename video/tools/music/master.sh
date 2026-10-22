#!/usr/bin/env bash
# Mixes a fitted music track with the film's SFX stem and masters the result
# for YouTube: music at -18 LUFS under the effects, then the mix two-pass
# loudness-normalised to -14 LUFS integrated, -2 dBTP (headroom for AAC encoding), 48 kHz.
#   tools/music/master.sh <music.wav> <out.wav> [sfx.wav]
set -euo pipefail
music="$1"; out="$2"; sfx="${3:-assets/music/sfx.wav}"
tmp="$(mktemp -d)"
ffmpeg -loglevel error -y -i "$music" -af loudnorm=I=-18:TP=-2:LRA=11 -ar 48000 "$tmp/m.wav"
ffmpeg -loglevel error -y -i "$tmp/m.wav" -i "$sfx" -filter_complex "[0][1]amix=inputs=2:normalize=0:duration=first" -ar 48000 "$tmp/mix.wav"
stats="$(ffmpeg -hide_banner -i "$tmp/mix.wav" -af loudnorm=I=-14:TP=-2:LRA=11:print_format=json -f null - 2>&1 | sed -n '/^{/,/^}/p')"
val() { echo "$stats" | python3 -c "import json,sys;print(json.load(sys.stdin)['$1'])"; }
ffmpeg -loglevel error -y -i "$tmp/mix.wav" -af "loudnorm=I=-14:TP=-2:LRA=11:measured_I=$(val input_i):measured_TP=$(val input_tp):measured_LRA=$(val input_lra):measured_thresh=$(val input_thresh):offset=$(val target_offset):linear=true,aresample=48000" -c:a pcm_s24le "$out"
ffmpeg -hide_banner -i "$out" -af ebur128=peak=true -f null - 2>&1 | grep -E "^\s+(I|Peak):" | tr -s ' ' | tr '\n' ' '; echo
rm -rf "$tmp"
