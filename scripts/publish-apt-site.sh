#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APT_REPO_ROOT="${APT_REPO_ROOT:-$ROOT_DIR/.stage/apt-repo}"
SITE_ROOT="${SITE_ROOT:?Set SITE_ROOT to the document root for the published package site}"
DEST_DIR="$SITE_ROOT/apt"

if [[ ! -d "$APT_REPO_ROOT/dists" ]]; then
  echo "APT repository root missing or incomplete: $APT_REPO_ROOT" >&2
  exit 1
fi

rm -rf "$DEST_DIR"
mkdir -p "$DEST_DIR"
cp -R "$APT_REPO_ROOT"/. "$DEST_DIR/"

cat > "$SITE_ROOT/index.html" <<'EOF'
<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8">
    <title>icey package repositories</title>
  </head>
  <body>
    <p>APT repository: <a href="./apt/">./apt/</a></p>
  </body>
</html>
EOF

echo "Published APT site to $SITE_ROOT"
