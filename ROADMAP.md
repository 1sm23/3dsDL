# Roadmap

## 0.2: Hardware stabilization and new interface

- [x] Move camera DMA to linear memory and invalidate its CPU cache.
- [x] Replace the inherited console UI with Citro2D.
- [x] Add URL confirmation and file-conflict dialogs.
- [x] Add touch and button navigation.
- [ ] Verify camera orientation, colors, and QR decoding on hardware.
- [ ] Verify the interface on New and Old Nintendo 3DS hardware.

Both hardware checks moved into the 1.0 hardware pass, which now has a written
plan in [docs/HARDWARE.md](docs/HARDWARE.md).

## 0.3: Download quality of life

- [x] Show speed, ETA, transferred size, and available SD space.
- [x] Add download history, retry, destination presets, and collision prompts.
- [x] Prevent sleep during active downloads.
- [x] Add optional HTTP range resume.
- [x] Make archive extraction explicitly opt-in. ZIP extraction is off by
      default and always asks before touching anything.

0.3.0 also adds a console clock check that reads internet time over NTP and
sets the clock for a chosen time zone, because HTTPS fails when the date is
wrong. It also fixes the problems found while reviewing 0.2.0: the camera feed
could freeze for good, the vendored QR decoder could overrun a buffer on noisy
images, a finished download was deleted when its name was rejected, dialogs
ran off the screen and could not be dismissed by touch, and the conflict
prompt stalled the connection while it waited.

## 0.4: Faster downloads

- [x] Ask for larger socket buffers, which widens each connection's TCP
      window (0.3.2).
- [x] Split large downloads across up to four connections when the server
      accepts byte ranges, with resume that remembers which parts are done.
- [x] Run the download on the New 3DS's third core and redraw the progress
      screen less often.

The socket buffer size and the idea that the 3DS caps each connection come
from Colby Shores' measurements in
[Steveice10/FBI#4](https://github.com/Steveice10/FBI/pull/4).

## 0.5: Installing CIAs

- [x] Install a CIA after it downloads, or from Files, through the AM
      service, with INSTALL & DELETE, a version check before replacing a
      newer title, seeds for newer games, and LAUNCH afterwards.
- [x] A Files screen that lists files as well as folders, with CIA details.
- [ ] Hardware pass for installing (items 29 to 36 in
      [docs/HARDWARE.md](docs/HARDWARE.md)).

FBI is no longer maintained, so 3dsDL is taking over its job of installing
what it downloads. The longer plan, including a full file manager and title
management, is in [docs/INSTALLER-PLAN.md](docs/INSTALLER-PLAN.md).

## 0.6: Files becomes a file manager

- [x] Copy, cut, paste, rename and delete files and whole folders, with
      progress, cancel, and a choice when a name is already taken.
- [x] Mark several entries, new folder, new file, sort by name, size, date
      or type, and hidden files.
- [x] Picture viewer (PNG, JPEG, BMP, GIF, TGA), a line editor for text and
      config files, ZIP contents, a hex view, and icons and titles for
      `.3dsx`, `.3ds`, `.nds` and `.tik` files.
- [x] Install several CIAs at once, or every CIA in a folder.
- [ ] Hardware pass for Files (items 37 to 44 in
      [docs/HARDWARE.md](docs/HARDWARE.md)).

## 1.0: Stable release

- [x] Host-side URL and filename tests.
- [x] Redirect, TLS, chunked-transfer, cancellation, and SD failure tests.
- [ ] New/Old 3DS hardware compatibility matrix. The checklist and matrix are
      ready in [docs/HARDWARE.md](docs/HARDWARE.md) and need runs on real
      consoles.
- [x] Reproducible CI builds, checksums, and release artifacts. The 3DSX is
      reproducible byte for byte. The CIA is too, apart from the ticket ID,
      title key and content ID that makerom randomises on every run.

## Ideas for after 1.0

- Receive links from a phone or computer browser on the same network, so long
  URLs never have to be typed on the console.
- Show a SHA-256 checksum for finished downloads.
