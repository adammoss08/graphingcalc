#!/usr/bin/env bash
# Render the app offscreen with the given expressions and write a PNG, then exit.
# Usage: scripts/screenshot.sh out.png [--size 1400x900] [--select N] [--open f.json] [--image f.png]
#        [--style-menu N] [--keypad] [--perf-hud] [--click x,y]... [latex ...]
# Needs no screen-recording permission: the app renders one frame to a texture and saves it itself.
set -euo pipefail
source "$(dirname "$0")/env.sh"
out="$1"; shift
"$ROOT/scripts/build.sh" >/dev/null
args=(--screenshot "$out")
while [[ $# -gt 0 ]]; do
    case "$1" in
        --size|--select|--view|--open|--hover|--image|--style-menu|--click) args+=("$1" "$2"); shift 2 ;;
        --keypad|--perf-hud) args+=("$1"); shift ;;
        *) args+=(--expr "$1"); shift ;;
    esac
done
"$APP_BIN" "${args[@]}"
echo "wrote $out"
