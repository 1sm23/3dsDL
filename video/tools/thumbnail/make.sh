#!/usr/bin/env bash
# Snapshots the thumbnail in index.html and writes a YouTube-ready JPG
# (1280 x 720, well under 2 MB) to renders/thumbnails/thumbnail.jpg.
set -euo pipefail
cd "$(dirname "$0")"
rm -rf snapshots
npx --yes hyperframes@0.8.145 snapshot --at 0.5 >/dev/null
out=../../renders/thumbnails; mkdir -p "$out"
ffmpeg -loglevel error -y -i snapshots/frame-00-at-0.5s.png -q:v 2 "$out/thumbnail.jpg"
rm -rf snapshots
echo "Wrote renders/thumbnails/thumbnail.jpg"
