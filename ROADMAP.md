# Roadmap

## 0.1 — Hardware stabilization

- Fix camera DMA allocation and cache coherency.
- Correct camera orientation and pixel conversion.
- Remove double-buffer ghosting from the inherited console UI.
- Verify QR scanning on New and Old Nintendo 3DS hardware.

## 0.2 — New interface

- Replace the console UI with Citro2D.
- Add Home, Scanner, Download, History, Settings, and Error screens.
- Support touch, buttons, and clear focus states.
- Add decoded-URL confirmation and editable filenames.

## 0.3 — Download quality of life

- Show speed, ETA, transferred size, and available SD space.
- Add download history, retry, destination presets, and collision prompts.
- Prevent sleep during active downloads.
- Add optional HTTP range resume.
- Make archive extraction explicitly opt-in.

## 1.0 — Stable release

- Host-side URL and filename tests.
- Redirect, TLS, chunked-transfer, cancellation, and SD failure tests.
- New/Old 3DS hardware compatibility matrix.
- Reproducible CI builds, checksums, and release artifacts.

