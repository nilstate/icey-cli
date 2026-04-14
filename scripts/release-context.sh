#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ICEY_SOURCE_DIR="${ICEY_SOURCE_DIR:-$ROOT_DIR/../icey}"
CLI_VERSION="$(tr -d '[:space:]' < "$ROOT_DIR/VERSION")"
ICEY_VERSION="$(tr -d '[:space:]' < "$ROOT_DIR/ICEY_VERSION")"

if [[ ! "$CLI_VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "VERSION must contain a plain semantic version, got: $CLI_VERSION" >&2
  exit 1
fi

if [[ ! "$ICEY_VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "ICEY_VERSION must contain a plain semantic version, got: $ICEY_VERSION" >&2
  exit 1
fi

if [[ -f "$ICEY_SOURCE_DIR/VERSION" ]]; then
  actual_icey_version="$(tr -d '[:space:]' < "$ICEY_SOURCE_DIR/VERSION")"
  if [[ "$actual_icey_version" != "$ICEY_VERSION" ]]; then
    echo "ICEY_SOURCE_DIR version mismatch: expected $ICEY_VERSION from ICEY_VERSION, found $actual_icey_version at $ICEY_SOURCE_DIR" >&2
    exit 1
  fi
fi

ICEY_HEAD_COMMIT=""
ICEY_LOCAL_TAG_COMMIT=""
ICEY_REMOTE_TAG_COMMIT=""
if [[ -f "$ICEY_SOURCE_DIR/scripts/release-manifest.sh" ]]; then
  eval "$(RELEASE_FETCH_ARCHIVE_META=0 bash "$ICEY_SOURCE_DIR/scripts/release-manifest.sh" "$ICEY_VERSION")"
  ICEY_HEAD_COMMIT="$RELEASE_HEAD_COMMIT"
  ICEY_LOCAL_TAG_COMMIT="$RELEASE_LOCAL_TAG_COMMIT"
  ICEY_REMOTE_TAG_COMMIT="$RELEASE_REMOTE_TAG_COMMIT"

  if [[ -n "$ICEY_LOCAL_TAG_COMMIT" && "$ICEY_HEAD_COMMIT" != "$ICEY_LOCAL_TAG_COMMIT" ]]; then
    echo "ICEY_SOURCE_DIR local tag mismatch: expected exact icey tag $ICEY_VERSION at $ICEY_LOCAL_TAG_COMMIT, found checkout at $ICEY_HEAD_COMMIT" >&2
    exit 1
  fi

  if [[ -n "$ICEY_REMOTE_TAG_COMMIT" && "$ICEY_HEAD_COMMIT" != "$ICEY_REMOTE_TAG_COMMIT" ]]; then
    echo "ICEY_SOURCE_DIR remote tag mismatch: expected exact upstream icey tag $ICEY_VERSION at $ICEY_REMOTE_TAG_COMMIT, found checkout at $ICEY_HEAD_COMMIT" >&2
    exit 1
  fi
fi

CLI_TAG="v${CLI_VERSION}"
ICEY_TAG="${ICEY_VERSION}"
CLI_SOURCE_DIRNAME="icey-cli-${CLI_VERSION}"
ICEY_SOURCE_DIRNAME="icey-${ICEY_VERSION}"
LINUX_BASENAME="icey-server-${CLI_VERSION}-Linux-x86_64"
WINDOWS_BASENAME="icey-server-${CLI_VERSION}-Windows-x86_64"
CLI_SOURCE_ARCHIVE="$ROOT_DIR/${CLI_SOURCE_DIRNAME}-source.tar.gz"
ICEY_SOURCE_ARCHIVE="$ROOT_DIR/${ICEY_SOURCE_DIRNAME}-source.tar.gz"
LINUX_TARBALL="$ROOT_DIR/${LINUX_BASENAME}.tar.gz"
LINUX_ZIP="$ROOT_DIR/${LINUX_BASENAME}.zip"
LATEST_LINUX_TARBALL="$ROOT_DIR/icey-server-Linux-x86_64.tar.gz"
LATEST_LINUX_ZIP="$ROOT_DIR/icey-server-Linux-x86_64.zip"
WINDOWS_ZIP="$ROOT_DIR/${WINDOWS_BASENAME}.zip"
DEB_PATH="$ROOT_DIR/icey-server_${CLI_VERSION}_amd64.deb"
APT_REPO_ARCHIVE="$ROOT_DIR/icey-server-apt-repo-${CLI_VERSION}.tar.gz"
CLI_SOURCE_URL="https://github.com/nilstate/icey-cli/releases/download/${CLI_TAG}/${CLI_SOURCE_DIRNAME}-source.tar.gz"
ICEY_SOURCE_URL="https://github.com/nilstate/icey-cli/releases/download/${CLI_TAG}/${ICEY_SOURCE_DIRNAME}-source.tar.gz"
LINUX_TARBALL_URL="https://github.com/nilstate/icey-cli/releases/download/${CLI_TAG}/${LINUX_BASENAME}.tar.gz"
LINUX_ZIP_URL="https://github.com/nilstate/icey-cli/releases/download/${CLI_TAG}/${LINUX_BASENAME}.zip"
WINDOWS_ZIP_URL="https://github.com/nilstate/icey-cli/releases/download/${CLI_TAG}/${WINDOWS_BASENAME}.zip"
DEB_URL="https://github.com/nilstate/icey-cli/releases/download/${CLI_TAG}/icey-server_${CLI_VERSION}_amd64.deb"
APT_REPO_URL="https://github.com/nilstate/icey-cli/releases/download/${CLI_TAG}/icey-server-apt-repo-${CLI_VERSION}.tar.gz"

printf 'ROOT_DIR=%q\n' "$ROOT_DIR"
printf 'ICEY_SOURCE_DIR=%q\n' "$ICEY_SOURCE_DIR"
printf 'CLI_VERSION=%q\n' "$CLI_VERSION"
printf 'ICEY_VERSION=%q\n' "$ICEY_VERSION"
printf 'ICEY_HEAD_COMMIT=%q\n' "$ICEY_HEAD_COMMIT"
printf 'ICEY_LOCAL_TAG_COMMIT=%q\n' "$ICEY_LOCAL_TAG_COMMIT"
printf 'ICEY_REMOTE_TAG_COMMIT=%q\n' "$ICEY_REMOTE_TAG_COMMIT"
printf 'CLI_TAG=%q\n' "$CLI_TAG"
printf 'ICEY_TAG=%q\n' "$ICEY_TAG"
printf 'CLI_SOURCE_DIRNAME=%q\n' "$CLI_SOURCE_DIRNAME"
printf 'ICEY_SOURCE_DIRNAME=%q\n' "$ICEY_SOURCE_DIRNAME"
printf 'LINUX_BASENAME=%q\n' "$LINUX_BASENAME"
printf 'WINDOWS_BASENAME=%q\n' "$WINDOWS_BASENAME"
printf 'CLI_SOURCE_ARCHIVE=%q\n' "$CLI_SOURCE_ARCHIVE"
printf 'ICEY_SOURCE_ARCHIVE=%q\n' "$ICEY_SOURCE_ARCHIVE"
printf 'LINUX_TARBALL=%q\n' "$LINUX_TARBALL"
printf 'LINUX_ZIP=%q\n' "$LINUX_ZIP"
printf 'LATEST_LINUX_TARBALL=%q\n' "$LATEST_LINUX_TARBALL"
printf 'LATEST_LINUX_ZIP=%q\n' "$LATEST_LINUX_ZIP"
printf 'WINDOWS_ZIP=%q\n' "$WINDOWS_ZIP"
printf 'DEB_PATH=%q\n' "$DEB_PATH"
printf 'APT_REPO_ARCHIVE=%q\n' "$APT_REPO_ARCHIVE"
printf 'CLI_SOURCE_URL=%q\n' "$CLI_SOURCE_URL"
printf 'ICEY_SOURCE_URL=%q\n' "$ICEY_SOURCE_URL"
printf 'LINUX_TARBALL_URL=%q\n' "$LINUX_TARBALL_URL"
printf 'LINUX_ZIP_URL=%q\n' "$LINUX_ZIP_URL"
printf 'WINDOWS_ZIP_URL=%q\n' "$WINDOWS_ZIP_URL"
printf 'DEB_URL=%q\n' "$DEB_URL"
printf 'APT_REPO_URL=%q\n' "$APT_REPO_URL"
