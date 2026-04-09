#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
eval "$(bash "$ROOT_DIR/scripts/release-context.sh")"

expected_tag="${1:-}"
if [[ -n "$expected_tag" && "$expected_tag" != "$CLI_TAG" ]]; then
  echo "expected release tag $CLI_TAG, got $expected_tag" >&2
  exit 1
fi

if ! grep -Eq "^## ${CLI_VERSION} - " "$ROOT_DIR/CHANGELOG.md"; then
  echo "CHANGELOG.md is missing a section for ${CLI_VERSION}" >&2
  exit 1
fi

section="$(
  awk '/^## '"$CLI_VERSION"' - /{found=1; next} /^## /{if(found) exit} found{print}' "$ROOT_DIR/CHANGELOG.md"
)"
printf '%s\n' "$section" | grep -Eq '[^[:space:]]' || {
  echo "CHANGELOG.md section for ${CLI_VERSION} is empty" >&2
  exit 1
}

echo "release metadata is valid for ${CLI_TAG} (icey ${ICEY_VERSION})"
