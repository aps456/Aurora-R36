#!/bin/sh
# Ports-menu installer. The device-ready release ZIP already contains the
# executable; this script registers the Aurora launcher in the Ports menu.
set -eu
APP_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PORTS_DIR="$(dirname -- "$APP_DIR")"
BIN="$APP_DIR/aurora"

show() {
  if command -v dialog >/dev/null 2>&1 && [ -e /dev/tty ]; then
    dialog --title "Aurora R36" --msgbox "$1" 12 64 </dev/tty >/dev/tty 2>&1 || true
  else
    printf '\nAurora R36\n%s\n' "$1"
    sleep 4
  fi
}

if [ ! -f "$BIN" ]; then
  show "Aurora executable is missing. Download and extract the ARM release ZIP into the ROMS partition first."
  exit 2
fi
chmod 755 "$BIN" "$APP_DIR/Aurora.sh" "$APP_DIR/update.sh"
if ! "$BIN" --version >/dev/null 2>&1; then
  show "The Aurora executable cannot run on this firmware. Check whether you installed the ARMv7 or ARM64 package that matches this device."
  exit 3
fi

# The release archive includes this launcher at the Ports root too. Re-copy it
# from the app directory so reinstalling repairs a missing/outdated menu item.
cp "$APP_DIR/Aurora.sh" "$PORTS_DIR/Aurora.sh"
chmod 755 "$PORTS_DIR/Aurora.sh"
show "Aurora is installed. Open Ports -> Aurora to start the player."
