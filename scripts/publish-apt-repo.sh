#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APT_REPO_ROOT="${APT_REPO_ROOT:-$ROOT_DIR/.stage/apt-repo}"
PACKAGES_REPO_DIR="${PACKAGES_REPO_DIR:?Set PACKAGES_REPO_DIR to the checked out package repository}"
APT_REPO_PATH="${APT_REPO_PATH:-icey}"
DEST_DIR="$PACKAGES_REPO_DIR/$APT_REPO_PATH"

if [[ ! -d "$APT_REPO_ROOT/dists" ]]; then
  echo "APT repository root missing or incomplete: $APT_REPO_ROOT" >&2
  exit 1
fi

if [[ ! -d "$PACKAGES_REPO_DIR/.git" ]]; then
  echo "Package repository root missing or not a git checkout: $PACKAGES_REPO_DIR" >&2
  exit 1
fi

mkdir -p "$PACKAGES_REPO_DIR"
touch "$PACKAGES_REPO_DIR/.nojekyll"

rm -rf "$DEST_DIR"
mkdir -p "$DEST_DIR"
cp -R "$APT_REPO_ROOT"/. "$DEST_DIR/"

cat > "$PACKAGES_REPO_DIR/index.html" <<'EOF'
<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Icey Packages</title>
  </head>
  <body>
    <main>
      <h1>Icey Packages</h1>
      <p>Static package repositories for Icey distributions.</p>
      <ul>
        <li><a href="./icey/">Icey APT repository</a></li>
      </ul>
    </main>
  </body>
</html>
EOF

cat > "$DEST_DIR/index.html" <<'EOF'
<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Icey APT Repository</title>
  </head>
  <body>
    <main>
      <h1>Icey APT Repository</h1>
      <p>This path hosts the static APT repository for <code>icey-server</code>.</p>
    </main>
  </body>
</html>
EOF

echo "Published APT repository to $DEST_DIR"
