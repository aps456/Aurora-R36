#!/bin/sh
set +e
cd "$(dirname "$0")"
echo "=== Aurora R36 native diagnostics ==="
uname -a
echo "SDL_VIDEODRIVER=${SDL_VIDEODRIVER:-<unset>}"
echo "SDL_AUDIODRIVER=${SDL_AUDIODRIVER:-<unset>}"
for t in cc make sdl2-config mpv ffmpeg ffprobe; do
  printf '%-14s' "$t"
  command -v "$t" || true
done
sdl2-config --version 2>/dev/null || true
mpv --version 2>/dev/null | head -n 1
ffmpeg -version 2>/dev/null | head -n 1
[ -x build/aurora ] && build/aurora --version || echo "Native binary not built yet."
