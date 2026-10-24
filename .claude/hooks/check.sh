#!/usr/bin/env bash
# Stop hook: if sources changed since the last green check, build + run tests + perf gate (bench --check).
# On failure, exit 2 so Claude keeps working (stderr is shown to Claude). Gives up after 3 blocks in a
# row, to avoid an endless loop when a failure needs the user.
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
STATE="$ROOT/build/.claude-check"
mkdir -p "$ROOT/build"
[[ -f "$ROOT/CMakeLists.txt" ]] || exit 0
sig=$(find "$ROOT/src" "$ROOT/shaders" "$ROOT/tests" "$ROOT/CMakeLists.txt" -type f \
        \( -name '*.cpp' -o -name '*.hpp' -o -name '*.mm' -o -name '*.metal' -o -name '*.txt' \) \
        -exec stat -f '%m %N' {} + 2>/dev/null | sort | shasum | cut -c1-40)
[[ -f "$STATE.ok" && "$(cat "$STATE.ok")" == "$sig" ]] && exit 0
if out=$("$ROOT/scripts/test.sh" 2>&1 && "$ROOT/scripts/bench.sh" --check 2>&1); then
    echo "$sig" > "$STATE.ok"; rm -f "$STATE.fails"; exit 0
fi
fails=$(( $(cat "$STATE.fails" 2>/dev/null || echo 0) + 1 )); echo "$fails" > "$STATE.fails"
if (( fails > 3 )); then
    rm -f "$STATE.fails"
    echo "check.sh: build/tests still failing after 3 attempts; tell the user what is broken." >&2
    exit 0
fi
{ echo "Build or tests are failing. Fix before finishing:"; echo "$out" | grep -E "error|FAIL|Assert|MISS|warning: unused" | head -30; echo "..."; echo "$out" | tail -15; } >&2
exit 2
