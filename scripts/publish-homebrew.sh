#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RENDERED_DIR="${RENDERED_DIR:-$ROOT_DIR/.stage/package-managers/rendered}"
TAP_REPO_DIR="${TAP_REPO_DIR:?Set TAP_REPO_DIR to a checked-out Homebrew tap repository}"
FORMULA_SRC="$RENDERED_DIR/homebrew/icey-server.rb"
FORMULA_DST="$TAP_REPO_DIR/Formula/icey-server.rb"

if [[ ! -f "$FORMULA_SRC" ]]; then
  echo "Rendered Homebrew formula missing: $FORMULA_SRC" >&2
  exit 1
fi

mkdir -p "$(dirname "$FORMULA_DST")"
cp "$FORMULA_SRC" "$FORMULA_DST"
echo "Published Homebrew formula to $FORMULA_DST"
