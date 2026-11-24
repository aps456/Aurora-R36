#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
SRC="$ROOT/src/aurora.c"
test -s "$SRC"
grep -q '#include <SDL.h>' "$SRC"
grep -q 'SDL_CreateWindow' "$SRC"
grep -q 'ffprobe' "$SRC"
grep -q 'input-ipc-server' "$SRC"
grep -q 'PAGE_BUTTON_SETUP' "$SRC"
grep -q 'BUTTON MAPPING COMPLETE' "$SRC"
grep -q 'button_setup_done' "$SRC"
grep -q 'CHECK FOR UPDATES' "$SRC"
grep -q 'aps456/Aurora-R36' "$ROOT/github_repo.txt"
grep -q 'Aurora_R36_aarch64.zip' "$ROOT/update.sh"
grep -q 'Aurora_R36_armv7.zip' "$ROOT/update.sh"
grep -q 'ports/Aurora-R36/aurora' "$ROOT/package-release.sh"
grep -q 'docker/setup-qemu-action' "$ROOT/.github/workflows/release.yml"
grep -q 'softprops/action-gh-release@v3' "$ROOT/.github/workflows/release.yml"
for f in "$ROOT/Aurora.sh" "$ROOT/Install Aurora.sh" "$ROOT/install.sh" "$ROOT/update.sh" "$ROOT/build.sh" "$ROOT/deploy-to-sd.sh" "$ROOT/install-device.sh" "$ROOT/diagnostics.sh" "$ROOT/package-release.sh" "$ROOT/tests/test_package.sh"; do
  test -f "$f"
  sh -n "$f"
done
# Source ZIP should carry executable bits for all shell entry points.
for f in "$ROOT"/*.sh "$ROOT"/tests/*.sh; do test -x "$f"; done
test -f "$ROOT/Makefile"
test -f "$ROOT/.github/workflows/release.yml"
echo "Aurora source, installer, updater, workflow and executable-bit checks passed."
