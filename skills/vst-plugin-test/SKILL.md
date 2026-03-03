---
name: vst-plugin-test
description: Run and interpret vst-test CLI suites for plugin QA, including DSP metrics, baseline gating, and CI-ready artifacts.
---

# vst-plugin-test skill

Use this skill when the task is about automated plugin QA, regression testing, or validating DSP behavior with the `vst-test` CLI in this repository.

## Tool version contract

Current tool contract target:

- CLI binary: `vst-test`
- Schema version: `1`
- Run output contract: `results.json`, `junit.xml`, `report.html`

If the CLI command set, schema, or output contract changes, update this skill in the same PR/commit.

## Core commands

```bash
vst-test --help
vst-test inspect --plugin <path> --json
vst-test validate --plugin <path> --json
vst-test run --suite <suite.json> --out-dir .vst-test/runs --json
vst-test baseline approve --suite <name> --run-id <run-id>
vst-test report --results <results.json>
```

## Standard workflow

1. Validate plugin path and inspect parameters.
2. Run suite with deterministic seed and explicit output dir.
3. Parse `results.json` and summarize failed/crashed/skipped cases.
4. Highlight threshold failures with metric values and gates.
5. For regressions, compare against baseline note in manifest.
6. Provide repro path from case artifacts (`repro.sh`, `worker.log`, rendered WAV).

## Expected artifacts

Under `<out-dir>/<run-id>/`:

- `results.json`: canonical machine-readable result
- `junit.xml`: CI test view
- `report.html`: human summary
- `artifacts/<case-id>/`: per-case outputs/logs/repro

## Quick interpretation rules

- `status=passed`: case met all thresholds (and baseline checks when enabled)
- `status=failed`: threshold or baseline gate failure
- `status=crashed`: worker crash/timeout
- `status=error`: infrastructure/runtime/plugin-load failure
- `status=skipped`: optional checks not run (e.g., optional `pluginval` unavailable)

## Exit codes

- `0` success
- `1` CLI usage/config error
- `2` infrastructure/runtime error
- `3` test failure
- `4` crash/timeout
- `5` missing required dependency
- `6` unsupported request

## Maintenance policy (required)

Whenever a new `vst-test` version introduces any of the following, update this skill file immediately:

1. New/removed/renamed command or flag.
2. Schema changes in suite/adapter/results.
3. New status values or exit-code behavior.
4. New artifact paths or report formats.
5. Changes to baseline approval semantics.

Do not leave this skill stale relative to current CLI behavior.
