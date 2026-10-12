# Website

The static site published at https://ankush.one/3dsDL/ by
`.github/workflows/pages.yml` whenever `site/` changes on `main`.

- `index.html`, `styles.css`, `app.js`: the page. It works without JavaScript
  except for the "send a link" QR tool; `app.js` also animates the device
  screens, the Cover Flow and the scroll reveals, and shows the latest
  release version and file sizes from the GitHub API.
- `qr/`: install QR codes. They point at `releases/latest/download/...`, so
  they stay valid for every release. Regenerate with
  `scripts/make-site-qr.sh` only if the repository or asset names change.
- `vendor/qrcode.js`: [qrcode-generator](https://github.com/kazuhikoarase/qrcode-generator)
  1.4.4 by Kazuhiko Arase (MIT), SHA-256
  `18ae399f81182bc9de916e9c77b195df20cc58d6f2d55a62b085a299f1bf1780`.
- `fonts/`: M PLUS Rounded 1c and M PLUS 1 Code, Latin subsets, under the
  SIL Open Font License (the `OFL-*.txt` files).
- `assets/`: the icon and banner resized from `resources/`, plus the two
  console photographs below.

## Console photographs

Both come from Wikimedia Commons and are resized to 1400 px wide with their
transparency kept.

| File | Source | Author | Licence |
| --- | --- | --- | --- |
| `assets/new-3ds.png` | [File:New Nintendo 3DS.png](https://commons.wikimedia.org/wiki/File:New_Nintendo_3DS.png) | PhilipTerryGraham | CC0 |
| `assets/new-3ds-studio.png` | [File:New Nintendo 3DS (weiß) 20250129 HOF2650 RAW-Export.png](https://commons.wikimedia.org/wiki/File:New_Nintendo_3DS_(wei%C3%9F)_20250129_HOF2650_RAW-Export.png) | PantheraLeo1359531 | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |

The CC BY photograph requires the credit that the page footer carries.

### Live screens on the hero photo

The hero's two screens are HTML elements drawn at the 3DS's native sizes
(400 × 240 and 320 × 240 px) using the same coordinates as `drawProgress()`
in `source/app/ui.cpp`, then projected onto the photographed screens with a
CSS `matrix3d()`. The matrices in `:root` of `styles.css` were computed by:

1. thresholding the photo's dark pixels and labelling connected regions,
   which isolates the two screens from the white shell;
2. taking each region's four extreme corners;
3. solving the homography from each screen's rectangle to its photographed
   quadrilateral (direct linear transform) and writing it in CSS's
   column-major `matrix3d` form, with `transform-origin: 0 0`.

If the photo is replaced, those matrices have to be recomputed for it.

To preview locally, run `python3 -m http.server -d site` and open
http://localhost:8000.
