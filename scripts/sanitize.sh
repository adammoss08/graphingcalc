#!/usr/bin/env bash
# Deep bug check (slow; run before a release): the unit tests under AddressSanitizer
# (use-after-free, out-of-bounds), UndefinedBehaviorSanitizer, then Metal API + shader validation.
# Separate Debug build dirs: build-asan, build-ubsan. Exits non-zero on the first failure.
set -euo pipefail
source "$(dirname "$0")/env.sh"
run() {  # dir, cxxflags
    local dir="$ROOT/$1"
    cmake -S "$ROOT" -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="$2 -fno-omit-frame-pointer" \
        -DCMAKE_OBJCXX_FLAGS="$2" -DCMAKE_EXE_LINKER_FLAGS="$2" >/dev/null
    cmake --build "$dir" --target calc_tests >/dev/null
    echo "== $1"
    "$dir/calc_tests"
}
run build-asan "-fsanitize=address"
run build-ubsan "-fsanitize=undefined -fno-sanitize-recover=undefined"
"$ROOT/scripts/build.sh" >/dev/null
echo "== Metal API + shader validation"
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 MTL_DEBUG_LAYER_ERROR_MODE=assert "$BUILD_DIR/calc_tests"
echo "sanitizers: all clean"
