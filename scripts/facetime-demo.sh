#!/usr/bin/env bash
# FaceTime camera to browser on a MacBook.
#
# Brings up the full pipeline:
#   mediamtx (RTSP relay) + ffmpeg (avfoundation -> RTSP) + icey-server
# All three run in the background, log output is multiplexed to stdout, and
# Ctrl-C tears them all down. icey-server adds the live latency badge,
# motion detection overlay, and voice-activity meter on the page itself.
#
# Pre-reqs (one-off):
#   brew install cmake pkg-config openssl@3 ffmpeg mediamtx
#   cmake --preset dev && cmake --build --preset dev
#   (cd web && npm install && npm run build)

set -u

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ICEY_BIN="${ICEY_BIN:-$REPO_ROOT/build-dev/src/server/icey-server}"
WEB_ROOT="${WEB_ROOT:-$REPO_ROOT/web/dist}"
MEDIAMTX_CONFIG="${MEDIAMTX_CONFIG:-$REPO_ROOT/docs/mediamtx.yml}"
FFMPEG_BIN="${FFMPEG_BIN:-ffmpeg}"
MEDIAMTX_BIN="${MEDIAMTX_BIN:-mediamtx}"

VIDEO_DEVICE="${VIDEO_DEVICE:-0}"
AUDIO_DEVICE="${AUDIO_DEVICE:-none}"  # set to 0 to capture the built-in mic
RTSP_PATH="${RTSP_PATH:-cam}"
RTSP_URL="rtsp://localhost:8554/${RTSP_PATH}"
HTTP_PORT="${HTTP_PORT:-4500}"

LOG_DIR="${LOG_DIR:-/tmp/icey-demo}"
mkdir -p "$LOG_DIR"

red()    { printf '\033[31m%s\033[0m\n' "$*"; }
green()  { printf '\033[32m%s\033[0m\n' "$*"; }
dim()    { printf '\033[2m%s\033[0m\n' "$*"; }

require() {
    if ! command -v "$1" >/dev/null 2>&1; then
        red "missing: $1"
        red "install with: brew install $2"
        exit 1
    fi
}

require ffmpeg ffmpeg
require mediamtx mediamtx

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

if [[ ! -f "$MEDIAMTX_CONFIG" ]]; then
    red "mediamtx config not found at $MEDIAMTX_CONFIG"
    exit 1
fi

PIDS=()
cleanup() {
    echo
    dim "shutting down..."
    for pid in "${PIDS[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null || true
        fi
    done
    # Give them a moment, then SIGKILL anything still running.
    sleep 0.5
    for pid in "${PIDS[@]}"; do
        kill -9 "$pid" 2>/dev/null || true
    done
    wait 2>/dev/null || true
    dim "stopped."
}
trap cleanup EXIT INT TERM

green "==> mediamtx (RTSP relay, TCP-only)"
"$MEDIAMTX_BIN" "$MEDIAMTX_CONFIG" > "$LOG_DIR/mediamtx.log" 2>&1 &
PIDS+=($!)
sleep 1

# Wait for RTSP listener.
for i in $(seq 1 20); do
    if lsof -nP -iTCP:8554 -sTCP:LISTEN >/dev/null 2>&1; then break; fi
    sleep 0.2
done
if ! lsof -nP -iTCP:8554 -sTCP:LISTEN >/dev/null 2>&1; then
    red "mediamtx never opened :8554. Last log:"
    tail -20 "$LOG_DIR/mediamtx.log"
    exit 1
fi

green "==> ffmpeg (avfoundation ${VIDEO_DEVICE}:${AUDIO_DEVICE} -> RTSP/${RTSP_PATH})"
# Build the ffmpeg command as one positional list. Bash 3.2 (the macOS
# system bash) chokes on empty-array expansion under `set -u`, so the
# audio encode flags are concatenated into a single args array instead.
ffmpeg_args=(
    -hide_banner -loglevel warning
    -f avfoundation -framerate 30 -video_size 1280x720 -pixel_format nv12
    -use_wallclock_as_timestamps 1
    -i "${VIDEO_DEVICE}:${AUDIO_DEVICE}"
    -fps_mode cfr -r 30
    -c:v libx264 -preset ultrafast -tune zerolatency -profile:v baseline
    -pix_fmt yuv420p -g 60 -b:v 2M
)
if [[ "$AUDIO_DEVICE" != "none" ]]; then
    ffmpeg_args+=(-c:a aac -b:a 128k -ar 48000 -ac 2)
fi
ffmpeg_args+=(-f rtsp -rtsp_transport tcp "$RTSP_URL")

"$FFMPEG_BIN" "${ffmpeg_args[@]}" > "$LOG_DIR/ffmpeg.log" 2>&1 &
ffmpeg_pid=$!
PIDS+=("$ffmpeg_pid")
sleep 2

if ! kill -0 "$ffmpeg_pid" 2>/dev/null; then
    red "ffmpeg exited immediately. Last log:"
    tail -30 "$LOG_DIR/ffmpeg.log"
    exit 1
fi

green "==> icey-server (--source ${RTSP_URL} --no-turn)"
"$ICEY_BIN" \
    --source "$RTSP_URL" \
    --web-root "$WEB_ROOT" \
    --port "$HTTP_PORT" \
    --no-turn \
    > "$LOG_DIR/icey.log" 2>&1 &
icey_pid=$!
PIDS+=("$icey_pid")
sleep 2

if ! kill -0 "$icey_pid" 2>/dev/null; then
    red "icey-server exited. Last log:"
    tail -30 "$LOG_DIR/icey.log"
    exit 1
fi

echo
green "ready."
echo "    open  http://localhost:${HTTP_PORT}/  and click Watch"
echo
dim "logs:  $LOG_DIR/{mediamtx,ffmpeg,icey}.log"
dim "stop:  Ctrl-C"
echo

# Tail the icey log to give a single readable feed in this terminal.
# Runs in the foreground so Ctrl-C reaches us, fires the trap, and tears
# down all three background processes.
tail -F "$LOG_DIR/icey.log" 2>/dev/null
