#!/bin/sh
# Copy the native C source and ArkOS launcher to the mounted ROMs partition.
set -eu
if [ "$#" -ne 1 ] || [ ! -d "$1" ]; then
  echo "Usage: $0 /path/to/roms-partition" >&2
  exit 2
fi
DEST="$1"
HERE="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
mkdir -p "$DEST/ports/Aurora-R36"
cp -R "$HERE/src" "$HERE/Makefile" "$HERE/build.sh" "$HERE/install-device.sh" "$HERE/diagnostics.sh" "$HERE/README.md" "$HERE/LICENSE" "$DEST/ports/Aurora-R36/"
cp "$HERE/Aurora.sh" "$DEST/ports/Aurora.sh"
chmod +x "$DEST/ports/Aurora.sh" "$DEST/ports/Aurora-R36/"*.sh
echo "Copied native Aurora. Build it on the handheld or with a matching ArkOS sysroot."
echo "Safely eject the card before removing it."
