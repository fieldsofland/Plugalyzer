#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "Usage: $0 <results.json> [max_cases]" >&2
  exit 1
fi

RESULTS_FILE="$1"
MAX_CASES="${2:-12}"

if [[ ! -f "$RESULTS_FILE" ]]; then
  echo "results file not found: $RESULTS_FILE" >&2
  exit 1
fi

if ! command -v jq >/dev/null 2>&1; then
  echo "jq is required for summarize-results.sh" >&2
  exit 1
fi

jq --argjson max_cases "$MAX_CASES" '
def compact_metrics:
  (.metrics // {}) | to_entries | .[:8] | from_entries;

{
  runId,
  suite,
  summary,
  resultsFile: input_filename,
  nonPassing: (
    [.cases[]
      | select(.status != "passed" and .status != "skipped")
      | {
          id,
          status,
          testType,
          message: (.message // ""),
          recommendation: ((.recommendations // [])[0] // ""),
          metrics: compact_metrics,
          artifacts: {
            reproCmd: (.artifacts.reproCmd // ""),
            workerLog: (.artifacts.workerLog // "")
          }
        }
    ] as $cases
    | {
        total: ($cases | length),
        shown: (($cases[:$max_cases]) | length),
        truncated: (($cases | length) > (($cases[:$max_cases]) | length)),
        cases: ($cases[:$max_cases])
      }
  )
}
' "$RESULTS_FILE"
