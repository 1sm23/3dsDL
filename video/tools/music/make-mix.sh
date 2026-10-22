#!/usr/bin/env bash
# Builds the film's soundtrack, assets/music/film-mix.wav: the 3DS Home Menu
# music under the opening, handed off to the eShop's Software Download music
# through its bar 15 (an A major chord both tracks share) on the 0:12 cut,
# mixed with the sound effects (npm run score) and mastered for YouTube.
#   tools/music/make-mix.sh      (run from the video folder)
set -euo pipefail
cd "$(dirname "$0")/../.."
m=assets/music
tools/music/.venv/bin/python -I tools/music/handoff.py "$m/3ds-bgm/02. Home Menu.mp3" \
  "$m/3ds-bgm/32. eShop - Software Download.mp3" "$m/beats/3ds-32. eShop - Software Download.json" \
  "$m/bgm.wav" 8.5 12 14.2 15 2>&1 | grep -v Warn
tools/music/master.sh "$m/bgm.wav" "$m/film-mix.wav" "$m/sfx.wav"
