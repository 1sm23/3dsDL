# Website

The static site published at https://ankush.one/3dsDL/ by
`.github/workflows/pages.yml` whenever `site/` changes on `main`.

- `index.html`, `styles.css`, `app.js`: the page. It works without JavaScript
  except for the "send a link" QR tool; `app.js` also shows the latest
  release version and file sizes from the GitHub API.
- `qr/`: install QR codes. They point at
  `releases/latest/download/...`, so they stay valid for every release.
  Regenerate with `scripts/make-site-qr.sh` only if the repository or asset
  names change.
- `vendor/qrcode.js`: [qrcode-generator](https://github.com/kazuhikoarase/qrcode-generator)
  1.4.4 by Kazuhiko Arase (MIT), SHA-256
  `18ae399f81182bc9de916e9c77b195df20cc58d6f2d55a62b085a299f1bf1780`.
- `assets/`: images resized from `resources/`.

To preview locally, run `python3 -m http.server -d site` and open
http://localhost:8000.
