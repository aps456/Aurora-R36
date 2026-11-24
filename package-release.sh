#!/bin/sh
# Build a device-ready ArkOS Ports ZIP from build/aurora.
set -eu
ARCH="${1:-}"
case "$ARCH" in armv7|aarch64) ;; *) echo "Usage: sh package-release.sh armv7|aarch64" >&2; exit 2;; esac
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
BIN="$ROOT/build/aurora"
[ -f "$BIN" ] || { echo "Missing $BIN; compile Aurora first." >&2; exit 2; }
DIST="$ROOT/dist-$ARCH"
OUT="$ROOT/Aurora_R36_${ARCH}.zip"
rm -rf "$DIST" "$OUT"
mkdir -p "$DIST/ports/Aurora-R36"
install -m 755 "$BIN" "$DIST/ports/Aurora-R36/aurora"
install -m 755 "$ROOT/Aurora.sh" "$DIST/ports/Aurora-R36/Aurora.sh"
install -m 755 "$ROOT/update.sh" "$DIST/ports/Aurora-R36/update.sh"
install -m 755 "$ROOT/install.sh" "$DIST/ports/Aurora-R36/install.sh"
install -m 644 "$ROOT/github_repo.txt" "$DIST/ports/Aurora-R36/github_repo.txt"
install -m 644 "$ROOT/README.md" "$DIST/ports/Aurora-R36/README.md"
install -m 644 "$ROOT/LICENSE" "$DIST/ports/Aurora-R36/LICENSE"
install -m 755 "$ROOT/Install Aurora.sh" "$DIST/ports/Install Aurora.sh"
install -m 755 "$ROOT/Aurora.sh" "$DIST/ports/Aurora.sh"
printf 'Aurora R36\nVersion: 0.3.0\nBuild architecture: %s\nBuild userspace: Debian Bullseye (glibc 2.31 baseline)\nRepository: aps456/Aurora-R36\n' "$ARCH" > "$DIST/ports/Aurora-R36/build-info.txt"
chmod 755 "$DIST/ports"/*.sh "$DIST/ports/Aurora-R36"/*.sh "$DIST/ports/Aurora-R36/aurora"
(cd "$DIST" && zip -qr "$OUT" ports)
echo "Created $OUT"
