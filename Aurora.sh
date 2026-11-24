#!/bin/sh
# ArkOS Ports launcher. Works both from /roms/ports/Aurora.sh and from the app folder.
set -u
HERE="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
ROOT="/roms/ports/Aurora-R36"
[ -d "$ROOT" ] || ROOT="$HERE/Aurora-R36"
[ -d "$ROOT" ] || ROOT="$HERE"
cd "$ROOT" || exit 1
BIN="$ROOT/aurora"
[ -x "$BIN" ] || BIN="$ROOT/build/aurora"
if [ ! -x "$BIN" ]; then
  echo "Aurora executable is missing. Use Ports -> Install Aurora first."
  sleep 5
  exit 1
fi
export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-kmsdrm}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-alsa}"
ES_WAS_RUNNING=0
if command -v pgrep >/dev/null 2>&1 && pgrep -x emulationstation >/dev/null 2>&1; then
  ES_WAS_RUNNING=1
  if command -v killall >/dev/null 2>&1; then killall -q emulationstation 2>/dev/null || true; fi
  sleep 1
fi
"$BIN"
STATUS=$?
if [ "$ES_WAS_RUNNING" -eq 1 ]; then
  if command -v emulationstation >/dev/null 2>&1; then
    emulationstation --no-splash
  elif [ -x /usr/bin/emulationstation ]; then
    /usr/bin/emulationstation --no-splash
  fi
fi
exit "$STATUS"
