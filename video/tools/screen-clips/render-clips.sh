#!/usr/bin/env bash
# Renders every animated screen sequence (engine/screens) into
# assets/footage/<seq>-top.mp4 and <seq>-bottom.mp4 (2400x1440 and
# 1920x1440, 60 fps), and the 3ds.local browser window for the receive
# scene as browser-receive.webm (2560x1600, transparent).
#
#   ./tools/screen-clips/render-clips.sh [seq ...]     (default: all)
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
out="$root/assets/footage"
tmp="$here/.tmp"
mkdir -p "$out" "$tmp"
cd "$here"
seqs=("$@")
if [ ${#seqs[@]} -eq 0 ]; then
  seqs=($(npx remotion compositions entry.tsx --public-dir="$here/public" --quiet 2>/dev/null | grep -o 'clip-[A-Za-z]*' | sed 's/clip-//'))
fi
for s in "${seqs[@]}"; do
  echo "== $s"
  npx remotion render entry.tsx "clip-$s" "$tmp/$s.mp4" --config="$here/remotion.config.ts" --public-dir="$here/public" \
    --codec=h264 --crf=6 --image-format=png --concurrency=4 --log=error
  ffmpeg -loglevel error -y -i "$tmp/$s.mp4" -vf "crop=2400:1440:0:0" -c:v libx264 -crf 10 -preset slow -pix_fmt yuv420p -an "$out/$s-top.mp4"
  ffmpeg -loglevel error -y -i "$tmp/$s.mp4" -vf "crop=1920:1440:2400:0" -c:v libx264 -crf 10 -preset slow -pix_fmt yuv420p -an "$out/$s-bottom.mp4"
  rm -f "$tmp/$s.mp4"
done
if [ $# -eq 0 ]; then
  echo "== browser-receive"
  npx remotion render entry.tsx browser-receive "$out/browser-receive.webm" --config="$here/remotion.config.ts" --public-dir="$here/public" \
    --codec=vp9 --pixel-format=yuva420p --image-format=png --scale=2 --crf=10 --concurrency=4 --log=error
fi
echo "Clips in $out"
