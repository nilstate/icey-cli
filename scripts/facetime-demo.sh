#!/usr/bin/env bash
# FaceTime camera to browser on a MacBook.
#
# Single-process: icey-server opens AVFoundation directly via the
# `avfoundation:` URL scheme. No mediamtx, no ffmpeg subprocess, no
# RTSP relay. Ctrl-C tears it down. icey-server adds the live latency
# badge, motion detection overlay, and voice-activity meter on the
# page itself.
#
# Pre-reqs (one-off):
#   brew install cmake pkg-config openssl@3 ffmpeg
#   cmake --preset dev && cmake --build --preset dev
#   (cd web && npm install && npm run build)
#
# Audio is off by default to avoid the FaceTime-mic-into-speakers
# feedback loop on a single-Mac demo. Set AUDIO_DEVICE=0 to enable
# mic capture (browser speaker is default-muted, so it's safe).

set -u

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ICEY_BIN="${ICEY_BIN:-$REPO_ROOT/build-dev/src/server/icey-server}"
WEB_ROOT="${WEB_ROOT:-$REPO_ROOT/web/dist}"

VIDEO_DEVICE="${VIDEO_DEVICE:-0}"
AUDIO_DEVICE="${AUDIO_DEVICE:-none}"
HTTP_PORT="${HTTP_PORT:-4500}"

LOG_DIR="${LOG_DIR:-/tmp/icey-demo}"
mkdir -p "$LOG_DIR"

red()    { printf '\033[31m%s\033[0m\n' "$*"; }
green()  { printf '\033[32m%s\033[0m\n' "$*"; }
dim()    { printf '\033[2m%s\033[0m\n' "$*"; }

if [[ ! -x "$ICEY_BIN" ]]; then
    red "icey-server not found at $ICEY_BIN"
    red "build with: cmake --preset dev && cmake --build --preset dev"
    exit 1
fi

if [[ ! -f "$WEB_ROOT/index.html" ]]; then
    red "web UI not built at $WEB_ROOT"
    red "build with: (cd web && npm install && npm run build)"
    exit 1
fi

SOURCE="avfoundation:${VIDEO_DEVICE}:${AUDIO_DEVICE}"

cleanup() {
    echo
    dim "shutting down..."
    if [[ -n "${ICEY_PID:-}" ]] && kill -0 "$ICEY_PID" 2>/dev/null; then
        kill "$ICEY_PID" 2>/dev/null || true
        sleep 0.5
        kill -9 "$ICEY_PID" 2>/dev/null || true
    fi
    wait 2>/dev/null || true
    dim "stopped."
}
trap cleanup EXIT INT TERM

green "==> icey-server (--source ${SOURCE} --no-turn)"
"$ICEY_BIN" \
    --source "$SOURCE" \
    --web-root "$WEB_ROOT" \
    --port "$HTTP_PORT" \
    --no-turn \
    > "$LOG_DIR/icey.log" 2>&1 &
ICEY_PID=$!
sleep 2

if ! kill -0 "$ICEY_PID" 2>/dev/null; then
    red "icey-server exited. Last log:"
    tail -30 "$LOG_DIR/icey.log"
    exit 1
fi

echo
green "ready."
echo "    open  http://localhost:${HTTP_PORT}/  and click Watch"
echo
dim "log:   $LOG_DIR/icey.log"
dim "stop:  Ctrl-C"
echo

# Tail the icey log so the user has a single readable feed in this
# terminal. Foreground so Ctrl-C reaches us, fires the trap, and
# tears the server down cleanly.
tail -F "$LOG_DIR/icey.log" 2>/dev/null
