#!/usr/bin/env bash
# End-to-end smoke test: boots the game for a while and checks that it loads, renders frames and does not stop on an error.
# Needs the game installed and a display (a window opens). Usage: scripts/smoke-test.sh [seconds]
set -euo pipefail
cd "$(dirname "$0")/.."

seconds="${1:-45}"
game="$(scripts/find-game.sh)"
make --quiet

output="$(DC_MAXSECS="$seconds" ./build/dcompile "$game" 2>&1 || true)"

fail() { echo "FAIL: $1" >&2; echo "$output" | tail -25 >&2; exit 1; }

grep -q "recognised build" <<<"$output" || fail "build not recognised"
grep -q "UNIMPLEMENTED IMPORT" <<<"$output" && fail "unimplemented import"
grep -q "STOPPED: wall-clock limit reached" <<<"$output" || fail "stopped before the time limit"
frames="$(sed -n 's/.* \([0-9][0-9]*\) frames flipped.*/\1/p' <<<"$output" | head -1)"
[[ "${frames:-0}" -gt 0 ]] || fail "no frames were presented"

echo "ok: $frames frames in ${seconds}s"
