#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ICEY_SOURCE_DIR="${ICEY_SOURCE_DIR:-$ROOT_DIR/../icey}"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build-dev}"
STAGE_DIR="${STAGE_DIR:-$ROOT_DIR/.stage/release-check}"

if [[ ! -f "$ICEY_SOURCE_DIR/CMakeLists.txt" ]]; then
  echo "ICEY_SOURCE_DIR does not point to an icey source tree: $ICEY_SOURCE_DIR" >&2
  exit 1
fi

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DICEY_SOURCE_DIR="$ICEY_SOURCE_DIR"
cmake --build "$BUILD_DIR" -j1 --target icey-server

npm --prefix "$ROOT_DIR/web" ci
npm --prefix "$ROOT_DIR/web" run build

rm -rf "$STAGE_DIR"
cmake --install "$BUILD_DIR" --prefix "$STAGE_DIR" --component apps

if [[ ! -x "$STAGE_DIR/bin/icey-server" ]]; then
  echo "Installed binary missing from staged layout: $STAGE_DIR/bin/icey-server" >&2
  exit 1
fi

if [[ ! -f "$STAGE_DIR/share/icey-server/config.example.json" ]]; then
  echo "Installed config example missing from staged layout: $STAGE_DIR/share/icey-server/config.example.json" >&2
  exit 1
fi

if [[ ! -f "$STAGE_DIR/share/icey-server/config.rtsp.example.json" ]]; then
  echo "Installed RTSP config example missing from staged layout: $STAGE_DIR/share/icey-server/config.rtsp.example.json" >&2
  exit 1
fi

if [[ ! -f "$STAGE_DIR/share/icey-server/web/index.html" ]]; then
  echo "Installed web UI missing from staged layout: $STAGE_DIR/share/icey-server/web/index.html" >&2
  exit 1
fi

"$STAGE_DIR/bin/icey-server" --version >/dev/null

if "$STAGE_DIR/bin/icey-server" --doctor >/dev/null 2>&1; then
  echo "Expected default preflight to fail without a configured source" >&2
  exit 1
fi

"$STAGE_DIR/bin/icey-server" \
  --config "$STAGE_DIR/share/icey-server/config.rtsp.example.json" \
  --doctor >/dev/null

if [[ -z "${MEDIA_SERVER_BROWSER:-}" ]]; then
  if command -v google-chrome >/dev/null 2>&1; then
    export MEDIA_SERVER_BROWSER="$(command -v google-chrome)"
  elif command -v chromium >/dev/null 2>&1; then
    export MEDIA_SERVER_BROWSER="$(command -v chromium)"
  fi
fi

npm --prefix "$ROOT_DIR/web" run test:smoke:chromium
