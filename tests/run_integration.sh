#!/usr/bin/env bash
# Starts the fixture server with a throwaway self-signed certificate, builds
# the ZIP fixtures, and runs the integration test binary against them.
#
#   ./run_integration.sh ./build/integration_tests [test-name-filter]

set -Eeuo pipefail

BINARY="$1"
FILTER="${2:-}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK="$(mktemp -d)"
SERVER_PID=""

cleanup() {
    if [[ -n "${SERVER_PID}" ]]; then
        kill "${SERVER_PID}" 2>/dev/null || true
        wait "${SERVER_PID}" 2>/dev/null || true
    fi
    rm -rf "${WORK}"
}
trap cleanup EXIT

openssl req -x509 -newkey rsa:2048 -nodes -days 2 \
    -subj "/CN=localhost" -addext "subjectAltName=DNS:localhost" \
    -keyout "${WORK}/key.pem" -out "${WORK}/cert.pem" >/dev/null 2>&1

python3 "${HERE}/fixtures/make_zips.py" "${WORK}/zips"

python3 "${HERE}/fixtures/server.py" \
    --cert "${WORK}/cert.pem" --key "${WORK}/key.pem" \
    --ready "${WORK}/ports.json" &
SERVER_PID=$!

for _ in $(seq 1 100); do
    [[ -s "${WORK}/ports.json" ]] && break
    sleep 0.1
done
[[ -s "${WORK}/ports.json" ]] || { echo "fixture server did not start" >&2; exit 1; }

read -r HTTP_PORT HTTPS_PORT NTP_GOOD NTP_UNSYNCED NTP_MISMATCHED < <(python3 -c \
    'import json,sys; p=json.load(open(sys.argv[1])); print(p["http"], p["https"], p["ntp_good"], p["ntp_unsynced"], p["ntp_mismatched"])' \
    "${WORK}/ports.json")

export FIXTURE_HTTP="http://127.0.0.1:${HTTP_PORT}"
export FIXTURE_HTTPS="https://localhost:${HTTPS_PORT}"
export FIXTURE_HTTPS_IP="https://127.0.0.1:${HTTPS_PORT}"
export FIXTURE_CERT="${WORK}/cert.pem"
export FIXTURE_ZIPS="${WORK}/zips"
export FIXTURE_RARS="${HERE}/fixtures/rar"
export MOZILLA_CA="${HERE}/../romfs/cacert.pem"
export NTP_GOOD NTP_UNSYNCED NTP_MISMATCHED

if [[ -n "${FILTER}" ]]; then
    "${BINARY}" "${FILTER}"
else
    "${BINARY}"
fi
