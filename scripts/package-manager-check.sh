#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ICEY_SOURCE_DIR="${ICEY_SOURCE_DIR:-$ROOT_DIR/../icey}"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build-release}"
CLI_VERSION="$(tr -d '[:space:]' < "$ROOT_DIR/VERSION")"
ICEY_VERSION="$(tr -d '[:space:]' < "$ICEY_SOURCE_DIR/VERSION")"
LINUX_BASENAME="icey-server-${CLI_VERSION}-Linux-x86_64"
LINUX_TARBALL="$ROOT_DIR/${LINUX_BASENAME}.tar.gz"
LINUX_ZIP="$ROOT_DIR/${LINUX_BASENAME}.zip"
LATEST_LINUX_TARBALL="$ROOT_DIR/icey-server-Linux-x86_64.tar.gz"
LATEST_LINUX_ZIP="$ROOT_DIR/icey-server-Linux-x86_64.zip"
CLI_SOURCE_ARCHIVE="$ROOT_DIR/icey-cli-${CLI_VERSION}-source.tar.gz"
ICEY_SOURCE_ARCHIVE="$ROOT_DIR/icey-${ICEY_VERSION}-source.tar.gz"
DEB_PATH="$ROOT_DIR/icey-server_${CLI_VERSION}_amd64.deb"
APT_REPO_ARCHIVE="$ROOT_DIR/icey-server-apt-repo-${CLI_VERSION}.tar.gz"
RENDERED_DIR="$ROOT_DIR/.stage/package-managers/rendered"

ICEY_SOURCE_DIR="$ICEY_SOURCE_DIR" BUILD_DIR="$BUILD_DIR" "$ROOT_DIR/scripts/build-source-archives.sh"
ICEY_SOURCE_DIR="$ICEY_SOURCE_DIR" BUILD_DIR="$BUILD_DIR" "$ROOT_DIR/scripts/package-release.sh"
ICEY_SOURCE_DIR="$ICEY_SOURCE_DIR" BUILD_DIR="$BUILD_DIR" "$ROOT_DIR/scripts/build-deb.sh"
ICEY_SOURCE_DIR="$ICEY_SOURCE_DIR" BUILD_DIR="$BUILD_DIR" "$ROOT_DIR/scripts/build-apt-repo.sh"

OUT_DIR="$RENDERED_DIR" \
ICEY_SOURCE_DIR="$ICEY_SOURCE_DIR" \
  "$ROOT_DIR/scripts/render-package-managers.sh"

tar -tzf "$LINUX_TARBALL" >/dev/null
unzip -Z1 "$LINUX_ZIP" >/dev/null
tar -tzf "$LATEST_LINUX_TARBALL" >/dev/null
unzip -Z1 "$LATEST_LINUX_ZIP" >/dev/null
dpkg-deb --contents "$DEB_PATH" >/dev/null
tar -tzf "$APT_REPO_ARCHIVE" >/dev/null

test -f "$RENDERED_DIR/homebrew/icey-server.rb"
test -f "$RENDERED_DIR/aur/PKGBUILD"
test -f "$RENDERED_DIR/apt/icey-server.list"
test -f "$RENDERED_DIR/nix/default.nix"
test -f "$RENDERED_DIR/scoop/icey-server.json"
test -f "$RENDERED_DIR/chocolatey/icey-server.nuspec"
test -f "$RENDERED_DIR/winget/0state.IceyServer.installer.yaml"
test -f "$RENDERED_DIR/SHA256SUMS.txt"
test -f "$ROOT_DIR/flake.nix"

echo "Package manager cutover artifacts validated."
