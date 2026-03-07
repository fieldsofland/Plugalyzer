#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
APP_DIR="$ROOT_DIR/apps/desktop"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
VST_TEST_BIN_DEFAULT="$BUILD_DIR/Plugalyzer_artefacts/Release/vst-test"

MODE="prod"
SKIP_NATIVE_BUILD=0
SKIP_DESKTOP_BUILD=0
DRY_RUN=0

usage() {
  cat <<'EOF'
Usage: scripts/workflows/gui-app.sh [options]

Options:
  --dev                  Run Electron in dev mode (Vite + hot reload)
  --prod                 Run Electron from built desktop bundles (default)
  --skip-native-build    Do not run native C++ build step
  --skip-desktop-build   Do not run desktop npm build step
  --dry-run              Print planned actions without executing
  -h, --help             Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --dev)
      MODE="dev"
      ;;
    --prod)
      MODE="prod"
      ;;
    --skip-native-build)
      SKIP_NATIVE_BUILD=1
      ;;
    --skip-desktop-build)
      SKIP_DESKTOP_BUILD=1
      ;;
    --dry-run)
      DRY_RUN=1
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
  shift
done

run() {
  if [[ "$DRY_RUN" -eq 1 ]]; then
    echo "[dry-run] $*"
    return 0
  fi
  echo "==> $*"
  "$@"
}

if [[ "$SKIP_NATIVE_BUILD" -eq 0 ]]; then
  run "$ROOT_DIR/scripts/workflows/build.sh"
fi

VST_TEST_BIN="${VST_TEST_BIN:-$VST_TEST_BIN_DEFAULT}"
if [[ ! -x "$VST_TEST_BIN" ]]; then
  echo "Missing vst-test binary: $VST_TEST_BIN" >&2
  echo "Build first with scripts/workflows/build.sh or set VST_TEST_BIN" >&2
  exit 1
fi

if [[ ! -d "$APP_DIR/node_modules" ]]; then
  run bash -lc "cd \"$APP_DIR\" && npm install"
fi

if [[ "$MODE" == "prod" && "$SKIP_DESKTOP_BUILD" -eq 0 ]]; then
  run bash -lc "cd \"$APP_DIR\" && npm run build"
fi

if [[ "$MODE" == "prod" ]]; then
  if [[ ! -f "$APP_DIR/dist/main/main.js" || ! -f "$APP_DIR/dist/renderer/index.html" ]]; then
    echo "Desktop app is not built (missing dist files)." >&2
    echo "Run scripts/workflows/gui-app.sh (without --skip-desktop-build) or npm run build in apps/desktop." >&2
    exit 1
  fi
  run bash -lc "cd \"$APP_DIR\" && VST_TEST_BIN=\"$VST_TEST_BIN\" npm run start"
else
  run bash -lc "cd \"$APP_DIR\" && VST_TEST_BIN=\"$VST_TEST_BIN\" npm run dev"
fi
