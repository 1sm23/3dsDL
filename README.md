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

Install current devkitARM, libctru, citro2d, citro3d, 3ds-curl, 3ds-mbedtls,
and their dependencies, then run:

```sh
make
```

The output is `3dsDL.3dsx`.

With Docker or Colima:

```sh
docker run --rm -v "$PWD:/project" -w /project \
  devkitpro/devkitarm:latest make -j2
```

## Licensing

3dsDL is distributed under GPLv3. Bundled third-party components retain their
respective notices; see [LICENSE](LICENSE) and
[LICENSE-quirc.md](LICENSE-quirc.md).
