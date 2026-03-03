# vst-test (Fork of Plugalyzer)

`vst-test` is a CLI-first audio plugin testing harness for VST3/AU/LV2/LADSPA development.
It keeps Plugalyzer's offline rendering core and adds suite-based QA automation for regression testing, DSP analysis, validation, and CI reporting.

## What It Adds

- Deterministic offline rendering command (`render`, legacy alias `process`)
- Plugin inspection command (`inspect`, legacy alias `listParameters`)
- Plugin discovery scanner (`scan`)
- Validation command with optional `pluginval` integration (`validate`)
- Full suite runner with case-matrix expansion and worker subprocess isolation (`run` + hidden `worker`)
- Baseline management (`baseline approve`)
- Reporting outputs (`results.json`, `junit.xml`, `report.html`)
- Multi-case JSON contract for agent/CI consumption

## Command Overview

```bash
vst-test render ...
vst-test inspect ...
vst-test scan ...
vst-test validate ...
vst-test run --suite suite.json ...
vst-test baseline approve --suite <suite> --run-id <run-id>
vst-test report --results <results.json>
```

## Build

```bash
git clone <this-fork-url>
cd Plugalyzer
git submodule sync --recursive
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 8
```

Primary binary:

- `build/Plugalyzer_artefacts/Release/vst-test`

Legacy alias copied post-build:

- `build/Plugalyzer_artefacts/Release/plugalyzer`

## Render Command

`render` processes input audio/MIDI via the selected plugin and writes output audio.
This keeps the existing Plugalyzer workflow and parameters.

```bash
vst-test render \
  --plugin /path/to/plugin.vst3 \
  --input in.wav \
  --output out.wav \
  --param "Drive:50%" \
  --json
```

## Suite-Driven Testing

`run` loads a suite JSON, expands test cases over matrix dimensions (`sampleRates`, `blockSizes`, `channels`), and executes each case in a worker subprocess.

Outputs are written to:

- `<out-dir>/<run-id>/results.json`
- `<out-dir>/<run-id>/junit.xml`
- `<out-dir>/<run-id>/report.html`
- `<out-dir>/<run-id>/artifacts/<case-id>/...`

Example:

```bash
vst-test run --suite suites/dreamrack.json --out-dir .vst-test/runs --jobs 4 --json
```

### Supported Test Types (v1)

- `load`
- `determinism`
- `aliasing`
- `eqCurve`
- `phaseGroupDelay`
- `thdn`
- `imd`
- `noiseDc`
- `latency`
- `automationZipper`
- `bypassClickPop`
- `stateRoundtrip`
- `perfStress`
- `validate`
- `presetGain`

### Minimal Suite Example

```json
{
  "schemaVersion": 1,
  "name": "smoke-suite",
  "plugins": [
    {
      "id": "myplugin",
      "path": "/abs/path/MyPlugin.vst3",
      "format": "vst3"
    }
  ],
  "matrix": {
    "sampleRates": [44100, 48000],
    "blockSizes": [256],
    "channels": ["stereo"]
  },
  "signals": [
    { "id": "hf_sweep", "type": "logSweep", "startHz": 20, "endHz": 22000, "durationSec": 10, "levelDbfs": -18 }
  ],
  "tests": [
    { "id": "load_smoke", "type": "load", "plugin": "myplugin", "signal": "hf_sweep" },
    { "id": "alias", "type": "aliasing", "plugin": "myplugin", "signal": "hf_sweep" }
  ],
  "thresholdProfiles": {
    "default": {
      "aliasingRatioDbMax": -70,
      "eqMaxErrorDb": 1.0,
      "eqRmsErrorDb": 0.35,
      "noiseFloorDbfsMax": -90,
      "latencyErrorSamplesMax": 1,
      "presetGainSpreadDbMax": 2.0
    }
  },
  "baseline": {
    "strict": false,
    "metricTolerance": 0.5
  },
  "pluginval": {
    "required": false,
    "strictness": 5
  }
}
```

### Preset Gain Alignment Example

Use `presetGain` to load a preset set and check output level spread from a sine reference.

```json
{
  "id": "preset_gain_alignment",
  "type": "presetGain",
  "plugin": "myplugin",
  "signal": "hf_sweep",
  "presetDirectory": "/abs/path/presets",
  "presetExtensions": [".vstpreset", ".fxp"],
  "presetFrequencyHz": 1000.0,
  "presetLevelDbfs": -18.0,
  "presetDurationSec": 3.0,
  "measurementWarmupSec": 0.5,
  "measurementDurationSec": 2.0
}
```

Artifact:

- `artifacts/<case-id>/preset_gain_summary.json`

If the selected test signal is not `sine`, `presetGain` will synthesize a sine internally using `presetFrequencyHz`/`presetLevelDbfs`/`presetDurationSec`.

## Chain Adapter Example

For chain plugins (e.g. Dreamrack module isolation), attach an adapter file in plugin spec (`"adapter": "adapters/dreamrack.chain.json"`).

```json
{
  "schemaVersion": 1,
  "pluginId": "dreamrack",
  "moduleOrder": ["preamp", "eq", "compressor", "saturation", "limiter"],
  "globalInit": [
    { "param": "Mix", "value": "100%" }
  ],
  "moduleIsolation": [
    {
      "module": "eq",
      "set": [
        { "param": "Selected Module", "value": "EQ" },
        { "param": "EQ Bypass", "value": "Off" },
        { "param": "Compressor Bypass", "value": "On" }
      ]
    }
  ],
  "modeVariants": [
    { "id": "eq_os_1x", "module": "eq", "set": [{ "param": "Oversampling", "value": "1x" }] },
    { "id": "eq_os_4x", "module": "eq", "set": [{ "param": "Oversampling", "value": "4x" }] }
  ]
}
```

## Baseline Workflow

Approve a run as active baseline:

```bash
vst-test baseline approve \
  --suite dreamrack-regression \
  --run-id 2026-03-03T13-22-11 \
  --runs-root .vst-test/runs \
  --notes "Approved after QA review"
```

Baseline state is stored under:

- `.vst-test/baselines/<suite>/current/results.json`
- `.vst-test/baselines/<suite>/manifest.json`

## Validate + pluginval

`validate` performs native lifecycle checks and can also run `pluginval`:

```bash
vst-test validate \
  --plugin /abs/path/MyPlugin.vst3 \
  --strictness 5 \
  --pluginval-path /usr/local/bin/pluginval \
  --json
```

## Stable Exit Codes

- `0` success
- `1` CLI usage/config parse error
- `2` infrastructure/runtime error
- `3` test assertion/threshold failure
- `4` plugin crash or timeout
- `5` missing required dependency (e.g. required `pluginval`)
- `6` unsupported platform/format request

## Agent-Friendly Guidance

`results.json` now includes `recommendations` per case. For failures/errors/crashes, this gives machine-readable remediation steps for agents and CI annotations.

## CI

A GitHub Actions workflow is included in `.github/workflows/ci.yml`.

## License

This fork remains GPL-3.0 compatible with upstream dependencies and origin.
