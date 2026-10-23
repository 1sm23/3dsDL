# Website

The static site published at https://ankush.one/3dsDL/ by
`.github/workflows/pages.yml` whenever `site/` changes on `main`.

- `index.html`, `styles.css`, `app.js`: the page, themed after the DS and 3DS
  system software. It works without JavaScript except for the "send a link"
  QR tool. `app.js` adds the HOME Menu features, the status bar clock, the
  Notifications read state, the installer tabs, the device screens and their
  boot animation, the synthesised menu sounds and the scroll reveals, and
  shows the latest release version and file sizes from the GitHub API.
- The page before the retheme is tagged `site-classic`.
- `qr/install-cia.svg`: the install QR code. It points at
  `releases/latest/download/3dsDL.cia`, so it stays valid for every release.
  Regenerate it with `scripts/make-site-qr.sh` only if the repository or
  asset name changes.
- `vendor/qrcode.js`: [qrcode-generator](https://github.com/kazuhikoarase/qrcode-generator)
  1.4.4 by Kazuhiko Arase (MIT), SHA-256
  `18ae399f81182bc9de916e9c77b195df20cc58d6f2d55a62b085a299f1bf1780`.
- `fonts/`: M PLUS Rounded 1c and M PLUS 1 Code (text), DotGothic16 (the
  pixel face for system labels) and Gaegu (the handwriting on the doodles),
  Latin subsets, all under the SIL Open Font License (the `OFL-*.txt` files).
- `assets/`: the icons and the social image (`banner.jpg`), rendered from the
  3D banner model by `resources/banner/render.sh`, the two console images
  below, and `screens/`, screenshots of 3dsDL.

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

Both images show 3dsDL on their screens, at the 3DS's native sizes
(400 × 240 and 320 × 240 px), projected onto the photographed screens with a
CSS `matrix3d()`. The hero steps through screenshots of a download and
install; the install section shows HTML for whichever method is picked. Each image's matrices live on `.device-angled` and
`.device-front` in `styles.css` and come from `scripts/screen-map`; see its
README. If an image is replaced, its matrices have to be recomputed, and so do the
hero doodles (drawn in the angled photo's 1356 × 1177 pixel space) and the
install render's LED position (`.led`, in its 736 × 736 pixel space).

`assets/screens/` holds lossless WebP captures from Azahar, each
`<name>-top.webp` (400 × 240) and `<name>-bottom.webp` (320 × 240) cut from
one 400 × 480 screenshot at 0,0 and 40,240. Recapture them whenever the
screens they show change.

To preview locally, run `python3 -m http.server -d site` and open
http://localhost:8000.

## Analytics

`app.js` starts with the same PostHog setup as the rest of ankush.one: the same project, sent through
the first-party proxy at `p1.ankush.one`, with autocapture, heatmaps, Web Vitals, error capture and
session replay (console and network calls included). Every event carries `site: "3dsDL"`, and since the
pages share the ankush.one origin, a visitor keeps one id and one replay session across this site, the
portfolio and the other project pages. Nothing loads on localhost or when the browser sends Global
Privacy Control.

Custom events go through `window.track(name, props)`: `send_qr_made`, `send_qr_enlarged` and
`send_link_shared` from the "send a link" tool (with the link's host, never the whole link),
`install_method`, `feature_viewed`, `faq_opened`, `menu_sounds`, `download_link_copied` and
`buddy_opened` (the 3dsFM card). The send form, its QR code and the large-code dialog carry
`ph-no-capture`, so neither autocapture nor session replay records them, and a shared `?url=` is
taken out of the address bar before PostHog starts. What the site sends is listed on https://ankush.one/privacy/, so
update that page when adding an event.
