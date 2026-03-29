#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ICEY_SOURCE_DIR="${ICEY_SOURCE_DIR:-$ROOT_DIR/../icey}"
OUT_DIR="${OUT_DIR:-$ROOT_DIR/.stage/package-managers/rendered}"
TEMPLATES_DIR="$ROOT_DIR/packaging/templates"
CLI_VERSION="$(tr -d '[:space:]' < "$ROOT_DIR/VERSION")"
ICEY_VERSION="$(tr -d '[:space:]' < "$ICEY_SOURCE_DIR/VERSION")"

CLI_SOURCE_ARCHIVE="$ROOT_DIR/icey-cli-${CLI_VERSION}-source.tar.gz"
ICEY_SOURCE_ARCHIVE="$ROOT_DIR/icey-${ICEY_VERSION}-source.tar.gz"
LINUX_BASENAME="icey-server-${CLI_VERSION}-Linux-x86_64"
LINUX_TARBALL="$ROOT_DIR/${LINUX_BASENAME}.tar.gz"
LINUX_ZIP="$ROOT_DIR/${LINUX_BASENAME}.zip"
DEB_PATH="$ROOT_DIR/icey-server_${CLI_VERSION}_amd64.deb"
APT_ARCHIVE="$ROOT_DIR/icey-server-apt-repo-${CLI_VERSION}.tar.gz"

sha256_file() {
  sha256sum "$1" | awk '{print $1}'
}

sri_sha256_file() {
  local base64_hash
  base64_hash="$(openssl dgst -sha256 -binary "$1" | openssl base64 -A)"
  printf 'sha256-%s' "$base64_hash"
}

placeholder() {
  printf '%s' "$1"
}

compute_or_placeholder() {
  local file="$1"
  local default="$2"
  if [[ -f "$file" ]]; then
    sha256_file "$file"
  else
    placeholder "$default"
  fi
}

compute_sri_or_placeholder() {
  local file="$1"
  local default="$2"
  if [[ -f "$file" ]]; then
    sri_sha256_file "$file"
  else
    placeholder "$default"
  fi
}

escape_sed() {
  printf '%s' "$1" | sed -e 's/[\\/&|]/\\&/g'
}

render_template() {
  local src="$1"
  local dst="$2"

  mkdir -p "$(dirname "$dst")"

  sed \
    -e "s|@CLI_VERSION@|$(escape_sed "$CLI_VERSION")|g" \
    -e "s|@ICEY_VERSION@|$(escape_sed "$ICEY_VERSION")|g" \
    -e "s|@ICEY_CLI_SOURCE_URL@|$(escape_sed "$ICEY_CLI_SOURCE_URL")|g" \
    -e "s|@ICEY_CLI_SOURCE_SHA256@|$(escape_sed "$ICEY_CLI_SOURCE_SHA256")|g" \
    -e "s|@ICEY_CLI_SOURCE_SRI@|$(escape_sed "$ICEY_CLI_SOURCE_SRI")|g" \
    -e "s|@ICEY_SOURCE_URL@|$(escape_sed "$ICEY_SOURCE_URL")|g" \
    -e "s|@ICEY_SOURCE_SHA256@|$(escape_sed "$ICEY_SOURCE_SHA256")|g" \
    -e "s|@ICEY_SOURCE_SRI@|$(escape_sed "$ICEY_SOURCE_SRI")|g" \
    -e "s|@ICEY_CLI_SOURCE_DIRNAME@|$(escape_sed "$ICEY_CLI_SOURCE_DIRNAME")|g" \
    -e "s|@ICEY_SOURCE_DIRNAME@|$(escape_sed "$ICEY_SOURCE_DIRNAME")|g" \
    -e "s|@LINUX_TARBALL_URL@|$(escape_sed "$LINUX_TARBALL_URL")|g" \
    -e "s|@LINUX_TARBALL_SHA256@|$(escape_sed "$LINUX_TARBALL_SHA256")|g" \
    -e "s|@LINUX_ZIP_URL@|$(escape_sed "$LINUX_ZIP_URL")|g" \
    -e "s|@LINUX_ZIP_SHA256@|$(escape_sed "$LINUX_ZIP_SHA256")|g" \
    -e "s|@DEB_URL@|$(escape_sed "$DEB_URL")|g" \
    -e "s|@DEB_SHA256@|$(escape_sed "$DEB_SHA256")|g" \
    -e "s|@WINDOWS_ZIP_URL@|$(escape_sed "$WINDOWS_ZIP_URL")|g" \
    -e "s|@WINDOWS_ZIP_SHA256@|$(escape_sed "$WINDOWS_ZIP_SHA256")|g" \
    -e "s|@APT_BASE_URL@|$(escape_sed "$APT_BASE_URL")|g" \
    -e "s|@APT_SUITE@|$(escape_sed "$APT_SUITE")|g" \
    -e "s|@APT_COMPONENT@|$(escape_sed "$APT_COMPONENT")|g" \
    "$src" > "$dst"
}

