#!/usr/bin/env bash
# What the VERSION file means for a build.
#
# VERSION is either a release, "1.2.3", or a pre-release of one,
# "1.2.3-beta.1" or "1.2.3-rc.2" (the label is SemVer's: dot-separated
# letters, digits and hyphens). This fork uses a fourth component for its
# revisions: "1.2.3.1", "1.2.3.2", up to "1.2.3.15". Legacy n builds such
# as "1.2.3n" remain supported.
# The app shows it as written.
#
# A CIA carries its version as one 16-bit number, which AM only lets go up:
# it refuses an older number and the same one again (0xC8E083FC), and 3dsDL
# cannot remove itself while it runs to get round that. So every build a
# tester installs needs a higher number than the last, betas included, and
# there is no room for that between 0.8.0 and 0.8.1 if the CIA says 0.8.1.
#
# The number is therefore a build number made from the version:
#
#     major * 16384 + minor * 512 + patch * 32 + step
#
# where the pre-release's last number uses steps 1 to 14, 15 is the release,
# 16 is the upstream-synced n build, and 17 to 31 are fork revisions
# (1.2.3.1 to 1.2.3.15). The next patch starts 32 steps later, leaving every
# channel installable in order. Major goes up to 3, minor to 31, patch to 15.
# This expanded spacing also keeps fork revisions above older 0.8.2n
# installs while leaving room for the next upstream beta and release.
#
#   scripts/version.sh                 numbers for the VERSION file
#   scripts/version.sh 0.8.1-beta.1    numbers for any version
#
# Prints "<major> <minor> <micro> <release|prerelease>", the fields makerom
# takes for that number. build.sh sources this file and calls cia_version.

cia_version() {
    local version="$1" base="$1" fork=0 revision=0
    if [[ "${base}" =~ ^([0-9]+\.[0-9]+\.[0-9]+)\.([1-9][0-9]?)$ ]]; then
        base="${BASH_REMATCH[1]}"
        revision="${BASH_REMATCH[2]}"
        if (( revision > 15 )); then
            echo "A fork revision must be 1 to 15, found '${version}'." >&2
            return 1
        fi
    fi
    if [[ "${base}" == *n ]]; then
        base="${base%n}"
        fork=1
    fi
    if [[ ! "${base}" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-([0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*))?$ ]]; then
        echo "VERSION must look like 1.2.3, 1.2.3.1, 1.2.3n or 1.2.3-beta.1, found '${version}'." >&2
        return 1
    fi
    local major="${BASH_REMATCH[1]}" minor="${BASH_REMATCH[2]}" patch="${BASH_REMATCH[3]}"
    local label="${BASH_REMATCH[5]}"
    if (( major > 3 || minor > 31 || patch > 15 )); then
        echo "${version} does not fit a CIA's version, which holds up to 3.31.15." >&2
        return 1
    fi
    local step=15 kind=release
    if (( revision > 0 )); then
        step=$((16 + revision))
    elif [[ -n "${label}" ]]; then
        if [[ ! "${label}" =~ (^|\.)([1-9][0-9]?)$ ]] || (( BASH_REMATCH[2] > 14 )); then
            echo "A pre-release must end in a number from 1 to 14, like beta.1 or rc.3, found '${label}'." >&2
            return 1
        else
            step="${BASH_REMATCH[2]}"
            kind=prerelease
        fi
    fi
    local number=$(( major * 16384 + minor * 512 + patch * 32 + step + fork ))
    if (( number > 65535 )); then
        echo "${version} exceeds a CIA's maximum build number (65535)." >&2
        return 1
    fi
    echo "$(( number >> 10 )) $(( (number >> 4) & 63 )) $(( number & 15 )) ${kind}"
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    set -Eeuo pipefail
    cia_version "${1:-$(tr -d '[:space:]' < "$(dirname "$0")/../VERSION")}"
fi
