#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ICEY_SOURCE_DIR="${ICEY_SOURCE_DIR:-$ROOT_DIR/../icey}"
CLI_VERSION="$(tr -d '[:space:]' < "$ROOT_DIR/VERSION")"
ICEY_VERSION="$(tr -d '[:space:]' < "$ICEY_SOURCE_DIR/VERSION")"
CLI_ARCHIVE="${CLI_ARCHIVE:-$ROOT_DIR/icey-cli-${CLI_VERSION}-source.tar.gz}"
ICEY_ARCHIVE="${ICEY_ARCHIVE:-$ROOT_DIR/icey-${ICEY_VERSION}-source.tar.gz}"

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
  --prefix="icey-cli-${CLI_VERSION}/" \
  -o "$CLI_ARCHIVE" \
  HEAD

git -C "$ICEY_SOURCE_DIR" archive \
  --format=tar.gz \
  --prefix="icey-${ICEY_VERSION}/" \
  -o "$ICEY_ARCHIVE" \
  HEAD

echo "Created source archive: $CLI_ARCHIVE"
echo "Created source archive: $ICEY_ARCHIVE"
