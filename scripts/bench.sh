#!/usr/bin/env bash
# Performance benchmarks against the CLAUDE.md targets (M3 Air baseline).
# --check: exit non-zero if any target is missed (used by the Stop hook).
set -euo pipefail
source "$(dirname "$0")/env.sh"
"$ROOT/scripts/build.sh" >/dev/null
status=0
"$BUILD_DIR/calc_bench" "$@" || status=1
# Launch -> first frame. One warm-up launch (the first run after a build pays code-signature
# validation), then the median of three.
"$APP_BIN" --launch-bench >/dev/null 2>&1 || true
times=()
for i in 1 2 3; do
    t=$("$APP_BIN" --launch-bench | sed -n 's/^launch_to_first_frame_ms=\([0-9.]*\).*/\1/p')
    times+=("$t")
done
med=$(printf '%s\n' "${times[@]}" | sort -n | sed -n 2p)
ok=$(awk -v m="$med" 'BEGIN { print (m < 150) ? "OK" : "MISS" }')
printf '%-32s %8.1f ms  (target <= 150.0)  %s\n' "launch_to_first_frame_ms (p50)" "$med" "$ok"
[[ "$ok" == OK ]] || status=1
if [[ "${1:-}" == "--check" ]]; then exit $status; fi
