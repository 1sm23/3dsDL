#!/usr/bin/env bash
# Checks scripts/version.sh: the CIA numbers for releases and pre-releases,
# and the versions it turns away.

set -Euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=../scripts/version.sh
source "${HERE}/../scripts/version.sh"
failures=0

expect() {
    local got
    got="$(cia_version "$1" 2>/dev/null)"
    if [[ "${got}" != "$2" ]]; then
        echo "FAIL $1: expected '$2', got '${got}'"
        failures=$((failures + 1))
    fi
}

refuse() {
    if cia_version "$1" >/dev/null 2>&1; then
        echo "FAIL $1 should be refused"
        failures=$((failures + 1))
    fi
}

expect 0.7.2 "0 7 2 release"
expect 0.8.0 "0 8 0 release"
expect 0.8.0-beta.1 "0 7 15 prerelease"
expect 0.8.0-rc.2 "0 7 15 prerelease"
expect 0.8.3-alpha "0 8 2 prerelease"
expect 1.0.0-beta "0 63 15 prerelease"
expect 63.63.15 "63 63 15 release"
expect 0.8.0-x-y.1.2a "0 7 15 prerelease"
refuse 0.8
refuse v0.8.0
refuse 0.8.0-
refuse 0.8.0-beta..1
refuse 0.8.0+build
refuse 01.2.3
refuse 0.7.16
refuse 0.64.0
refuse 64.0.0
refuse 0.0.0-beta

if (( failures > 0 )); then
    echo "${failures} version check(s) failed"
    exit 1
fi
echo "version.sh: all checks passed"
