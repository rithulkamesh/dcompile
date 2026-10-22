#!/usr/bin/env bash
# Prints the path of the game's main executable, or exits with status 1 if it cannot be found.
#
# Lookup order: $DCOMPILE_GAME, then the default Steam library on macOS.
set -euo pipefail

executable="Assassin's Creed 2.app/Contents/MacOS/scimitar_sf"
candidates=(
    "${DCOMPILE_GAME:-}"
    "$HOME/Library/Application Support/Steam/steamapps/common/Assassin's Creed 2/$executable"
)

for path in "${candidates[@]}"; do
    if [[ -n "$path" && -f "$path" ]]; then
        printf '%s\n' "$path"
        exit 0
    fi
done

echo "Could not find the game. Install it from Steam, or point DCOMPILE_GAME at its scimitar_sf executable." >&2
exit 1
