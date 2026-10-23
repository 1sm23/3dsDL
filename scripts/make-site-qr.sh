#!/usr/bin/env bash
# Regenerates the install QR code on the website. It points at GitHub's
# "latest release" download URL, so it only changes if the repository or
# the asset name changes.

set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO="ankushKun/3dsDL"
LATEST="https://github.com/${REPO}/releases/latest/download"
TOOL="$(mktemp -d)/qr-svg"
trap 'rm -rf "$(dirname "${TOOL}")"' EXIT

cc -O2 -I"${ROOT}/source/qrcodegen" "${ROOT}/scripts/qr-svg.c" \
    "${ROOT}/source/qrcodegen/qrcodegen.c" -o "${TOOL}"

"${TOOL}" "${LATEST}/3dsDL.cia" "${ROOT}/site/qr/install-cia.svg" \
    "QR code that installs the latest 3dsDL CIA with FBI"
echo "Wrote site/qr/install-cia.svg"
