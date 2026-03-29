#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RENDERED_DIR="${RENDERED_DIR:-$ROOT_DIR/.stage/package-managers/rendered}"
AUR_REPO_DIR="${AUR_REPO_DIR:?Set AUR_REPO_DIR to a checked-out AUR package repository}"

for file in PKGBUILD .SRCINFO; do
  if [[ ! -f "$RENDERED_DIR/aur/$file" ]]; then
    echo "Rendered AUR file missing: $RENDERED_DIR/aur/$file" >&2
    exit 1
  fi
  cp "$RENDERED_DIR/aur/$file" "$AUR_REPO_DIR/$file"
done

echo "Published AUR package files to $AUR_REPO_DIR"
