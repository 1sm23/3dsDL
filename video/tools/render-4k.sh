#!/usr/bin/env bash
# Renders the film at 3840 x 2160, 60 fps, and encodes the master with the
# soundtrack: renders/3dsDL - Follow the ring (4K60).mp4. HyperFrames'
# own audio mix sits lower than the mastered one, so the video is rendered
# near-lossless and muxed with assets/music/film-mix.wav in the final encode.
set -euo pipefail
cd "$(dirname "$0")/.."
python3 tools/film/first-build.py
npx --yes hyperframes@0.8.145 render --resolution 4k --fps 60 --quality delivery --crf 4 --video-frame-format png -o renders/.raw-4k.mp4 --quiet
ffmpeg -v error -y -i renders/.raw-4k.mp4 -i assets/music/film-mix.wav -map 0:v -map 1:a \
  -c:v libx264 -preset slow -crf 10 -aq-mode 3 -profile:v high -level 5.2 -pix_fmt yuv420p -g 30 -bf 2 \
  -color_primaries bt709 -color_trc bt709 -colorspace bt709 \
  -c:a aac -b:a 320k -ar 48000 -shortest -movflags +faststart "renders/3dsDL - Follow the ring (4K60).mp4"
rm -f renders/.raw-4k.mp4
echo "Wrote renders/3dsDL - Follow the ring (4K60).mp4"
