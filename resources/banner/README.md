# HOME Menu banner and icon

When 3dsDL is selected on the HOME Menu, the top screen shows its banner,
with a short jingle. This folder holds an animated 3D banner model, like the
ones official games have, and everything it is made from, as code.

> **Status: confirmed on a console** and packed by `scripts/build.sh` since
> 0.3.1. Getting there took five test builds: the first two crashed the HOME
> Menu as soon as 3dsDL was selected, the third drew its first frame and then
> froze it, and the fourth worked but its wordmark vanished when seen from
> behind. The rules below are what fixed each of those; keep to them.

The scene is a glossy SD card in 3dsDL's cyan. A yellow download arrow drops
in from above the screen, lands with its tip in the card, bounces and sinks
inside. The card squashes happily, sparkles burst out, and the 3dsDL wordmark
floats in front, gently bobbing. The loop is four seconds long.

The 48 × 48 HOME Menu icon, the flat fallback banner, the README art and the
website's icons and social image are all rendered from the same model, so
they always match.

| File | What it is |
| --- | --- |
| `model.py` | Builds the scene as code: rounded, bevelled shapes, materials and rigid keyframe animation, written out as `banner.glb`. |
| `logo.html` | The wordmark texture, rendered to `logo.png` (256 × 128). |
| `jingle.py` | Synthesises `jingle.wav`, an original tune timed to the animation. |
| `build-cgfx.py` | Converts `banner.glb` to `3dsDL.cgfx` with [pycgfx](https://github.com/skyfloogle/pycgfx), then names each mesh's node (see below), sets a key light from the upper left and a soft ambient fill. pycgfx's defaults would leave anything turned away from the camera black. |
| `render.html`, `render.sh` | Render the model in headless Chrome with three.js, framed by the HOME Menu's banner camera, to make the icon and the other images. |
| `banner.glb`, `3dsDL.cgfx`, `jingle.wav`, `logo.png` | The generated files, committed so that `scripts/build.sh` needs none of these tools. |

## Rebuilding

After changing anything here:

```sh
resources/banner/render.sh   # logo texture, model, icon and images (needs Chrome)
scripts/make-banner.sh       # jingle, model and CGFX (needs Docker)
```

`scripts/make-banner.sh` runs pycgfx in a digest-pinned Python 3.12
container, with pycgfx's files fetched at a pinned commit and checked against
known SHA-256 hashes. pycgfx has no licence file, so it is fetched to a
temporary folder for each run and never stored in this repository.

`scripts/build.sh` then packs the model and jingle into the CIA with
`bannertool makebanner -ci`. A banner only appears
for the CIA, because the Homebrew Launcher shows the icon from the `.smdh`
and has no banners.

## Rules the model follows

These come from what the HOME Menu accepts. Breaking them can freeze the
HOME Menu on a real console even when an emulator shows the banner fine.

- **Rigid animation only.** Every moving part is its own node with
  translation, rotation and scale keys. Skinned (bone-weighted) animation has
  been reported to freeze the HOME Menu as soon as the title is selected.
- **The model is called `COMMON`.** pycgfx names it so, and the HOME Menu
  looks for it.
- **Every mesh names its node.** pycgfx leaves each mesh's node name blank;
  the first 3D build was made that way and crashed the HOME Menu.
  `build-cgfx.py` fills the names in (and makes mesh names unique), the fix
  that [CBC for YANBF](https://github.com/daaaaavia/CBC-for-YANBF)'s pycgfx
  patch notes call required for a banner that doesn't crash.
- **Every name lookup must resolve.** The HOME Menu finds bones, materials
  and animations by name through a Patricia tree in each table. pycgfx
  reorders the bones of see-through (BLEND) materials, like the wordmark,
  without rebuilding the bones' tree, so lookups land on the wrong bone; the
  second 3D build crashed this way (a null pointer read in `menu`).
  `build-cgfx.py` rebuilds the tree, marks BLEND materials translucent, and
  then walks every table's tree the way the console does, refusing to write
  a CGFX in which any name would not be found.
- **The wordmark is a billboard.** It is a flat picture, invisible from
  behind, so its bone uses the 3DS's Y-axis billboard mode and always turns
  to face the viewer. (Without it, the wordmark vanished whenever the banner
  was seen from the side.)
- **No blended transparency.** The wordmark is a hard-edged cut-out (alpha
  MASK), which needs no bone reordering and no extra drawing pass.
- **Nothing scales to (nearly) nothing.** Parts that should vanish hide
  inside the card instead. The third build shrank them to 0.001 and froze.
- **About 2,000 triangles at most**, the limit CBC for YANBF uses. The model
  is 1,934; arcs are kept to a few segments each.
- **Under 512 KiB.** The CGFX is about 150 KiB, most of it the wordmark
  texture. `build-cgfx.py` refuses to write anything over the limit.
- **Short stereo sound.** Banner sound must be two channels and at most 3
  seconds. The jingle is 1.6 seconds of 44.1 kHz stereo, the format the
  working flat banner uses.
- **Small power-of-two textures**, at most 256 pixels on a side.
- **The camera is fixed:** at (0, 1, 44.786), looking down -z with a 30°
  vertical field of view. The plane z = 0 shows x from -20 to 20 and y from
  -11 to 13, so one unit is ten pixels of the top screen. Anything with a
  larger z comes out of the screen in 3D.

## Testing changes

Check every change to the model on a console before releasing it: a bad
banner crashes or freezes the HOME Menu as soon as 3dsDL is selected.

```sh
./scripts/build.sh                          # the 3D banner (the default)
THREEDSDL_FLAT_BANNER=1 ./scripts/build.sh  # the flat picture instead
# another CGFX, such as a still made with model.py --static 0.8
THREEDSDL_BANNER_CGFX=path/to/banner.cgfx ./scripts/build.sh
```

`docs/HARDWARE.md` has the checks to run, and what to do if the HOME Menu
crashes: install a flat banner build over it with FBI.
