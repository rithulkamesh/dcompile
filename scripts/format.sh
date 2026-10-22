#!/usr/bin/env bash
# Formats all sources with clang-format (style in .clang-format).  Use --check to only verify.
set -euo pipefail
cd "$(dirname "$0")/.."

files=$(find src -name '*.h' -o -name '*.c' -o -name '*.m')
if [[ "${1:-}" == "--check" ]]; then
    # shellcheck disable=SC2086
    clang-format --dry-run --Werror $files
else
    # shellcheck disable=SC2086
    clang-format -i $files
fi
