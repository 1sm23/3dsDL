# HOME Menu banner and icon

When 3dsDL is selected on the HOME Menu, the top screen shows its banner,
with a short jingle. This folder holds an animated 3D banner model, like the
ones official games have, and everything it is made from, as code.

> **Status: experimental.** The first build of the 3D banner crashed the
> HOME Menu on a console as soon as 3dsDL was selected. `build-cgfx.py` now
> applies the fix described below, but until a build passes the banner
> checks in `docs/HARDWARE.md`, `scripts/build.sh` packs the flat picture
> (`resources/banner-3dsdl.png`, rendered from the same model) and only uses
> the model with `THREEDSDL_3D_BANNER=1`.

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

With `THREEDSDL_3D_BANNER=1`, `scripts/build.sh` then packs the model and
jingle into the CIA with `bannertool makebanner -ci`. A banner only appears
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
- **Under 512 KiB.** The CGFX is about 190 KiB, most of it the wordmark
  texture. `build-cgfx.py` refuses to write anything over the limit.
- **Small power-of-two textures**, at most 256 pixels on a side.
- **The camera is fixed:** at (0, 1, 44.786), looking down -z with a 30°
  vertical field of view. The plane z = 0 shows x from -20 to 20 and y from
  -11 to 13, so one unit is ten pixels of the top screen. Anything with a
  larger z comes out of the screen in 3D.

## Trying the 3D banner

```sh
THREEDSDL_3D_BANNER=1 ./scripts/build.sh
```

`docs/HARDWARE.md` has the checks to run on a console, and what to do if the
HOME Menu crashes: install a normal (flat banner) build over it with FBI.
