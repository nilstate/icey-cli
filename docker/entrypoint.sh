#!/bin/sh
set -eu

mode="${ICEY_MODE:-stream}"

set -- /usr/local/bin/icey-server \
  --host "${ICEY_HOST:-0.0.0.0}" \
  --port "${ICEY_PORT:-4500}" \
  --turn-port "${ICEY_TURN_PORT:-3478}" \
  --mode "${mode}" \
  --web-root "${ICEY_WEB_ROOT:-/app/web}"

case "${mode}" in
  stream)
    set -- "$@" --source "${ICEY_SOURCE:-/app/media/test.mp4}"
    ;;
  record)
    set -- "$@" --record-dir "${ICEY_RECORD_DIR:-/app/recordings}"
    ;;
  relay)
    ;;
  *)
    echo "Unsupported ICEY_MODE: ${mode}" >&2
    exit 1
    ;;
esac

case "${ICEY_LOOP:-auto}" in
  1|true|TRUE|yes|YES)
    set -- "$@" --loop
    ;;
  0|false|FALSE|no|NO)
    set -- "$@" --no-loop
    ;;
  auto)
    ;;
  *)
    echo "Unsupported ICEY_LOOP: ${ICEY_LOOP}" >&2
    exit 1
    ;;
esac

if [ "${ICEY_DISABLE_TURN:-0}" = "1" ]; then
  set -- "$@" --no-turn
fi

if [ -n "${ICEY_TURN_EXTERNAL_IP:-}" ]; then
  set -- "$@" --turn-external-ip "${ICEY_TURN_EXTERNAL_IP}"
fi

if [ "${ICEY_DOCTOR:-0}" = "1" ]; then
  set -- "$@" --doctor
fi

exec "$@"
