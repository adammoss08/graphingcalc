# Shared environment for build scripts. Source, don't execute.
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
BUILD_TYPE="${BUILD_TYPE:-RelWithDebInfo}"
APP="$BUILD_DIR/GraphingCalc.app"
APP_BIN="$APP/Contents/MacOS/GraphingCalc"
