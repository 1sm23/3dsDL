#!/usr/bin/env bash

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_IMAGE="${THREEDSDL_BUILD_IMAGE:-devkitpro/devkitarm:20260610}"
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

step "Validating build artifacts"
for artifact in 3dsDL.3dsx 3dsDL.smdh; do
    [[ -s "${PROJECT_ROOT}/${artifact}" ]] || fail "Missing or empty artifact: ${artifact}"
    printf 'Found %s\n' "${artifact}"
done

step "Packaging dist/ and generating SHA-256"
mkdir -p "${DIST_DIR}"
cp "${PROJECT_ROOT}/3dsDL.3dsx" "${DIST_DIR}/3dsDL.3dsx"
cp "${PROJECT_ROOT}/3dsDL.smdh" "${DIST_DIR}/3dsDL.smdh"
(
    cd "${DIST_DIR}"
    if command -v shasum >/dev/null 2>&1; then
        shasum -a 256 3dsDL.3dsx > 3dsDL.sha256
    elif command -v sha256sum >/dev/null 2>&1; then
        sha256sum 3dsDL.3dsx > 3dsDL.sha256
    else
        fail "Neither shasum nor sha256sum is available."
    fi
)

printf '\nBuild complete.\n'
printf '  App:      %s\n' "${DIST_DIR}/3dsDL.3dsx"
printf '  Metadata: %s\n' "${DIST_DIR}/3dsDL.smdh"
printf '  Checksum: %s\n' "${DIST_DIR}/3dsDL.sha256"
