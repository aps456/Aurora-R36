#!/bin/sh
# In-app updater. Replaces only the program binary; personal data is untouched.
set -eu
APP_DIR="${AURORA_HOME:-/roms/ports/Aurora-R36}"
REPO_FILE="$APP_DIR/github_repo.txt"
STATUS_FILE="/tmp/aurora-update-status.txt"
write_status() { printf '%s\n' "$1" > "$STATUS_FILE"; }

if [ ! -f "$REPO_FILE" ]; then write_status "Update source is not configured."; exit 2; fi
REPO="$(tr -d ' \r\n' < "$REPO_FILE")"
case "$REPO" in */*) ;; *) write_status "Invalid GitHub owner/repository."; exit 2;; esac
OWNER="${REPO%%/*}"; NAME="${REPO#*/}"
case "$OWNER$NAME" in *[!A-Za-z0-9_.-]*) write_status "Invalid repository name."; exit 2;; esac
command -v curl >/dev/null 2>&1 || { write_status "curl is required for updates."; exit 2; }
command -v unzip >/dev/null 2>&1 || { write_status "unzip is required for updates."; exit 2; }

ARCH="$(uname -m)"
case "$ARCH" in
  aarch64|arm64) ASSET="Aurora_R36_aarch64.zip" ;;
  armv7l|armv6l|armhf|arm) ASSET="Aurora_R36_armv7.zip" ;;
  *) write_status "Unsupported device architecture: $ARCH"; exit 2 ;;
esac

TMP="/tmp/aurora-update-$$"
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT HUP INT TERM
API="https://api.github.com/repos/$OWNER/$NAME/releases/latest"
if ! curl -fsSL "$API" -o "$TMP/release.json"; then
  write_status "Could not contact GitHub Releases. Check the network and try again."
  exit 1
fi
URL="$(sed -n 's/.*"browser_download_url":[[:space:]]*"\([^"]*'"$ASSET"'[^"]*\)".*/\1/p' "$TMP/release.json" | head -n 1)"
TAG="$(sed -n 's/.*"tag_name":[[:space:]]*"\([^"]*\)".*/\1/p' "$TMP/release.json" | head -n 1)"
if [ -z "$URL" ]; then
  write_status "No published $ASSET asset was found. A GitHub Release must exist first."
  exit 1
fi
if ! curl -fL "$URL" -o "$TMP/update.zip"; then
  write_status "Aurora update download failed. The installed version was not changed."
  exit 1
fi
if ! unzip -oq "$TMP/update.zip" -d "$TMP/unpacked"; then
  write_status "Downloaded update archive is invalid. The installed version was not changed."
  exit 1
fi
BIN="$(find "$TMP/unpacked" -type f -name aurora -print | head -n 1)"
if [ -z "$BIN" ]; then
  write_status "Release archive did not contain the Aurora executable."
  exit 1
fi
# Verify the downloaded executable before replacing the current one.
if ! "$BIN" --version >/dev/null 2>&1; then
  write_status "Downloaded binary is not compatible. The installed version was not changed."
  exit 1
fi
cp "$BIN" "$APP_DIR/aurora.new"
chmod 755 "$APP_DIR/aurora.new"
if [ -f "$APP_DIR/aurora" ]; then cp "$APP_DIR/aurora" "$APP_DIR/aurora.previous"; fi
if ! mv "$APP_DIR/aurora.new" "$APP_DIR/aurora"; then
  [ ! -f "$APP_DIR/aurora.previous" ] || mv "$APP_DIR/aurora.previous" "$APP_DIR/aurora"
  write_status "Could not install the update. Previous version restored."
  exit 1
fi
rm -f "$APP_DIR/aurora.previous"
write_status "Aurora updated to ${TAG:-latest}. Restart Aurora to use the new version."
