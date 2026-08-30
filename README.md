# 3dsDL

A maintained Nintendo 3DS homebrew application for downloading files directly
to the SD card from HTTP/HTTPS URLs and QR codes.

Maintainer: [ankushKun](https://github.com/ankushKun) ·
X: [@ankushKun_](https://x.com/ankushKun_)

## Current status

The modern download engine is functional on New Nintendo 3DS hardware. It uses
libcurl and mbedTLS, follows redirects, verifies HTTPS with an embedded Mozilla
CA bundle, streams downloads to the SD card, and removes partial files after
errors or cancellation.

The current development build includes a new Citro2D interface, button and
touch navigation, URL review, download progress, corrected camera buffer
handling, and an explicit Rename / Overwrite / Cancel dialog when a destination
file already exists. Camera rendering and the new interface still require a
final hardware verification pass.

## Install

Copy the release build to:

```text
/3ds/3dsDL/3dsDL.3dsx
```

Then launch 3dsDL from the Homebrew Launcher. FTPD can be used for the initial
copy without removing the SD card.

QR codes must contain a direct URL beginning with `http://` or `https://`.
JavaScript download pages, authentication prompts, and interactive confirmation
pages are not direct file URLs.

## Build

The recommended build is a single command:

```sh
./scripts/build.sh
```

The script performs each step explicitly: checks Docker, starts Colima when
needed, obtains the pinned devkitPro image, cleans the old build, compiles the
application, validates the artifacts, and writes release-ready files plus a
SHA-256 checksum to `dist/`.

Optional environment overrides:

```sh
THREEDSDL_BUILD_JOBS=4 ./scripts/build.sh
THREEDSDL_BUILD_IMAGE=devkitpro/devkitarm:20260610 ./scripts/build.sh
THREEDSDL_KEEP_COLIMA=1 ./scripts/build.sh
```

For a native build, install current devkitARM, libctru, citro2d, citro3d,
3ds-curl, 3ds-mbedtls, and their dependencies, then run `make`.

## Licensing

3dsDL is distributed under GPLv3. Bundled third-party components retain their
respective notices; see [LICENSE](LICENSE) and
[LICENSE-quirc.md](LICENSE-quirc.md).
