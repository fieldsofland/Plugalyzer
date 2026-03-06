# Plugalyzer Desktop GUI

Electron + React desktop frontend for the `vst-test ui-server` sidecar.

## Requirements

- Node.js 20+
- npm 10+
- Built `vst-test` binary at `build/Plugalyzer_artefacts/Release/vst-test`

## Run (dev)

```bash
cd apps/desktop
npm install
npm run dev
```

Optional sidecar binary override:

```bash
VST_TEST_BIN=/abs/path/to/vst-test npm run dev
```

## Build

```bash
cd apps/desktop
npm run build
npm run start
```

## Core capabilities

- Plugin/version scan from `/Users/matt/dev/vst/*/build/*_artefacts/...`
- Manual scan-root override in UI settings
- Version hotswap activation
- Realtime transport controls (file load/play/pause/seek/loop)
- Quick Alias, Dev Quick, Release Gate, Nonlinear Scan, Preset Loudness, and combined Release Cycle actions
- Live meter/spectrum charts and test-type visualization panels
