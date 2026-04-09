#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
eval "$(bash "$ROOT_DIR/scripts/release-context.sh")"
CLI_ARCHIVE="${CLI_ARCHIVE:-$CLI_SOURCE_ARCHIVE}"
ICEY_ARCHIVE="${ICEY_ARCHIVE:-$ICEY_SOURCE_ARCHIVE}"
CLI_ARCHIVE_REF="${CLI_ARCHIVE_REF:-HEAD}"
ICEY_ARCHIVE_REF="${ICEY_ARCHIVE_REF:-HEAD}"

if [[ ! -d "$ROOT_DIR/.git" ]]; then
  echo "icey-cli source tree is not a git repository: $ROOT_DIR" >&2
  exit 1
fi

if [[ ! -d "$ICEY_SOURCE_DIR/.git" ]]; then
  echo "ICEY_SOURCE_DIR does not point to an icey git repository: $ICEY_SOURCE_DIR" >&2
  exit 1
fi

git -C "$ROOT_DIR" archive \
  --format=tar.gz \
  --prefix="${CLI_SOURCE_DIRNAME}/" \
  -o "$CLI_ARCHIVE" \
  "$CLI_ARCHIVE_REF"

git -C "$ICEY_SOURCE_DIR" archive \
  --format=tar.gz \
  --prefix="${ICEY_SOURCE_DIRNAME}/" \
  -o "$ICEY_ARCHIVE" \
  "$ICEY_ARCHIVE_REF"

echo "Created source archive: $CLI_ARCHIVE"
echo "Created source archive: $ICEY_ARCHIVE"
