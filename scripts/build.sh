#!/usr/bin/env bash
# Configure (if needed) and build the app + tests.
set -euo pipefail
source "$(dirname "$0")/env.sh"
if [[ ! -f "$BUILD_DIR/build.ninja" ]]; then
    cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE="$BUILD_TYPE" >/dev/null
fi
cmake --build "$BUILD_DIR" "$@"
