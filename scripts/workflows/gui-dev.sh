#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
APP_DIR="$ROOT_DIR/apps/desktop"

cd "$APP_DIR"

if [[ ! -d node_modules ]]; then
  npm install
fi

npm run dev
