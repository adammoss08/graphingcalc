#!/usr/bin/env bash
# Build and launch the app. Extra args are passed through (e.g. --expr 'y=x^2', --open file.json).
set -euo pipefail
source "$(dirname "$0")/env.sh"
"$ROOT/scripts/build.sh"
exec "$APP_BIN" "$@"
