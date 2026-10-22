#!/usr/bin/env bash
# Installs the build dependencies with Homebrew and checks the toolchain.
set -euo pipefail

if [[ "$(uname -s)" != "Darwin" || "$(uname -m)" != "arm64" ]]; then
    echo "dcompile targets macOS on Apple silicon (arm64)." >&2
    exit 1
fi

if ! xcode-select -p >/dev/null 2>&1; then
    echo "Install the Xcode command line tools first:  xcode-select --install" >&2
    exit 1
fi

if ! command -v brew >/dev/null 2>&1; then
    echo "Homebrew is required (https://brew.sh)." >&2
    exit 1
fi

brew install ffmpeg pkg-config
echo "Dependencies installed. Build with:  make"
