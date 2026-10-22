# 3dsDL launch video

"Follow the ring", the 78 second launch film for 3dsDL, built with
[HyperFrames](https://hyperframes.heygen.com) (HTML + GSAP, rendered in
headless Chrome). The finished master is
`renders/3dsDL - Follow the ring (4K60).mp4` (3840 x 2160, 60 fps) and the
YouTube thumbnail is `renders/thumbnails/thumbnail.jpg`.

## Building it

Everything runs from this folder. A full rebuild, in order:

```sh
npm run clips      # the app's animated screens (Remotion)
npm run console    # the white 3D console shots at 4K (Blender, about 3 hours)
npm run build      # writes index.html, compositions/frames/ and the cue sheet
npm run score      # the sound effects, assets/music/sfx.wav
npm run mix        # the soundtrack, assets/music/film-mix.wav
npm run render     # the 4K60 master
npm run thumbnail  # the thumbnail
```

Most changes only need `npm run build` and `npm run render`. Edit
`tools/film/first-build.py`, never the generated HTML. A change to when
sounds play needs `npm run score` and `npm run mix` too.

## What's where

- `tools/film/first-build.py` writes the film: every scene, its timing and
  motion, the copy, and the sound cues (`tools/score/cue-sheet.ts`).
- `tools/score/` synthesises the sound effects from code, including 3dsDL's
  own interface sounds.
- `tools/music/` blends the two tracks (`handoff.py`, tempo-matched on a
  shared chord), and masters the mix to -14 LUFS (`master.sh`).
  `beatmap.py` maps a track's beats. The Python environments are
  `.venv` (librosa) and `.venv-beat` (Beat This!).
- `tools/console-blender/` renders the console in Cycles: `geng4d.py` rigs
  the model, `scene.py` sets up the lights and render, `render.py` animates
  the two shots, `still.py` renders the thumbnail still.
- `tools/screen-clips/` renders the app's screens. `engine/` redraws the real
  UI on canvases, and `patches/data.ts` holds the film's demo content.
- `tools/thumbnail/` is the thumbnail page.

## Requirements

Node 20+, Python 3, ffmpeg, Blender 5.2 (Metal GPU on a Mac), and for the
screen clips `npm install` in `tools/screen-clips`.

## Credits and licences

- The 3D console is "New Nintendo 3DS" by Geng4d, bought on Sketchfab. Its
  files stay out of git.
- The device photo is by PantheraLeo1359531 (CC BY 4.0).
- The music is "Home Menu" and "Software Download" (Nintendo eShop) from the
  Nintendo 3DS system, © Nintendo. It's for this personal, non-commercial
  video only and stays out of git.
- Not affiliated with Nintendo.
