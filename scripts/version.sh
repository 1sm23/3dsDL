#!/usr/bin/env bash
# What the VERSION file means for a build.
#
# VERSION is either a release, "1.2.3", or a pre-release of one,
# "1.2.3-beta.1" or "1.2.3-rc.2" (the label is SemVer's: dot-separated
# letters, digits and hyphens). The app shows it as written.
#
# A CIA carries its version as three small numbers (major and minor up to
# 63, micro up to 15), with no room for a label. A release keeps its own
# numbers. A pre-release takes the version one step below the release it
# leads up to, so 0.8.0-beta.N packs as 0.7.15: it installs over 0.7.x and
# over the other 0.8.0 betas, and 0.8.0 itself installs over all of them.
#
#   scripts/version.sh                 numbers for the VERSION file
#   scripts/version.sh 0.8.0-beta.1    numbers for any version
#
# Prints "<major> <minor> <micro> <release|prerelease>". build.sh sources
# this file and calls cia_version itself.

cia_version() {
    local version="$1"
    if [[ ! "${version}" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-([0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*))?$ ]]; then
        echo "VERSION must look like 1.2.3 or 1.2.3-beta.1, found '${version}'." >&2
        return 1
    fi
    local major="${BASH_REMATCH[1]}" minor="${BASH_REMATCH[2]}" micro="${BASH_REMATCH[3]}"
    local label="${BASH_REMATCH[5]}"
    if (( major > 63 || minor > 63 || micro > 15 )); then
        echo "${version} does not fit a CIA, which holds at most 63.63.15." >&2
        return 1
    fi
    if [[ -z "${label}" ]]; then
        echo "${major} ${minor} ${micro} release"
        return 0
    fi
    if (( micro > 0 )); then
        micro=$((micro - 1))
    elif (( minor > 0 )); then
        minor=$((minor - 1))
        micro=15
    elif (( major > 0 )); then
        major=$((major - 1))
        minor=63
        micro=15
    else
        echo "0.0.0 has nothing below it, so it cannot have a pre-release." >&2
        return 1
    fi
    echo "${major} ${minor} ${micro} prerelease"
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    set -Eeuo pipefail
    cia_version "${1:-$(tr -d '[:space:]' < "$(dirname "$0")/../VERSION")}"
fi
