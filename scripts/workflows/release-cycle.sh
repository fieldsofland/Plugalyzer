#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
PLUGIN_PATH="${1:-}"

if [[ -z "$PLUGIN_PATH" ]]; then
  echo "Usage: $0 <path-to-plugin.vst3> [--skip-build] [extra run args]" >&2
  exit 1
fi

shift || true

DO_BUILD=1
declare -a EXTRA_ARGS=()
for arg in "$@"; do
  if [[ "$arg" == "--skip-build" ]]; then
    DO_BUILD=0
  else
    EXTRA_ARGS+=("$arg")
  fi
done

if [[ "$DO_BUILD" -eq 1 ]]; then
  "$ROOT_DIR/scripts/workflows/build.sh"
fi

SUITES=(
  "$ROOT_DIR/suites/examples/chorus80.release-gate.example.json"
  "$ROOT_DIR/suites/examples/chorus80.nonlinear-scan.example.json"
  "$ROOT_DIR/suites/examples/chorus80.preset-loudness.example.json"
)

OVERALL_EXIT=0
for suite in "${SUITES[@]}"; do
  echo "=== Running $(basename "$suite") ==="
  RUN_ARGS=(
    --suite "$suite"
    --plugin "$PLUGIN_PATH"
    --jobs "${JOBS:-1}"
    --json-mode summary
    --max-summary-cases 12
  )

  if [[ ${#EXTRA_ARGS[@]} -gt 0 ]]; then
    RUN_ARGS+=("${EXTRA_ARGS[@]}")
  fi

  set +e
  "$ROOT_DIR/scripts/workflows/run-suite.sh" "${RUN_ARGS[@]}"
  CODE=$?
  set -e

  if [[ "$CODE" -gt "$OVERALL_EXIT" ]]; then
    OVERALL_EXIT="$CODE"
  fi
done

exit "$OVERALL_EXIT"
