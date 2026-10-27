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

# fields of major * 16384 + minor * 512 + patch * 32 + step
expect 0.8.1 "4 2 15 release"              # 4143
expect 0.8.1-beta.1 "4 2 1 prerelease"     # 4129
expect 0.8.1-beta.2 "4 2 2 prerelease"     # 4130
expect 0.8.1-rc.3 "4 2 3 prerelease"       # 4131
expect 0.8.0 "4 0 15 release"              # 4111, above the 128 it shipped as
expect 0.9.0-beta.14 "4 32 14 prerelease"  # 4622
expect 1.0.0 "16 0 15 release"             # 16399
expect 3.31.15 "63 62 15 release"          # 65519
expect 3.31.15.15 "63 63 15 release"  # 65535, the most a CIA holds
expect 0.8.1-x-y.1.2 "4 2 2 prerelease"
expect 0.8.2n "4 5 0 release"             # 4176, above old 0.8.2n builds
expect 0.8.2.1 "4 5 1 release"       # 4177, after its n build
expect 0.8.2.2 "4 5 2 release"       # 4178
expect 0.8.3-beta.1n "4 6 2 prerelease"   # 4194, after the fork revisions
expect 0.8.3n "4 7 0 release"             # 4208
expect 0.8.15n "4 31 0 release"           # 4592, patch carry
expect 0.31.15n "15 63 0 release"         # 16368, minor carry
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
refuse 0.8.2nn
refuse 0.8.2N
refuse 0.8.2.16
refuse 0.8.2.0
refuse 0.8.2.01
refuse 0.8.2.1.1
refuse 0.8.2.1-beta.1
refuse 0.8.2.1n
refuse 3.31.16
refuse 3.32.0
refuse 3.63.15n

# Each build must install over the one before it: the numbers only go up.
previous=128 # what 0.8.0 shipped as
for version in 0.8.1-beta.1 0.8.1-beta.2 0.8.1-rc.3 0.8.1 0.8.2-beta.1 0.8.2 0.8.2n 0.8.2.1 0.8.2.2 0.8.3-beta.1n 0.8.3n 0.8.15n 0.9.0-beta.1 0.9.0 0.9.0n 1.0.0-rc.1 1.0.0 1.0.0n; do
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
