# Chorus80 Test Iteration Plan

This file defines a practical test strategy for Chorus80-style chain plugins and records the latest local run outcomes.

## Feature-to-Test Strategy

| Chorus80 feature | Best tests | Why |
|---|---|---|
| Amp / Desk FX / Chorus Boost nonlinearity | `aliasing`, `saturationFingerprint`, `thdn`, `imd`, `bypassClickPop` | Catch foldback, harshness, intermodulation, and click behavior in drive stages. |
| Modulation blocks (Micropitch / Chorus A/B / BBD) | `automationZipper`, `phaseGroupDelay`, `latency`, `determinism` | Catch stepping, unstable delay modulation, and time variance. |
| Global lifecycle / host stability | `load`, `validate`, `stateRoundtrip`, `perfStress` | Prevent regressions in loading, state, and throughput. |
| Preset UX consistency | `presetGain`, `abx` | Keep perceived loudness and listening comparisons level matched. |

## Committed Suite Templates

- `suites/examples/chorus80.release-gate.example.json`
- `suites/examples/chorus80.nonlinear-scan.example.json`
- `suites/examples/chorus80.preset-loudness.example.json`
- `suites/examples/chorus80.analog-vibe.example.json`

## Latest Local Results (March 3, 2026)

### Run: `chorus80-release-gate-local-v2`
- Run ID: `2026-03-03T16-16-59`
- Summary: `passed=36`, `failed=0`, `skipped=6`, `total=42`
- Notes: Optional `pluginval` correctly skipped (tool behavior now matches contract when `pluginval` is missing).

### Run: `chorus80-nonlinear-scan-local-v2`
- Run ID: `2026-03-03T16-11-11`
- Summary: `passed=12`, `failed=18`, `total=30`
- Main failures:
  - `aliasing` failed for `amp`, `deskfx`, `chorusBoost` at 48k/96k.
  - `thdn` failed for the same nonlinear modules.
  - `bypassClickPop` failed for nonlinear module toggles.
- Main passes:
  - `imd` passed all nonlinear module cases.
  - `saturationFingerprint` passed all nonlinear module cases.

### Run: `chorus80-preset-loudness-local-v2`
- Run ID: `2026-03-03T16-14-07`
- Summary: `passed=1`, `failed=1`, `total=2`
- Main failure:
  - `presetGainSpreadDb = 28.81 dB` across `49` programs (target max was `2.0 dB`).
- Artifacts for fixes:
  - `preset_gain_adjustments.csv`
  - `preset_gain_adjustments.md`
  - `preset_gain_trim_plan.json`
  - `apply_chorus80_master_output_trims.py`

## Plugin Improvement Priorities

1. Reduce foldback in nonlinear stages:
   - Add/raise oversampling in `amp`, `deskfx`, `chorusBoost`.
   - Add steeper post-nonlinearity low-pass cleanup.
2. Eliminate bypass pops:
   - Add short crossfades for bypass toggles (2-10 ms).
   - Prefer zero-crossing aware switching for hard bypass states.
3. Normalize preset loudness:
   - Apply the generated trim plan to master output trims.
   - Re-run `chorus80.preset-loudness` and target spread <= `2 dB` first, then <= `1 dB`.

## Test Harness Improvement Priorities

1. Keep `pluginval` optional behavior strict:
   - Missing executable => `skipped` (done in current code).
2. Improve nonlinear quality metrics:
   - `thdn` currently penalizes intended saturation harmonics; add a dedicated "creative saturation profile" gate.
3. Improve bypass pop methodology:
   - Move from midpoint-delta comparison to timed in-stream toggle analysis with transition window scoring.
4. Improve aliasing scoring:
   - Add optional weighting by audible band and level, and report severity bands (minor/moderate/severe).

## Suggested Weekly Loop

1. Run `release-gate` and fail only on stability/perf/lifecycle regressions.
2. Run `nonlinear-scan` and track aliasing/click trends by module.
3. Run `preset-loudness`, apply trim updates, and re-run until spread target is met.
4. Run `analog-vibe` before shipping to combine all gates in one report set.
