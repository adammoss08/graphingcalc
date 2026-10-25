#!/usr/bin/env bash
# Build and launch the app with diagnostics. Extra args are passed through (e.g. --expr 'y=x^2',
# --open file.json, --3d).
#
# Everything the app writes to stdout/stderr is shown and also saved to build/logs/run-<time>.log.
# On exit it reports the exit status, the signal if the app crashed, and the newest crash report.
#
# Environment:
#   CALC_VERBOSE=0   quieter: only errors from the app (default 1: startup, row errors, slow frames)
#   CALC_VALIDATE=1  Metal API + shader validation (much slower; catches GPU misuse that is
#                    otherwise silent, e.g. out-of-bounds buffer reads in a shader)
set -euo pipefail
source "$(dirname "$0")/env.sh"

LOG_DIR="$BUILD_DIR/logs"
mkdir -p "$LOG_DIR"
LOG="$LOG_DIR/run-$(date +%Y%m%d-%H%M%S).log"
CRASH_DIR="$HOME/Library/Logs/DiagnosticReports"

say() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;31m==>\033[0m %s\n' "$*"; }

say "GraphingCalc $(git -C "$ROOT" describe --tags --always --dirty 2>/dev/null || echo '(no git)') on $(git -C "$ROOT" branch --show-current 2>/dev/null || echo '?')"
say "macOS $(sw_vers -productVersion), $(sysctl -n machdep.cpu.brand_string), build type $BUILD_TYPE"
say "building..."
if ! "$ROOT/scripts/build.sh" 2>&1 | tee -a "$LOG"; then
    warn "build failed (output above, also in $LOG)"
    exit 1
fi

export CALC_VERBOSE="${CALC_VERBOSE:-1}"
if [[ "${CALC_VALIDATE:-0}" != 0 ]]; then
    export MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 MTL_DEBUG_LAYER_ERROR_MODE=assert
    say "Metal API + shader validation ON (slower)"
fi
say "launching $APP_BIN $*"
say "log: $LOG"

marker="$(mktemp -t calc-run)"
set +e
"$APP_BIN" "$@" 2>&1 | tee -a "$LOG"
status=${PIPESTATUS[0]}
set -e

if [[ $status -eq 0 ]]; then
    say "exited normally"
elif [[ $status -gt 128 ]]; then
    sig=$((status - 128))
    warn "app was killed by signal $sig (SIG$(kill -l "$sig" 2>/dev/null || echo '?'))"
else
    warn "app exited with status $status"
fi

# Crash reports written during this run (macOS writes them a moment after the process dies).
if [[ $status -ne 0 ]]; then
    for _ in 1 2 3 4 5; do
        report="$(find "$CRASH_DIR" -maxdepth 1 -name 'GraphingCalc*' -newer "$marker" 2>/dev/null | head -1 || true)"
        [[ -n "$report" ]] && break
        sleep 1
    done
    if [[ -n "${report:-}" ]]; then
        warn "crash report: $report"
        # The exception type and the crashed thread's top frames are usually enough to start.
        python3 - "$report" <<'PY' 2>/dev/null || head -40 "$report"
import json, sys
text = open(sys.argv[1]).read()
header, _, body = text.partition("\n")
r = json.loads(body)
exc = r.get("exception", {})
print(f"    {exc.get('type', '?')} {exc.get('signal', '')} {exc.get('subtype', '')}".rstrip())
images = r.get("usedImages", [])
for t in r.get("threads", []):
    if not t.get("triggered"):
        continue
    for f in t.get("frames", [])[:12]:
        img = images[f["imageIndex"]].get("name", "?") if f.get("imageIndex", -1) < len(images) else "?"
        print(f"    {img:<24} {f.get('symbol', hex(f.get('imageOffset', 0)))}")
PY
    else
        warn "no crash report found in $CRASH_DIR"
    fi
fi
rm -f "$marker"
exit "$status"
