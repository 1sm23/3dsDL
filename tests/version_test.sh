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

# fields of major * 16384 + minor * 256 + patch * 16 + step
expect 0.8.1 "2 1 15 release"              # 2079
expect 0.8.1-beta.1 "2 1 1 prerelease"     # 2065
expect 0.8.1-beta.2 "2 1 2 prerelease"     # 2066
expect 0.8.1-rc.3 "2 1 3 prerelease"       # 2067
expect 0.8.0 "2 0 15 release"              # 2063, above the 128 it shipped as
expect 0.9.0-beta.14 "2 16 14 prerelease"  # 2318
expect 1.0.0 "16 0 15 release"             # 16399
expect 3.63.15 "63 63 15 release"          # 65535, the most a CIA holds
expect 0.8.1-x-y.1.2 "2 1 2 prerelease"
refuse 0.8
refuse v0.8.1
refuse 0.8.1-
refuse 0.8.1-beta..1
refuse 0.8.1+build
refuse 01.2.3
refuse 0.7.16
refuse 0.64.0
refuse 4.0.0
refuse 0.8.1-beta
refuse 0.8.1-beta.0
refuse 0.8.1-beta.15
refuse 0.8.1-beta.1a

# Each build must install over the one before it: the numbers only go up.
previous=128 # what 0.8.0 shipped as
for version in 0.8.1-beta.1 0.8.1-beta.2 0.8.1-rc.3 0.8.1 0.8.2-beta.1 0.8.2 0.9.0-beta.1 0.9.0 1.0.0-rc.1 1.0.0; do
    read -r a b c _ <<< "$(cia_version "${version}")"
    number=$(( (a << 10) | (b << 4) | c ))
    if (( number <= previous )); then
        echo "FAIL ${version} packs as ${number}, not above ${previous}"
        failures=$((failures + 1))
    fi
    previous=${number}
done

if (( failures > 0 )); then
    echo "${failures} version check(s) failed"
    exit 1
fi
echo "version.sh: all checks passed"
