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
- Desktop sidecar transport: JSON-RPC lines over localhost TCP via `vst-test ui-server`

If the CLI command set, schema, or output contract changes, update this skill in the same PR/commit.

## Core commands

```bash
vst-test --help
vst-test inspect --plugin <path> --json
vst-test validate --plugin <path> --json
vst-test run --suite <suite.json> --out-dir .vst-test/runs --json
vst-test run --suite <suite.json> --out-dir .vst-test/runs --json-summary
vst-test ui-server --port 47555 --session desktop
vst-test baseline approve --suite <name> --run-id <run-id>
vst-test report --results <results.json>
```

## Chorus80 suite shortcuts

Use these suite templates when targeting Chorus80-like chain plugins:

- `suites/examples/chorus80.alias-quick.example.json`
- `suites/examples/chorus80.dev-quick.example.json`
- `suites/examples/chorus80.release-gate.example.json`
- `suites/examples/chorus80.nonlinear-scan.example.json`
- `suites/examples/chorus80.preset-loudness.example.json`
- `suites/examples/chorus80.analog-vibe.example.json`

Workflow scripts:

- `scripts/workflows/quick-alias.sh <plugin.vst3>`
- `scripts/workflows/dev-cycle.sh <plugin.vst3>`
- `scripts/workflows/release-cycle.sh <plugin.vst3>`
- `scripts/workflows/run-suite.sh --suite <suite.json> --plugin <plugin.vst3>`
- `scripts/workflows/gui-app.sh` (single-command desktop launcher)
- `scripts/workflows/gui-dev.sh`
- `scripts/workflows/gui-build.sh`

Desktop GUI quick actions map to:

- Quick Alias -> `chorus80.alias-quick.example.json`
- Dev Quick -> `chorus80.dev-quick.example.json`
- Release Gate -> `chorus80.release-gate.example.json`
- Nonlinear Scan -> `chorus80.nonlinear-scan.example.json`
- Preset Loudness -> `chorus80.preset-loudness.example.json`
- Release Cycle -> runs `Release Gate`, then `Nonlinear Scan`, then `Preset Loudness`

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

Case-level JSON fields to consume:

- `metrics`: numeric measurements and thresholds
- `recommendations`: remediation guidance for failed/error/crashed cases

## Quick interpretation rules

- `status=passed`: case met all thresholds (and baseline checks when enabled)
- `status=failed`: threshold or baseline gate failure
- `status=crashed`: worker crash/timeout
- `status=error`: infrastructure/runtime/plugin-load failure
- `status=skipped`: optional checks not run (e.g., optional `pluginval` unavailable)

When `testType=aliasing`, use:

- `metrics.aliasingRatioDb`: worst foldback-to-fundamental ratio from high-frequency scan
  (can mislead when the fundamental is lowpass-attenuated near Nyquist -- check absolutes)
- `metrics.aliasWorstToneDbfs`: approximate absolute level of the worst alias component (dBFS)
- `metrics.aliasWorstToneVsOutputRmsDb`: worst alias relative to analysis-window output RMS
- `artifacts.aliasingScan`: per-tone foldback scan details
- Gate `aliasWorstToneDbfsMax` (unset or 0.0 = disabled)

When `testType=latency`, use:

- `metrics.reportedLatencySamples`: plugin-reported latency
- `metrics.latencyResidualSamples`: signed residual misalignment after the engine trims
  reported latency (positive = output late, negative = early)
- `metrics.latencyErrorSamples`: absolute residual, gated by `latencyErrorSamplesMax`

When `testType=automationZipper`, optional test fields:

- `paramName`, `rampStartValue`, `rampEndValue`: ramp a named parameter between values
  (same text-or-normalized semantics as CLI `--param`); default remains first param 0..1
- Metric `zipperArtifactDb` vs gate `zipperArtifactDbMax` (unchanged)

When `testType=bypassToggleStream` (single-pass in-stream toggle click detection):

- Fields: `paramName` (default "Bypass"), `valueA`/`valueB` (default "Off"/"On"),
  `toggleAtSec` array (default 40% and 70% of the render)
- `metrics.streamToggleClickDbfs`: worst toggle click above steady-state step baseline
  (gated by `streamToggleClickDbfsMax`, default -30 when profile does not set it)
- `metrics.streamToggleClickRawDbfs` / `metrics.streamToggleBaselineStepDbfs`: raw values
- `artifacts.bypassToggleStream`: per-toggle detail JSON

When `testType=stereoPhase` (phase / mono-collapse detection, stereo output only):

- `metrics.minWindowCorrelation`: worst per-window (100 ms / 50 ms hop) L/R Pearson correlation
  (gated by `stereoCorrelationMin`, default -0.8, lower bound)
- `metrics.maxSideMidRatioDb`: worst side/mid RMS ratio (gated by `sideMidRatioDbMax`, default 18)
- `metrics.phaseCollapseWindows`: windows with sideRms > 4x midRms AND correlation < -0.5
  (gated by `phaseCollapseWindowsMax`, default 0); windows below -60 dBFS are ignored

When `testType=perfStress`, use:

- `metrics.realtimeFactor`: mean whole-render realtime factor over 5 repetitions
- `metrics.worstBlockRealtimeFactor` / `metrics.p95BlockRealtimeFactor`: per-processBlock
  timing headroom (gates `minWorstBlockRealtimeFactor` / `minP95BlockRealtimeFactor`;
  omit or 0.0 = disabled)

When `testType=abx`, use:

- `metrics.abxLoudnessDeltaDb`: loudness-match quality between A and B
- `artifacts.abxTrialsBlind` / `artifacts.abxTrialsAnswers`: listening/evaluation sheets
- `artifacts.abxInstructions`: runbook for manual ABX session

When `testType=saturationFingerprint`, use:

- `metrics.saturationWorstThdDb`: worst THD point across input sweep
- `metrics.saturationOddEvenImbalanceDb`: mean absolute odd/even balance deviation
- `artifacts.saturationFingerprintCsv`: curve data for plotting

Cross-cutting fields:

- Every case result carries a `layoutHonored` bool (and `metrics.layoutHonored`) recording
  whether the requested channel layout was honored or fell back to the plugin default;
  threshold flag `requireLayoutHonored: true` fails fallen-back cases
- `analysisWarmupSec` (per-test, default 0.75) skips the settle transient before analysis in
  `thdn`, `imd`, `aliasing`, `saturationFingerprint`
- Signal generators include `multitone` (`toneCount`), `pluck` (`frequencyHz`, `intervalSec`),
  and `diRhythm`; all deterministic for a given `--seed`

When `testType=presetGain`, use:

- `metrics.presetGainSpreadDb`: max-min output loudness spread (LUFS) across presets
- `metrics.presetInputLufs` / `metrics.presetTargetLufs`: loudness target baseline
- `artifacts.presetGainSummary`: per-variant loudness and trim recommendations
- `artifacts.presetGainAdjustmentsCsv`: spreadsheet-friendly gain adjustment table
- `artifacts.presetGainAdjustmentsMd`: readable Markdown adjustment table
- `artifacts.presetGainTrimPlan`: machine-readable trim plan
- `artifacts.presetGainTrimScript`: helper script to apply Chorus80 master output trims

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
