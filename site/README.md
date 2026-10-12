# Website

The static site published at https://ankush.one/3dsDL/ by
`.github/workflows/pages.yml` whenever `site/` changes on `main`.

- `index.html`, `styles.css`, `app.js`: the page, themed after the DS and 3DS
  system software. It works without JavaScript except for the "send a link"
  QR tool. `app.js` adds the HOME Menu features, the status bar clock, the
  Notifications read state, the installer tabs, the device screens and their
  boot animation, the synthesised menu sounds and the scroll reveals, and
  shows the latest release version and file sizes from the GitHub API.
- `classic.html`, `classic.css`, `classic.js`: a snapshot of the page before
  the retheme (also tagged `site-classic`), kept for comparison. It is
  marked `noindex`.
- `qr/`: install QR codes. They point at `releases/latest/download/...`, so
  they stay valid for every release. Regenerate with
  `scripts/make-site-qr.sh` only if the repository or asset names change.
- `vendor/qrcode.js`: [qrcode-generator](https://github.com/kazuhikoarase/qrcode-generator)
  1.4.4 by Kazuhiko Arase (MIT), SHA-256
  `18ae399f81182bc9de916e9c77b195df20cc58d6f2d55a62b085a299f1bf1780`.
- `fonts/`: M PLUS Rounded 1c and M PLUS 1 Code (text), DotGothic16 (the
  pixel face for system labels) and Gaegu (the handwriting on the doodles),
  Latin subsets, all under the SIL Open Font License (the `OFL-*.txt` files).
- `assets/`: the icons and the social image (`banner.jpg`), rendered from the
  3D banner model by `resources/banner/render.sh`, plus the two console
  images below.

## Console images

| File | Used in | Source | Author | Licence |
| --- | --- | --- | --- | --- |
| `assets/new-3ds-angled.png` | hero | [File:New Nintendo 3DS (weiß) 20250129 HOF2650 RAW-Export.png](https://commons.wikimedia.org/wiki/File:New_Nintendo_3DS_(wei%C3%9F)_20250129_HOF2650_RAW-Export.png), resized to 1800 px and cropped to the console (1356 × 1177) | PantheraLeo1359531 | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| `assets/new-3ds-front.png` | install | Nintendo's front-on New Nintendo 3DS product render, cut out at 736 × 736 with the screens blanked | Nintendo | © Nintendo, not freely licensed |

The CC BY photograph requires the credit that the page footer carries; the
footer also notes that it was cropped and that its screens are replaced. The
install render is Nintendo's own marketing image, credited in the footer too;
swap it for a freely licensed front view if one turns up.

### Live screens on the images

Both images show 3dsDL on their screens. The screens are HTML drawn at the
3DS's native sizes (400 × 240 and 320 × 240 px) using the same coordinates
as `ui.cpp` (`drawProgress()` in the hero; the install section shows the
QR code and steps for whichever method is picked), then projected onto the photographed screens with a CSS
`matrix3d()`. Each image's matrices live on `.device-angled` and
`.device-front` in `styles.css` and come from `scripts/screen-map`; see its
README. If an image is replaced, its matrices have to be recomputed, and so do the
hero doodles (drawn in the angled photo's 1356 × 1177 pixel space) and the
install render's LED position (`.led`, in its 736 × 736 pixel space).

To preview locally, run `python3 -m http.server -d site` and open
http://localhost:8000.
