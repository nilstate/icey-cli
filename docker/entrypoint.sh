#!/bin/sh
set -eu

# icey-server reads ICEY_* environment variables natively (ICEY_HOST,
# ICEY_PORT, ICEY_TURN_PORT, ICEY_MODE, ICEY_SOURCE, ICEY_RECORD_DIR,
# ICEY_WEB_ROOT, ICEY_TLS_CERT/KEY, ICEY_LOOP, ICEY_TURN, ICEY_CONFIG,
# ICEY_LOG_LEVEL, ICEY_AUTH_TOKEN, ICEY_TURN_SECRET). This wrapper only
# provides container-specific defaults and the ICEY_DOCTOR convenience.

export ICEY_WEB_ROOT="${ICEY_WEB_ROOT:-/app/web}"
export ICEY_RECORD_DIR="${ICEY_RECORD_DIR:-/app/recordings}"

if [ "${ICEY_MODE:-stream}" = "stream" ]; then
  export ICEY_SOURCE="${ICEY_SOURCE:-/app/media/test.mp4}"
fi

# Back-compat: ICEY_DISABLE_TURN=1 predates native ICEY_TURN support.
if [ "${ICEY_DISABLE_TURN:-0}" = "1" ]; then
  export ICEY_TURN=false
fi

set -- /usr/local/bin/icey-server

if [ "${ICEY_DOCTOR:-0}" = "1" ]; then
  set -- "$@" --doctor
fi

exec "$@"
