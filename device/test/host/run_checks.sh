#!/usr/bin/env bash
# Host-side checks for firmware logic that has no hardware dependency.
# Needs g++ and python3 (or uv). Run from anywhere.
set -euo pipefail
cd "$(dirname "$0")/../.."
if command -v uv >/dev/null 2>&1; then
    UV_CACHE_DIR="${UV_CACHE_DIR:-${TMPDIR:-/tmp}/nazer-uv}" uv run --no-project python test/host/make_test_pack.py
else
    python3 test/host/make_test_pack.py
fi
out="${TMPDIR:-/tmp}/nazer-host-checks"
mkdir -p "$out"
for check in roadlimits power_policy queue ble_auth claim_response; do
    g++ -std=c++11 -Wall -Wextra -Werror -pedantic "test/host/test_${check}.cpp" -o "$out/${check}"
    "$out/${check}"
    echo "PASS ${check}"
done
