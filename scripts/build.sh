#!/usr/bin/env bash

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_IMAGE="${THREEDSDL_BUILD_IMAGE:-devkitpro/devkitarm:20260610}"
CIA_IMAGE="${THREEDSDL_CIA_IMAGE:-3dsdl-cia-tools:1.0.1}"
BUILD_JOBS="${THREEDSDL_BUILD_JOBS:-2}"
KEEP_COLIMA="${THREEDSDL_KEEP_COLIMA:-0}"
DIST_DIR="${PROJECT_ROOT}/dist"
STARTED_COLIMA=0
STEP=0

step() {
    STEP=$((STEP + 1))
    printf '\n[%d] %s\n' "${STEP}" "$1"
}

fail() {
    printf '\nBuild failed: %s\n' "$1" >&2
    exit 1
}

cleanup() {
    if [[ "${STARTED_COLIMA}" == "1" && "${KEEP_COLIMA}" != "1" ]]; then
        printf '\nStopping the Colima instance started by this script...\n'
        colima stop >/dev/null
    fi
}

run_builder() {
    docker run --rm \
        --volume "${PROJECT_ROOT}:/project" \
        --workdir /project \
        "${BUILD_IMAGE}" "$@"
}

run_cia_tool() {
    docker run --rm --platform linux/amd64 \
        --volume "${PROJECT_ROOT}:/project" \
        --workdir /project \
        "${CIA_IMAGE}" "$@"
}

trap cleanup EXIT

[[ "${BUILD_JOBS}" =~ ^[1-9][0-9]*$ ]] || fail "THREEDSDL_BUILD_JOBS must be a positive integer."

step "Checking the container tools"
command -v docker >/dev/null 2>&1 || fail "Docker is required. Install Docker Desktop or Docker CLI with Colima."

if ! docker info >/dev/null 2>&1; then
    command -v colima >/dev/null 2>&1 || fail "Docker is not running and Colima is not installed."
    printf 'Docker is stopped; starting Colima...\n'
    colima start
    STARTED_COLIMA=1
    docker info >/dev/null 2>&1 || fail "Docker did not become available after Colima started."
fi

step "Checking the pinned devkitPro image (${BUILD_IMAGE})"
if ! docker image inspect "${BUILD_IMAGE}" >/dev/null 2>&1; then
    docker pull "${BUILD_IMAGE}"
else
    printf 'Using the locally cached image.\n'
fi

step "Checking the pinned CIA packaging image (${CIA_IMAGE})"
if ! docker image inspect "${CIA_IMAGE}" >/dev/null 2>&1; then
    docker build --platform linux/amd64 \
        --file "${PROJECT_ROOT}/scripts/cia-tools.Dockerfile" \
        --tag "${CIA_IMAGE}" "${PROJECT_ROOT}"
else
    printf 'Using the locally cached CIA packaging image.\n'
fi

step "Validating the launcher icon"
ICON="${PROJECT_ROOT}/resources/icon-3dsdl.png"
[[ -f "${ICON}" ]] || fail "Missing launcher icon: ${ICON}"
PNG_COLOR_TYPE="$(od -An -t u1 -j 25 -N 1 "${ICON}" | tr -d '[:space:]')"
[[ "${PNG_COLOR_TYPE}" == "4" || "${PNG_COLOR_TYPE}" == "6" ]] || \
    fail "The launcher PNG must contain a real alpha channel (PNG color type 4 or 6)."
printf 'Launcher icon contains an alpha channel.\n'

step "Cleaning previous build outputs"
run_builder make clean

step "Compiling 3dsDL with ${BUILD_JOBS} job(s)"
run_builder make -j"${BUILD_JOBS}"

step "Creating Home Menu metadata and banner"
run_cia_tool bannertool makebanner \
    -i resources/banner-3dsdl.png \
    -a resources/banner-silence.wav \
    -o build/3dsDL.bnr
run_cia_tool bannertool makesmdh \
    -s "3dsDL" \
    -l "Download files straight onto your 3DS" \
    -p "ankushKun" \
    -i resources/icon-3dsdl.png \
    -f visible,nosavebackups \
    -o build/3dsDL.icn

step "Packaging the installable CIA"
run_cia_tool makerom \
    -f cia -o 3dsDL.cia -target t -exefslogo \
    -elf 3dsDL.elf \
    -rsf resources/3dsDL.rsf \
    -banner build/3dsDL.bnr \
    -icon build/3dsDL.icn \
    -major 0 -minor 2 -micro 0

step "Validating build artifacts"
for artifact in 3dsDL.3dsx 3dsDL.smdh 3dsDL.cia; do
    [[ -s "${PROJECT_ROOT}/${artifact}" ]] || fail "Missing or empty artifact: ${artifact}"
    printf 'Found %s\n' "${artifact}"
done

step "Packaging dist/ and generating SHA-256"
mkdir -p "${DIST_DIR}"
cp "${PROJECT_ROOT}/3dsDL.3dsx" "${DIST_DIR}/3dsDL.3dsx"
cp "${PROJECT_ROOT}/3dsDL.smdh" "${DIST_DIR}/3dsDL.smdh"
cp "${PROJECT_ROOT}/3dsDL.cia" "${DIST_DIR}/3dsDL.cia"
(
    cd "${DIST_DIR}"
    if command -v shasum >/dev/null 2>&1; then
        shasum -a 256 3dsDL.3dsx 3dsDL.cia > 3dsDL.sha256
    elif command -v sha256sum >/dev/null 2>&1; then
        sha256sum 3dsDL.3dsx 3dsDL.cia > 3dsDL.sha256
    else
        fail "Neither shasum nor sha256sum is available."
    fi
)

step "Removing intermediate build files"
run_builder make clean

printf '\nBuild complete.\n'
printf '  3DSX:     %s\n' "${DIST_DIR}/3dsDL.3dsx"
printf '  CIA:      %s\n' "${DIST_DIR}/3dsDL.cia"
printf '  Metadata: %s\n' "${DIST_DIR}/3dsDL.smdh"
printf '  Checksum: %s\n' "${DIST_DIR}/3dsDL.sha256"
