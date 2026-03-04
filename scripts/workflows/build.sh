#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
BUILD_TYPE="${BUILD_TYPE:-Release}"

default_jobs() {
  if command -v nproc >/dev/null 2>&1; then
    nproc
    return
  fi
  if command -v sysctl >/dev/null 2>&1; then
    sysctl -n hw.ncpu
    return
  fi
  echo 8
}

BUILD_JOBS="${BUILD_JOBS:-$(default_jobs)}"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build "$BUILD_DIR" -j"$BUILD_JOBS"

echo "Built: $BUILD_DIR/Plugalyzer_artefacts/Release/vst-test"
