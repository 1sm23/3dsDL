# Roadmap

## 0.2 — Hardware stabilization and new interface

- [x] Move camera DMA to linear memory and invalidate its CPU cache.
- [x] Replace the inherited console UI with Citro2D.
- [x] Add URL confirmation and file-conflict dialogs.
- [x] Add touch and button navigation.
- [ ] Verify camera orientation, colors, and QR decoding on hardware.
- [ ] Verify the interface on New and Old Nintendo 3DS hardware.

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
