#!/usr/bin/env bash
# Builds dcompile if necessary and runs the game.
#
#   ./run.sh                    run the game found by scripts/find-game.sh
#   ./run.sh --game PATH        run a specific scimitar_sf executable
#   ./run.sh --log FILE         keep diagnostic output (stderr) in FILE
#
# Diagnostic switches are environment variables with a DC_ prefix; see docs/debugging.md.
set -euo pipefail
cd "$(dirname "$0")"

game=""
log=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --game) game="$2"; shift 2 ;;
        --log) log="$2"; shift 2 ;;
        -h|--help) sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

[[ -n "$game" ]] || game="$(scripts/find-game.sh)"
make --quiet

if [[ -n "$log" ]]; then
    exec ./build/dcompile "$game" 2>"$log"
fi
exec ./build/dcompile "$game"