ICEY_CLI_SOURCE_URL="${ICEY_CLI_SOURCE_URL:-https://github.com/nilstate/icey-cli/releases/download/v${CLI_VERSION}/icey-cli-${CLI_VERSION}-source.tar.gz}"
ICEY_SOURCE_URL="${ICEY_SOURCE_URL:-https://github.com/nilstate/icey-cli/releases/download/v${CLI_VERSION}/icey-${ICEY_VERSION}-source.tar.gz}"
ICEY_CLI_SOURCE_SHA256="${ICEY_CLI_SOURCE_SHA256:-$(compute_or_placeholder "$CLI_SOURCE_ARCHIVE" "REPLACE_WITH_ICEY_CLI_SOURCE_SHA256")}"
ICEY_SOURCE_SHA256="${ICEY_SOURCE_SHA256:-$(compute_or_placeholder "$ICEY_SOURCE_ARCHIVE" "REPLACE_WITH_ICEY_SOURCE_SHA256")}"
ICEY_CLI_SOURCE_SRI="${ICEY_CLI_SOURCE_SRI:-$(compute_sri_or_placeholder "$CLI_SOURCE_ARCHIVE" "sha256-REPLACE_WITH_ICEY_CLI_SOURCE_SRI")}"
ICEY_SOURCE_SRI="${ICEY_SOURCE_SRI:-$(compute_sri_or_placeholder "$ICEY_SOURCE_ARCHIVE" "sha256-REPLACE_WITH_ICEY_SOURCE_SRI")}"
ICEY_CLI_SOURCE_DIRNAME="${ICEY_CLI_SOURCE_DIRNAME:-icey-cli-${CLI_VERSION}}"
ICEY_SOURCE_DIRNAME="${ICEY_SOURCE_DIRNAME:-icey-${ICEY_VERSION}}"
LINUX_TARBALL_URL="${LINUX_TARBALL_URL:-https://github.com/nilstate/icey-cli/releases/download/v${CLI_VERSION}/${LINUX_BASENAME}.tar.gz}"
LINUX_TARBALL_SHA256="${LINUX_TARBALL_SHA256:-$(compute_or_placeholder "$LINUX_TARBALL" "REPLACE_WITH_LINUX_TARBALL_SHA256")}"
LINUX_ZIP_URL="${LINUX_ZIP_URL:-https://github.com/nilstate/icey-cli/releases/download/v${CLI_VERSION}/${LINUX_BASENAME}.zip}"
LINUX_ZIP_SHA256="${LINUX_ZIP_SHA256:-$(compute_or_placeholder "$LINUX_ZIP" "REPLACE_WITH_LINUX_ZIP_SHA256")}"
DEB_URL="${DEB_URL:-https://github.com/nilstate/icey-cli/releases/download/v${CLI_VERSION}/icey-server_${CLI_VERSION}_amd64.deb}"
DEB_SHA256="${DEB_SHA256:-$(compute_or_placeholder "$DEB_PATH" "REPLACE_WITH_DEB_SHA256")}"
WINDOWS_ZIP_URL="${WINDOWS_ZIP_URL:-https://github.com/nilstate/icey-cli/releases/download/v${CLI_VERSION}/icey-server-${CLI_VERSION}-Windows-x86_64.zip}"
WINDOWS_ZIP_SHA256="${WINDOWS_ZIP_SHA256:-REPLACE_WITH_WINDOWS_ZIP_SHA256}"
APT_BASE_URL="${APT_BASE_URL:-https://packages.0state.com/icey}"
APT_SUITE="${APT_SUITE:-stable}"
APT_COMPONENT="${APT_COMPONENT:-main}"

rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"

render_template \
  "$TEMPLATES_DIR/homebrew/icey-server.rb.in" \
  "$OUT_DIR/homebrew/icey-server.rb"

render_template \
  "$TEMPLATES_DIR/aur/PKGBUILD.in" \
  "$OUT_DIR/aur/PKGBUILD"

cat > "$OUT_DIR/aur/.SRCINFO" <<EOF
pkgbase = icey-server
	pkgdesc = Self-hosted source-to-browser server built on icey
	pkgver = $CLI_VERSION
	pkgrel = 1
	url = https://github.com/nilstate/icey-cli
	arch = x86_64
	license = AGPL3
	makedepends = cmake
	makedepends = gcc
	makedepends = make
	makedepends = nodejs
	makedepends = npm
	makedepends = pkgconf
	depends = ffmpeg
	depends = openssl
	source = icey-cli-${CLI_VERSION}.tar.gz::$ICEY_CLI_SOURCE_URL
	source = icey-${ICEY_VERSION}.tar.gz::$ICEY_SOURCE_URL
	sha256sums = $ICEY_CLI_SOURCE_SHA256
	sha256sums = $ICEY_SOURCE_SHA256

pkgname = icey-server
EOF

render_template \
  "$TEMPLATES_DIR/apt/icey-server.list.in" \
  "$OUT_DIR/apt/icey-server.list"

render_template \
  "$TEMPLATES_DIR/nix/default.nix.in" \
  "$OUT_DIR/nix/default.nix"

render_template \
  "$TEMPLATES_DIR/scoop/icey-server.json.in" \
  "$OUT_DIR/scoop/icey-server.json"

render_template \
  "$TEMPLATES_DIR/chocolatey/icey-server.nuspec.in" \
  "$OUT_DIR/chocolatey/icey-server.nuspec"
render_template \
  "$TEMPLATES_DIR/chocolatey/tools/chocolateyinstall.ps1.in" \
  "$OUT_DIR/chocolatey/tools/chocolateyinstall.ps1"
render_template \
  "$TEMPLATES_DIR/chocolatey/tools/chocolateyuninstall.ps1.in" \
  "$OUT_DIR/chocolatey/tools/chocolateyuninstall.ps1"

render_template \
  "$TEMPLATES_DIR/winget/0state.IceyServer.yaml.in" \
  "$OUT_DIR/winget/0state.IceyServer.yaml"
render_template \
  "$TEMPLATES_DIR/winget/0state.IceyServer.locale.en-US.yaml.in" \
  "$OUT_DIR/winget/0state.IceyServer.locale.en-US.yaml"
render_template \
  "$TEMPLATES_DIR/winget/0state.IceyServer.installer.yaml.in" \
  "$OUT_DIR/winget/0state.IceyServer.installer.yaml"

cat > "$OUT_DIR/SHA256SUMS.txt" <<EOF
$ICEY_CLI_SOURCE_SHA256  $(basename "$CLI_SOURCE_ARCHIVE")
$ICEY_SOURCE_SHA256  $(basename "$ICEY_SOURCE_ARCHIVE")
$LINUX_TARBALL_SHA256  $(basename "$LINUX_TARBALL")
$LINUX_ZIP_SHA256  $(basename "$LINUX_ZIP")
$DEB_SHA256  $(basename "$DEB_PATH")
$(compute_or_placeholder "$APT_ARCHIVE" "REPLACE_WITH_APT_REPO_SHA256")  $(basename "$APT_ARCHIVE")
EOF

echo "Rendered package manager manifests: $OUT_DIR"
