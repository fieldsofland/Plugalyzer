#!/usr/bin/env bash
set -euo pipefail

BIN=${1:-build/Plugalyzer_artefacts/Release/vst-test}
OUT_DIR=${2:-.ci-smoke}

mkdir -p "$OUT_DIR"

cat > "$OUT_DIR/suite.json" <<'JSON'
{
  "schemaVersion": 1,
  "name": "smoke-suite",
  "plugins": [{"id":"missing","path":"/does/not/exist.vst3","format":"vst3"}],
  "matrix": {"sampleRates":[44100],"blockSizes":[256],"channels":["stereo"]},
  "signals": [{"id":"silence","type":"silence","durationSec":1}],
  "tests": [{"id":"load","type":"load","plugin":"missing","signal":"silence","includeModuleIsolation":false}]
}
JSON

"$BIN" --help >/dev/null
"$BIN" run --help >/dev/null

set +e
"$BIN" run --suite "$OUT_DIR/suite.json" --out-dir "$OUT_DIR/runs" --json > "$OUT_DIR/run.json"
EXIT_CODE=$?
set -e

if [[ "$EXIT_CODE" -ne 3 ]]; then
  echo "Expected exit code 3 from failing smoke suite, got $EXIT_CODE" >&2
  exit 1
fi

echo "Smoke passed: run exited with expected code 3 and produced artifacts in $OUT_DIR/runs"
