#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="${BIN:-$ROOT_DIR/build/Plugalyzer_artefacts/Release/vst-test}"
OUT_DIR="${OUT_DIR:-$ROOT_DIR/.vst-test/runs}"
JOBS="${JOBS:-1}"
SEED="${SEED:-1337}"
JSON_MODE="summary"
MAX_SUMMARY_CASES=12

SUITE_FILE=""
PLUGIN_PATH=""
declare -a SELECT_FILTERS=()
declare -a PASSTHROUGH_ARGS=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --suite)
      SUITE_FILE="$2"
      shift 2
      ;;
    --plugin)
      PLUGIN_PATH="$2"
      shift 2
      ;;
    --jobs)
      JOBS="$2"
      shift 2
      ;;
    --seed)
      SEED="$2"
      shift 2
      ;;
    --bin)
      BIN="$2"
      shift 2
      ;;
    --out-dir)
      OUT_DIR="$2"
      shift 2
      ;;
    --select)
      SELECT_FILTERS+=("$2")
      shift 2
      ;;
    --json-mode)
      JSON_MODE="$2"
      shift 2
      ;;
    --max-summary-cases)
      MAX_SUMMARY_CASES="$2"
      shift 2
      ;;
    --)
      shift
      PASSTHROUGH_ARGS+=("$@")
      break
      ;;
    *)
      PASSTHROUGH_ARGS+=("$1")
      shift
      ;;
  esac
done

if [[ -z "$SUITE_FILE" || -z "$PLUGIN_PATH" ]]; then
  echo "Usage: $0 --suite <suite.json> --plugin <plugin.vst3> [options]" >&2
  exit 1
fi

if [[ ! -x "$BIN" ]]; then
  echo "vst-test binary not executable: $BIN" >&2
  exit 1
fi

if [[ ! -f "$SUITE_FILE" ]]; then
  echo "suite file not found: $SUITE_FILE" >&2
  exit 1
fi

if ! command -v jq >/dev/null 2>&1; then
  echo "jq is required for run-suite.sh" >&2
  exit 1
fi

SUITE_DIR="$(cd "$(dirname "$SUITE_FILE")" && pwd)"
SUITE_NAME="$(basename "$SUITE_FILE")"
TMP_SUITE="$SUITE_DIR/.tmp.${SUITE_NAME}.$$"

cleanup() {
  rm -f "$TMP_SUITE"
}
trap cleanup EXIT

jq --arg plugin "$PLUGIN_PATH" '
  .plugins = (.plugins | map(.path = $plugin))
' "$SUITE_FILE" > "$TMP_SUITE"

mkdir -p "$OUT_DIR"

RUN_CMD=(
  "$BIN" run
  --suite "$TMP_SUITE"
  --out-dir "$OUT_DIR"
  --jobs "$JOBS"
  --seed "$SEED"
)

if [[ ${#SELECT_FILTERS[@]} -gt 0 ]]; then
  for filter in "${SELECT_FILTERS[@]}"; do
    RUN_CMD+=(--select "$filter")
  done
fi

case "$JSON_MODE" in
  summary)
    RUN_CMD+=(--json-summary --max-summary-cases "$MAX_SUMMARY_CASES")
    ;;
  full)
    RUN_CMD+=(--json)
    ;;
  none)
    ;;
  *)
    echo "invalid --json-mode: $JSON_MODE (expected summary|full|none)" >&2
    exit 1
    ;;
esac

if [[ ${#PASSTHROUGH_ARGS[@]} -gt 0 ]]; then
  RUN_CMD+=("${PASSTHROUGH_ARGS[@]}")
fi

"${RUN_CMD[@]}"
