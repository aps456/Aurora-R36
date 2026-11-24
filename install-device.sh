#!/bin/sh
# Build Aurora natively on the handheld (or in a matching ArkOS sysroot).
set -eu
cd "$(dirname "$0")"
missing=""
for tool in cc make sdl2-config; do
  command -v "$tool" >/dev/null 2>&1 || missing="$missing $tool"
done
for tool in mpv ffmpeg ffprobe; do
  command -v "$tool" >/dev/null 2>&1 || missing="$missing $tool"
done
if [ -n "$missing" ]; then
  echo "Missing tools/dependencies:$missing"
  echo "Install the SDL2 development package, compiler and Make, plus MPV and FFmpeg."
  echo "Package names vary between ArkOS / ArkOS4Clone images; this script does not change firmware packages automatically."
  exit 2
fi
./build.sh
echo "Build complete. Open Ports -> Aurora R36."
