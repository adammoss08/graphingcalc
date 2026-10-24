#!/usr/bin/env bash
# Build, then run the unit tests. Extra args filter tests by name substring.
set -euo pipefail
source "$(dirname "$0")/env.sh"
"$ROOT/scripts/build.sh"
"$BUILD_DIR/calc_tests" "$@"
