#!/usr/bin/env bash
# Renders the film's two console shots at 3840 x 2160, 60 fps, into
# .frames/ (a fresh Blender every 120 frames: a long session degrades), then
# encodes assets/footage/console-open.webm (8 s) and console-close.webm (3 s)
# with alpha. Re-running resumes; delete .frames/ to start over.
#   ./render-console.sh [step]    step 2 = every other frame, a quick 30 fps preview
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; cd "$here"
step="${1:-1}"; frames=".frames"; out="$here/../../assets/footage"
for start in $(seq 0 120 659); do
  end=$((start + 119)); [ "$end" -gt 659 ] && end=659
  RES=4k FAST=1 blender -b --factory-startup -P render.py -- "$frames" 16 "$start" "$end" "$step" 60 2>&1 | grep -E "Traceback|Error" || true
done
fps=$((60 / step)); tmp="$(mktemp -d)"; k=0
for i in $(seq 0 "$step" 659); do ln -s "$here/$frames/f$(printf %04d "$i").png" "$tmp/s$(printf %04d "$k").png"; k=$((k + 1)); done
split=$((480 / step))
ffmpeg -loglevel error -y -framerate "$fps" -i "$tmp/s%04d.png" -frames:v "$split" -c:v libvpx-vp9 -pix_fmt yuva420p -b:v 0 -crf 12 -row-mt 1 "$out/console-open.webm"
ffmpeg -loglevel error -y -framerate "$fps" -start_number "$split" -i "$tmp/s%04d.png" -c:v libvpx-vp9 -pix_fmt yuva420p -b:v 0 -crf 12 -row-mt 1 "$out/console-close.webm"
rm -rf "$tmp"
echo "Wrote console-open.webm and console-close.webm at $fps fps"
